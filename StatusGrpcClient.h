// StatusGrpcClient.h

#pragma once
#include "global.h"
#include "Singleton.h"
#include "ConfigManager.h"
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::HeartbeatRsp;
using message::LoginRsp;
using message::LoginReq;
using message::QueryUserRouteRsp;
using message::RegisterChatServerRsp;
using message::ReportUserOfflineRsp;
using message::ReportUserOnlineRsp;
using message::StatusService;

class StatusConPool {
public:
    StatusConPool(size_t poolSize,
        const std::string& host,
        const std::string& port);
    ~StatusConPool();

    std::unique_ptr<StatusService::Stub> getConnection();
    void returnConnection(std::unique_ptr<StatusService::Stub> stub);
    void Close();

private:
    size_t    poolSize_;
    std::atomic<bool> b_stop_;
    std::queue<std::unique_ptr<StatusService::Stub>> connections_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

class StatusGrpcClient : public Singleton<StatusGrpcClient>
{
    friend class Singleton<StatusGrpcClient>;
public:
    ~StatusGrpcClient();

    GetChatServerRsp GetChatServer(int uid);
    LoginRsp Login(int uid, const std::string& token);
    RegisterChatServerRsp RegisterChatServer();
    HeartbeatRsp Heartbeat();
    ReportUserOnlineRsp ReportUserOnline(int uid, const std::string& token);
    ReportUserOfflineRsp ReportUserOffline(int uid);
    QueryUserRouteRsp QueryUserRoute(int uid);

    void StartHeartbeat();
    void StopHeartbeat();
    const std::string& ServerId() const;

private:
    StatusGrpcClient();
    void HeartbeatLoop();

    std::unique_ptr<StatusConPool> pool_;
    std::string self_host_;
    std::string self_grpc_host_;
    std::string self_port_;
    std::string self_grpc_port_;
    std::string self_server_id_;
    std::atomic<bool> heartbeat_stop_;
    std::atomic<bool> heartbeat_started_;
    std::thread heartbeat_thread_;
};
