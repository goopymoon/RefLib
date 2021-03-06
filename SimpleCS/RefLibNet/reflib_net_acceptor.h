#pragma once

#include <vector>
#include <memory>
#include "reflib_net_completion.h"

#define SOCKETADDR_BUFFER_SIZE  (sizeof(SOCKADDR_STORAGE) + 16)

namespace RefLib
{

class NetSocketBase;
class NetConnection;

class AcceptBuffer : public NetCompletionOP
{
public:
    AcceptBuffer() 
        : NetCompletionOP(NetCompletionOP::OP_ACCEPT)
    {
        memset(_data, 0x00, SOCKETADDR_BUFFER_SIZE * 2);
    }

    void Reset(SOCKET sock = INVALID_SOCKET)
    {
        NetCompletionOP::Reset(sock);
        memset(_data, 0x00, SOCKETADDR_BUFFER_SIZE * 2);
    }

    WSAOVERLAPPED& GetOL() { return ol; }
    SOCKET GetSocket() { return client; }
    char* GetData() { return &_data[0]; }

private:
    char _data[SOCKETADDR_BUFFER_SIZE * 2];
};

class NetAcceptor
{
public:
    NetAcceptor(NetSocketBase* sock, HANDLE compPort);
    ~NetAcceptor();

    void Accepts();
    void OnAccept(std::weak_ptr<NetConnection> clientobj, NetCompletionOP* bufObj);

private:
    bool PostAccept(AcceptBuffer* acceptObj);

    std::vector<AcceptBuffer*> _pendingAccepts;
    NetSocketBase* _listenSock;
    HANDLE _comPort;
};

} // namespace RefLib
