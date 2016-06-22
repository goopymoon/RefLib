#pragma once

#include <vector>
#include <concurrent_queue.h>
#include <memory>
#include "reflib_runable_threads.h"
#include "reflib_safelock.h"
#include "reflib_composit_id.h"

namespace RefLib
{

class GameNetObj;
class NetListener;
class NetWorkerServer;

class NetService
    : public RefLib::RunableThreads
    , public std::enable_shared_from_this<NetService>
{
public:
    NetService();
    virtual ~NetService();

    bool Initialize(unsigned port, uint32 maxCnt, uint32 concurrency);
    void Shutdown();

    bool Register(std::weak_ptr<GameNetObj> obj);

    HANDLE GetCompletionPort() const { return _comPort; }

    std::weak_ptr<GameNetObj> GetObj();
    std::weak_ptr<GameNetObj> GetObj(CompositId id);
    bool FreeObj(const CompositId& id);

protected:
    // run by thread
    virtual unsigned Run() override;

private:
    typedef std::vector<std::shared_ptr<GameNetObj>> GAME_NET_OBJS;

    Concurrency::concurrent_queue<std::shared_ptr<GameNetObj>> _freeObj;
    GAME_NET_OBJS _objs;

    std::unique_ptr<NetListener> _netListener;
    std::unique_ptr<NetWorkerServer> _netWorker;

    uint32 _maxCnt;
    HANDLE _comPort;
};

} // namespace RefLib