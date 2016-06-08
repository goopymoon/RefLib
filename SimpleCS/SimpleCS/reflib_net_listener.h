#pragma once

#include "reflib_net_completion.h"
#include "reflib_net_socket_base.h"

namespace RefLib
{

class NetAcceptor;
class NetConnection;
class NetConnectionMgr;
class NetCompletionOP;

class NetListener : public NetSocketBase
{
public:
    NetListener();
    ~NetListener();

    bool Initialize(unsigned reserve);
    bool Listen(unsigned port);
    void Shutdown();

    void FreeNetConn(NetConnection* conn);

    virtual void OnCompletionSuccess(NetCompletionOP* bufObj, DWORD bytesTransfered) override;
    virtual void OnCompletionFailure(NetCompletionOP* bufObj, DWORD bytesTransfered, int error) override;

private:
    void OnAccept(NetCompletionOP* bufObj);

    NetConnectionMgr*   _netConnMgr;
    NetAcceptor*        _acceptor;
};

} // namespace RefLib
