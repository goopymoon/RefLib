#pragma once

#include <concurrent_queue.h>
#include <queue>
#include "reflib_net_overlapped.h"
#include "reflib_net_socket_base.h"
#include "reflib_circular_buffer.h"
#include "reflib_safelock.h"

namespace RefLib
{

class NetIoBuffer : public NetCompletionOP
{
public:
    NetIoBuffer() {}
    ~NetIoBuffer();

    void PushData(MemoryBlock* data) 
    { 
        _data.push(data);
    }

    bool PopData(MemoryBlock* buffer) 
    {
        if (!_data.empty())
        {
            buffer = _data.front();
            _data.pop();
            return true;
        }
        return false;
    }

private:
    std::queue<MemoryBlock*> _data;
};

class NetSocket : public NetSocketBase
{
public:
    NetSocket() {}
    virtual ~NetSocket() {}

    bool Initialize(SOCKET sock);

    void Send(char* data, uint32 dataLen);

    virtual void OnDisconnected() override;
    virtual void OnCompletion(NetCompletionOP* bufObj, DWORD bytesTransfered) override;

    bool PostRecv();
    bool PostSend();

    void OnConnected();

private:
    void OnRecv(NetIoBuffer* recvOP, DWORD bytesTransfered);
    void OnSent(NetIoBuffer* sendOP, DWORD bytesTransfered);

    void ClearRecvQueue();
    void ClearSendQueue();

    Concurrency::concurrent_queue<MemoryBlock*> _sendPendingQueue;

    CircularBuffer  _recvBuffer;
    SafeLock        _recvLock;
};

} // namespace RefLib