#include "stdafx.h"

#include <list>
#include "reflib_net_socket.h"
#include "reflib_netio_buffer.h"
#include "reflib_net_listener.h"
#include "reflib_memory_pool.h"
#include "reflib_packet_header_obj.h"

namespace RefLib
{

NetSocket::NetSocket()
{
}

bool NetSocket::Initialize(SOCKET sock) 
{ 
    REFLIB_ASSERT_RETURN_VAL_IF_FAILED(sock != INVALID_SOCKET, "Socket is invalid", false);
    NetSocketBase::SetSocket(sock);

    return true;
}

void NetSocket::ClearRecvQueue()
{
    SafeLock::Owner guard(_recvLock);

    REFLIB_ASSERT(_recvBuffer.Size() == 0, "Recv buffer is not empty.");
    _recvBuffer.Clear();
}

void NetSocket::ClearSendQueue()
{
    MemoryBlock* buffer = nullptr;

    REFLIB_ASSERT(_sendPendingQueue.empty(), "Send pending queue is not empty");
    while (_sendPendingQueue.try_pop(buffer))
    {
        g_memoryPool.FreeBuffer(buffer);
    }

    REFLIB_ASSERT(_sendQueue.empty(), "Send queue is not empty");
    while (_sendQueue.try_pop(buffer))
    {
        g_memoryPool.FreeBuffer(buffer);
    }
}

bool NetSocket::PostRecv()
{
    _netStatus.fetch_or(NET_STATUS_RECV_PENDING);

    NetIoBuffer* recvOP = new NetIoBuffer(NetCompletionOP::OP_READ);
    recvOP->Reset(GetSocket());

    MemoryBlock* buffer = recvOP->Alloc(MAX_PACKET_SIZE);

    WSABUF wbuf;
    wbuf.buf = buffer->GetData();
    wbuf.len = buffer->GetDataLen();

    DWORD flags = 0;
    int rc = WSARecv(
        GetSocket(),
        &wbuf,
        1,
        NULL,
        &flags,
        &(recvOP->ol),
        NULL
    );

    if (rc == SOCKET_ERROR)
    {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            delete recvOP;

            DebugPrint("PostRecv: WSARecv* failed: %s", SocketGetErrorString(error).c_str());
            Disconnect(NET_CTYPE_SYSTEM);

            return false;
        }
    }

    return true;
}

void NetSocket::Send(char* data, uint16 dataLen)
{
    PacketHeaderObj packet;
    packet.SetHeader(dataLen);

    MemoryBlock* buffer = g_memoryPool.GetBuffer(dataLen + PACKET_HEADER_SIZE);

    memcpy(buffer->GetData(), packet.header.blob, PACKET_HEADER_SIZE);
    memcpy(buffer->GetData() + PACKET_HEADER_SIZE, data, dataLen);

    _sendPendingQueue.push(buffer);

    PrepareSend();
}

void NetSocket::PrepareSend()
{
    unsigned int sendPacketSize = 0;
    MemoryBlock* buffer = nullptr;

    while (!_sendPendingQueue.empty()
        && (_sendQueue.unsafe_size() < MAX_SEND_ARRAY_SIZE)
        && (sendPacketSize < DEF_SOCKET_BUFFER_SIZE))
    {
        _sendPendingQueue.try_pop(buffer);
        _sendQueue.push(buffer);

        sendPacketSize += buffer->GetDataLen();
    }

    if (sendPacketSize > 0)
        PostSend();
}

bool NetSocket::PostSend()
{
    int status = _netStatus.load();
    int expected = status | NET_STATUS_CONNECTED & NET_STATUS_RECV_PENDING & (!NET_STATUS_SEND_PENDING);
    int desired = expected | NET_STATUS_SEND_PENDING;

    if (!_netStatus.compare_exchange_strong(expected, desired))
    {
        DebugPrint("PostSend: netStatus exchange failed: current(0x%x)", status);
        return false;
    }

    size_t sendQueueSize = _sendQueue.unsafe_size();
    if (sendQueueSize == 0)
    {
        DebugPrint("PostSend: send queue is empty.");
        return false;
    }

    NetIoBuffer* sendOP = new NetIoBuffer(NetCompletionOP::OP_WRITE);
    sendOP->Reset(GetSocket());

    std::vector<WSABUF> wbufs;
    wbufs.resize(sendQueueSize);

    MemoryBlock* buffer = nullptr;
    int idx = 0;
    while (_sendQueue.try_pop(buffer))
    {
        wbufs[idx].buf = buffer->GetData();
        wbufs[idx].len = buffer->GetDataLen();
        sendOP->PushData(buffer);
        idx++;
    }

    int rc = WSASend(
        GetSocket(),
        &(wbufs[0]),
        static_cast<DWORD>(wbufs.size()),
        NULL,
        0,
        &(sendOP->ol),
        NULL
    );

    if (rc == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            DebugPrint("PostSend: WSASend* failed: %s", SocketGetLastErrorString().c_str());
            Disconnect(NET_CTYPE_SYSTEM);
            delete sendOP;

            return false;
        }
    }

    return true;
}

void NetSocket::OnCompletionFailure(NetCompletionOP* bufObj, DWORD bytesTransfered, int error)
{
    REFLIB_ASSERT_RETURN_IF_FAILED(bufObj, "OnCOmpletionFailure: NetCompletionOP is nullptr.");

    DebugPrint("NetSocket] Socket(%d), OP(%d), Error(%d)", bufObj->client, bufObj->op, error);

    switch (bufObj->op)
    {
    case NetCompletionOP::OP_CONNECT:
    case NetCompletionOP::OP_DISCONNECT:
        break;
    case NetCompletionOP::OP_READ:
    case NetCompletionOP::OP_WRITE:
        delete bufObj;
        break;
    default:
        delete bufObj;
        REFLIB_ASSERT(false, "Invalid net op");
        break;
    }
    return;
}

void NetSocket::OnCompletionSuccess(NetCompletionOP* bufObj, DWORD bytesTransfered)
{
    REFLIB_ASSERT_RETURN_IF_FAILED(bufObj, "OnCOmpletionFailure: NetCompletionOP is nullptr.");

    switch (bufObj->op)
    {
    case NetCompletionOP::OP_CONNECT:
        OnConnected();
        break;
    case NetCompletionOP::OP_READ:
        OnRecv(bufObj, bytesTransfered);
        break;
    case NetCompletionOP::OP_WRITE:
        OnSent(bufObj, bytesTransfered);
        break;
    case NetCompletionOP::OP_DISCONNECT:
        OnDisconnected();
        break;
    default:
        REFLIB_ASSERT(false, "Invalid net op");
        break;
    }
}

void NetSocket::OnConnected()
{
    NetSocketBase::OnConnected();

    PostRecv();
}

void NetSocket::OnDisconnected()
{
    NetSocketBase::OnDisconnected();

    ClearRecvQueue();
    ClearSendQueue();
}

void NetSocket::OnRecv(NetCompletionOP* recvOP, DWORD bytesTransfered)
{
    REFLIB_ASSERT_RETURN_IF_FAILED(recvOP, "NetIoBuffer is null");

    NetIoBuffer *ioBuffer = reinterpret_cast<NetIoBuffer*>(recvOP);
    MemoryBlock* buffer;

    if (buffer = ioBuffer->PopData())
    {
        OnRecvData(buffer->GetData(), bytesTransfered);
    }
    else
    {
        Disconnect(NetCloseType::NET_CTYPE_SYSTEM);
    }

    delete ioBuffer;
    _netStatus.fetch_and(~NET_STATUS_RECV_PENDING);

    PostRecv();
}

void NetSocket::OnRecvData(const char* data, int dataLen)
{
    REFLIB_ASSERT_RETURN_IF_FAILED(data, "null data received.");
    REFLIB_ASSERT_RETURN_IF_FAILED(dataLen, "null size data received.");

    {
        SafeLock::Owner guard(_recvLock);
        REFLIB_ASSERT(_recvBuffer.PutData(data, dataLen), "Data loss: Not enough space");
    }

    MemoryBlock* buffer = nullptr;
    ePACKET_EXTRACT_RESULT ret = PER_NO_DATA;

    while ((ret = ExtractPakcetData(buffer)) == PER_SUCCESS)
    {
        if (!RecvPacket(buffer))
        {
            ret = PER_ERROR;
            break;
        }
    };

    if (ret == PER_ERROR)
        Disconnect(NET_CTYPE_SYSTEM);
}

NetSocket::ePACKET_EXTRACT_RESULT NetSocket::ExtractPakcetData(MemoryBlock*& buffer)
{
    PacketHeaderObj packetObj;

    SafeLock::Owner guard(_recvLock);

    if (!_recvBuffer.GetData(packetObj.header.blob, PACKET_HEADER_SIZE))
    {
        buffer = nullptr;
        return PER_NO_DATA;
    }

    if (!packetObj.IsValidEnvTag())
    {
        DebugPrint("Invalid packet envelop tag");
        buffer = nullptr;
        return PER_ERROR;
    }

    if (!packetObj.IsValidContentLength())
    {
        DebugPrint("Invalid packet conetnt length");
        buffer = nullptr;
        return PER_ERROR;
    }

    uint16 contentLen = packetObj.GetContentLen();

    buffer = g_memoryPool.GetBuffer(contentLen);
    _recvBuffer.GetData(buffer->GetData(), contentLen);

    return PER_SUCCESS;
}

void NetSocket::OnSent(NetCompletionOP* sendOP, DWORD bytesTransfered)
{
    delete sendOP;
    _netStatus.fetch_and(~NET_STATUS_SEND_PENDING);

    PrepareSend();
}

} // namespace RefLib