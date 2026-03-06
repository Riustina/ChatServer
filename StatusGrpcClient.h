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

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::LoginRsp;
using message::LoginReq;
using message::StatusService;

// ──────────────────────────────────────────────
// gRPC Stub 连接池
// ──────────────────────────────────────────────
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
    std::mutex              mutex_;
    std::condition_variable cond_;
};

// ──────────────────────────────────────────────
// StatusGrpcClient 单例
// ──────────────────────────────────────────────
class StatusGrpcClient : public Singleton<StatusGrpcClient>
{
    friend class Singleton<StatusGrpcClient>;
public:
    ~StatusGrpcClient() = default;

    // 向 StatusServer 申请为 uid 分配一台 ChatServer
    GetChatServerRsp GetChatServer(int uid);
    LoginRsp Login(int uid, const std::string& token);

private:
    StatusGrpcClient();

    std::unique_ptr<StatusConPool> pool_;
};
