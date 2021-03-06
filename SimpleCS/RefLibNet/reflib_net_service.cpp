#include "stdafx.h"

#include <algorithm>
#include "reflib_net_service.h"
#include "reflib_net_connection.h"
#include "reflib_net_connection_manager.h"
#include "reflib_net_listener.h"
#include "reflib_net_connector.h"
#include "reflib_net_worker.h"
#include "reflib_net_obj.h"
#include "reflib_def.h"
#include "reflib_net_api.h"

namespace RefLib
{

///////////////////////////////////////////////////////////////////
// NetService

NetService::NetService()
    : _maxCnt(0)
    , _comPort(INVALID_HANDLE_VALUE)
{
}

NetService::~NetService()
{
    if (_comPort)
    {
        CloseHandle(_comPort);
        _comPort = nullptr;
    }
}

bool NetService::InitServer(uint32 maxCnt, uint32 concurrency)
{
	if (!g_network.Initialize())
		return false;

    if (_netConnectionProxy.get())
    {
        DebugPrint("Already initialized");
        return false;
    }

    _comPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, (ULONG_PTR)nullptr, concurrency);
    if (!_comPort)
    {
        DebugPrint("CreateIoCompletionPort failed: %d", GetLastError());
        return false;
    }

    _maxCnt = maxCnt;
    _objs.resize(maxCnt);

    _netConnectionProxy = std::make_unique<NetListener>(this);
    if (!_netConnectionProxy->Initialize(maxCnt, concurrency))
        return false;

    return CreateThreads(concurrency);
}

bool NetService::InitClient(uint32 maxCnt, uint32 concurrency)
{
	if (!g_network.Initialize())
		return false;

	if (_netConnectionProxy.get())
    {
        DebugPrint("Already initialized");
        return false;
    }

    _comPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, (ULONG_PTR)nullptr, concurrency);
    if (!_comPort)
    {
        DebugPrint("CreateIoCompletionPort failed: %d", GetLastError());
        return false;
    }

    _maxCnt = maxCnt;
    _objs.resize(maxCnt);

    _netConnectionProxy = std::make_unique<NetConnector>(this);
    if (!_netConnectionProxy->Initialize(maxCnt, concurrency))
        return false;

    if (!CreateThreads(concurrency))
        return false;

    RunableThreads::Activate();

    return true;
}

bool NetService::AddListeningObj(std::weak_ptr<NetObj> obj)
{
    if (_netConnectionProxy->GetChildType() != NET_CTYPE_LISTENER)
    {
        DebugPrint("NetService is not initialized for Listening.");
        return false;
    }

    return RegisterNetObj(obj);
}

void NetService::StartListen(unsigned port)
{
    if (_netConnectionProxy->GetChildType() != NET_CTYPE_LISTENER)
    {
        DebugPrint("NetService is not initialized for Listening.");
        return;
    }
    RunableThreads::Activate();
    _netConnectionProxy->Listen(port);
}

bool NetService::Connect(const std::string& ipStr, uint32 port, std::weak_ptr<NetObj> obj)
{
    if (_netConnectionProxy->GetChildType() != NET_CTYPE_CONNECTOR)
    {
        DebugPrint("NetService is not initialized for Connector.");
        return false;
    }

    if (!RegisterNetObj(obj))
        return false;

    return _netConnectionProxy->Connect(ipStr, port, obj);
}

bool NetService::RegisterNetObj(std::weak_ptr<NetObj> obj)
{
    auto p = obj.lock();
    if (!p) return false;

    auto con = _netConnectionProxy->RegisterCon().lock();
    if (!con) return false;

    if (p->Initialize(con))
    {
        con->RegisterParent(p);

        SafeLock::Owner lock(_freeLock);

        uint32 slot = static_cast<uint32>(_freeObjs.size());
        _freeObjs.emplace(slot, p);

        return true;
    }

    return false;
}

std::weak_ptr<NetObj> NetService::GetNetObj(const CompositId& id)
{
    if (id.GetSlotId() >= _maxCnt)
        return std::weak_ptr<NetObj>();

    return _objs[id.GetSlotId()];
}

bool NetService::AllocNetObj(const CompositId& id)
{
    if (id.GetSlotId() >= _maxCnt)
        return false;

    SafeLock::Owner lock(_freeLock);

    auto it = _freeObjs.find(id.GetSlotId());
    if (it == _freeObjs.end())
        return false;

    std::shared_ptr<NetObj> p = it->second;

    _freeObjs.erase(it);
    _objs[id.GetSlotId()] = p;

    return true;
}

bool NetService::FreeNetObj(const CompositId& id)
{
    if (id.GetSlotId() >= _maxCnt)
        return false;

    auto p = _objs[id.GetSlotId()];
    if (!p)
        return false;

    _freeObjs.emplace(id.GetSlotId(), p);
    _objs[id.GetSlotId()].reset();

    return true;
}

void NetService::Run()
{
    ULONG_PTR ulKey;
    OVERLAPPED *lpOverlapped;
    DWORD bytesTransfered;

    int rc = GetQueuedCompletionStatus(_comPort, &bytesTransfered,
        &ulKey, &lpOverlapped, THREAD_TIMEOUT_IN_MSEC);

    // Check time out
    if (rc == FALSE && lpOverlapped == nullptr)
        return;

    if (NetObj* obj = (NetObj*)ulKey)
    {
        obj->OnRecvPacket();
    }
}

void NetService::Shutdown()
{
    DebugPrint("--Shutdown NetService--");

    if (_netConnectionProxy)
    {
        DebugPrint("Shutdown NetConnectionProxy.");
        _netConnectionProxy->Shutdown();
    }

    Join();
}

void NetService::OnTerminated()
{
    Deactivate();
}

///////////////////////////////////////////////////////////////////
// NetServerService

bool NetServerService::Initialize(uint32 maxCnt, uint32 concurrency)
{
	return InitServer(maxCnt, concurrency);
}

bool NetServerService::AddListeningObj(std::weak_ptr<NetObj> obj)
{
	return NetService::AddListeningObj(obj);
}

void NetServerService::StartListen(unsigned port)
{
	return NetService::StartListen(port);
}

///////////////////////////////////////////////////////////////////
// NetClientService

bool NetClientService::Initialize(uint32 maxCnt, uint32 concurrency)
{
	return InitClient(maxCnt, concurrency);
}

bool NetClientService::Connect(const std::string& ipStr, uint32 port, std::weak_ptr<NetObj> obj)
{
	return NetService::Connect(ipStr, port, obj);
};

} //namespace RefLib