// StatusGrpcClient.cpp

#include "StatusGrpcClient.h"
#include <chrono>
#include <iostream>
#include "Defer.h"

namespace {
std::string buildServerId(const std::string& host, const std::string& port)
{
    return host + ":" + port;
}

constexpr auto kHeartbeatInterval = std::chrono::seconds(3);
}

StatusConPool::StatusConPool(size_t poolSize,
    const std::string& host,
    const std::string& port)
    : poolSize_(poolSize)
    , b_stop_(false)
{
    const std::string address = host + ":" + port;
    for (size_t i = 0; i < poolSize_; ++i) {
        auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        connections_.push(StatusService::NewStub(channel));
    }
    std::cout << "[StatusGrpcClient.cpp] StatusConPool [构造] 连接池初始化完成，地址: "
        << address << "，连接数: " << poolSize_ << "\n";
}

StatusConPool::~StatusConPool()
{
    Close();
}

std::unique_ptr<StatusService::Stub> StatusConPool::getConnection()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return b_stop_.load() || !connections_.empty(); });

    if (b_stop_.load()) {
        std::cerr << "[StatusGrpcClient.cpp] getConnection [获取连接] 连接池已关闭\n";
        return nullptr;
    }

    auto stub = std::move(connections_.front());
    connections_.pop();
    return stub;
}

void StatusConPool::returnConnection(std::unique_ptr<StatusService::Stub> stub)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (b_stop_.load()) {
        std::cerr << "[StatusGrpcClient.cpp] returnConnection [归还连接] 连接池已关闭，丢弃连接\n";
        return;
    }

    connections_.push(std::move(stub));
    cond_.notify_one();
}

void StatusConPool::Close()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_.load()) {
            return;
        }
        b_stop_.store(true);
        while (!connections_.empty()) {
            connections_.pop();
        }
    }
    cond_.notify_all();
    std::cout << "[StatusGrpcClient.cpp] Close [Close] 连接池已关闭，所有连接已释放\n";
}

StatusGrpcClient::StatusGrpcClient()
    : heartbeat_stop_(false)
    , heartbeat_started_(false)
{
    auto& cfg = ConfigManager::getInstance();
    const std::string host = cfg["StatusServer"]["host"];
    const std::string port = cfg["StatusServer"]["port"];
    self_host_ = cfg["SelfServer"]["Host"];
    self_port_ = cfg["SelfServer"]["Port"];
    self_grpc_port_ = cfg["SelfServer"]["GrpcPort"];
    self_server_id_ = buildServerId(self_host_, self_port_);
    pool_ = std::make_unique<StatusConPool>(5, host, port);
}

StatusGrpcClient::~StatusGrpcClient()
{
    StopHeartbeat();
    if (pool_) {
        pool_->Close();
    }
}

GetChatServerRsp StatusGrpcClient::GetChatServer(int uid)
{
    GetChatServerRsp reply;
    auto stub = pool_->getConnection();
    if (stub == nullptr) {
        std::cerr << "[StatusGrpcClient.cpp] GetChatServer [GetChatServer] 获取 gRPC 连接失败，uid: " << uid << "\n";
        reply.set_error(ErrorCodes::RPC_Failed);
        return reply;
    }

    Defer defer([&stub, this]() { pool_->returnConnection(std::move(stub)); });

    GetChatServerReq request;
    request.set_uid(uid);
    ClientContext context;

    Status status = stub->GetChatServer(&context, request, &reply);
    if (status.ok()) {
        std::cout << "[StatusGrpcClient.cpp] GetChatServer [GetChatServer] 分配 ChatServer 成功，uid: "
            << uid << "，host: " << reply.host() << "，server_id: " << reply.server_id() << "\n";
        return reply;
    }

    std::cerr << "[StatusGrpcClient.cpp] GetChatServer [GetChatServer] gRPC 调用失败，uid: "
        << uid << "，错误: " << status.error_message() << "\n";
    reply.set_error(ErrorCodes::RPC_Failed);
    return reply;
}

LoginRsp StatusGrpcClient::Login(int uid, const std::string& token)
{
    LoginRsp reply;
    auto stub = pool_->getConnection();
    if (stub == nullptr) {
        std::cerr << "[StatusGrpcClient.cpp] Login [Login] 获取 gRPC 连接失败，uid: " << uid << "\n";
        reply.set_error(ErrorCodes::RPC_Failed);
        return reply;
    }

    Defer defer([&stub, this]() { pool_->returnConnection(std::move(stub)); });

    LoginReq request;
    request.set_uid(uid);
    request.set_token(token);
    ClientContext context;

    Status status = stub->Login(&context, request, &reply);
    if (status.ok()) {
        std::cout << "[StatusGrpcClient.cpp] Login [Login] token 校验成功，uid: " << uid << "\n";
        return reply;
    }

    std::cerr << "[StatusGrpcClient.cpp] Login [Login] gRPC 调用失败，uid: "
        << uid << "，错误: " << status.error_message() << "\n";
    reply.set_error(ErrorCodes::RPC_Failed);
    return reply;
}

RegisterChatServerRsp StatusGrpcClient::RegisterChatServer()
{
    RegisterChatServerRsp reply;
    auto stub = pool_->getConnection();
    if (stub == nullptr) {
        std::cerr << "[StatusGrpcClient.cpp] RegisterChatServer [RegisterChatServer] 获取 gRPC 连接失败\n";
        reply.set_error(ErrorCodes::RPC_Failed);
        return reply;
    }

    Defer defer([&stub, this]() { pool_->returnConnection(std::move(stub)); });

    message::RegisterChatServerReq request;
    request.set_server_id(self_server_id_);
    request.set_host(self_host_);
    request.set_port(self_port_);
    request.set_grpc_port(self_grpc_port_);
    ClientContext context;

    Status status = stub->RegisterChatServer(&context, request, &reply);
    if (status.ok()) {
        std::cout << "[StatusGrpcClient.cpp] RegisterChatServer [RegisterChatServer] 注册成功，server_id: "
            << self_server_id_ << "\n";
        return reply;
    }

    std::cerr << "[StatusGrpcClient.cpp] RegisterChatServer [RegisterChatServer] gRPC 调用失败，server_id: "
        << self_server_id_ << "，错误: " << status.error_message() << "\n";
    reply.set_error(ErrorCodes::RPC_Failed);
    return reply;
}

HeartbeatRsp StatusGrpcClient::Heartbeat()
{
    HeartbeatRsp reply;
    auto stub = pool_->getConnection();
    if (stub == nullptr) {
        std::cerr << "[StatusGrpcClient.cpp] Heartbeat [Heartbeat] 获取 gRPC 连接失败\n";
        reply.set_error(ErrorCodes::RPC_Failed);
        return reply;
    }

    Defer defer([&stub, this]() { pool_->returnConnection(std::move(stub)); });

    message::HeartbeatReq request;
    request.set_server_id(self_server_id_);
    request.set_host(self_host_);
    request.set_port(self_port_);
    request.set_grpc_port(self_grpc_port_);
    request.set_timestamp(static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()));
    ClientContext context;

    Status status = stub->Heartbeat(&context, request, &reply);
    if (status.ok()) {
        return reply;
    }

    std::cerr << "[StatusGrpcClient.cpp] Heartbeat [Heartbeat] gRPC 调用失败，server_id: "
        << self_server_id_ << "，错误: " << status.error_message() << "\n";
    reply.set_error(ErrorCodes::RPC_Failed);
    return reply;
}

ReportUserOnlineRsp StatusGrpcClient::ReportUserOnline(int uid, const std::string& token)
{
    ReportUserOnlineRsp reply;
    auto stub = pool_->getConnection();
    if (stub == nullptr) {
        std::cerr << "[StatusGrpcClient.cpp] ReportUserOnline [ReportUserOnline] 获取 gRPC 连接失败，uid: " << uid << "\n";
        reply.set_error(ErrorCodes::RPC_Failed);
        return reply;
    }

    Defer defer([&stub, this]() { pool_->returnConnection(std::move(stub)); });

    message::ReportUserOnlineReq request;
    request.set_uid(uid);
    request.set_token(token);
    request.set_server_id(self_server_id_);
    request.set_host(self_host_);
    request.set_port(self_port_);
    ClientContext context;

    Status status = stub->ReportUserOnline(&context, request, &reply);
    if (status.ok()) {
        std::cout << "[StatusGrpcClient.cpp] ReportUserOnline [ReportUserOnline] uid: " << uid
            << " 上报在线成功，server_id: " << self_server_id_ << "\n";
        return reply;
    }

    std::cerr << "[StatusGrpcClient.cpp] ReportUserOnline [ReportUserOnline] gRPC 调用失败，uid: "
        << uid << "，错误: " << status.error_message() << "\n";
    reply.set_error(ErrorCodes::RPC_Failed);
    return reply;
}

ReportUserOfflineRsp StatusGrpcClient::ReportUserOffline(int uid)
{
    ReportUserOfflineRsp reply;
    auto stub = pool_->getConnection();
    if (stub == nullptr) {
        std::cerr << "[StatusGrpcClient.cpp] ReportUserOffline [ReportUserOffline] 获取 gRPC 连接失败，uid: " << uid << "\n";
        reply.set_error(ErrorCodes::RPC_Failed);
        return reply;
    }

    Defer defer([&stub, this]() { pool_->returnConnection(std::move(stub)); });

    message::ReportUserOfflineReq request;
    request.set_uid(uid);
    request.set_server_id(self_server_id_);
    ClientContext context;

    Status status = stub->ReportUserOffline(&context, request, &reply);
    if (status.ok()) {
        std::cout << "[StatusGrpcClient.cpp] ReportUserOffline [ReportUserOffline] uid: " << uid
            << " 上报下线成功，server_id: " << self_server_id_ << "\n";
        return reply;
    }

    std::cerr << "[StatusGrpcClient.cpp] ReportUserOffline [ReportUserOffline] gRPC 调用失败，uid: "
        << uid << "，错误: " << status.error_message() << "\n";
    reply.set_error(ErrorCodes::RPC_Failed);
    return reply;
}

QueryUserRouteRsp StatusGrpcClient::QueryUserRoute(int uid)
{
    QueryUserRouteRsp reply;
    auto stub = pool_->getConnection();
    if (stub == nullptr) {
        std::cerr << "[StatusGrpcClient.cpp] QueryUserRoute [QueryUserRoute] 获取 gRPC 连接失败，uid: " << uid << "\n";
        reply.set_error(ErrorCodes::RPC_Failed);
        return reply;
    }

    Defer defer([&stub, this]() { pool_->returnConnection(std::move(stub)); });

    message::QueryUserRouteReq request;
    request.set_uid(uid);
    ClientContext context;

    Status status = stub->QueryUserRoute(&context, request, &reply);
    if (status.ok()) {
        std::cout << "[StatusGrpcClient.cpp] QueryUserRoute [QueryUserRoute] uid: " << uid
            << " 查询完成，online: " << reply.online() << "\n";
        return reply;
    }

    std::cerr << "[StatusGrpcClient.cpp] QueryUserRoute [QueryUserRoute] gRPC 调用失败，uid: "
        << uid << "，错误: " << status.error_message() << "\n";
    reply.set_error(ErrorCodes::RPC_Failed);
    return reply;
}

void StatusGrpcClient::StartHeartbeat()
{
    bool expected = false;
    if (!heartbeat_started_.compare_exchange_strong(expected, true)) {
        return;
    }

    heartbeat_stop_.store(false);
    heartbeat_thread_ = std::thread(&StatusGrpcClient::HeartbeatLoop, this);
    std::cout << "[StatusGrpcClient.cpp] StartHeartbeat [StartHeartbeat] 心跳线程已启动\n";
}

void StatusGrpcClient::StopHeartbeat()
{
    heartbeat_stop_.store(true);
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    heartbeat_started_.store(false);
    std::cout << "[StatusGrpcClient.cpp] StopHeartbeat [StopHeartbeat] 心跳线程已停止\n";
}

void StatusGrpcClient::HeartbeatLoop()
{
    while (!heartbeat_stop_.load()) {
        auto rsp = Heartbeat();
        if (rsp.error() != ErrorCodes::Success) {
            std::cerr << "[StatusGrpcClient.cpp] HeartbeatLoop [HeartbeatLoop] 心跳失败，server_id: "
                << self_server_id_ << "，error: " << rsp.error() << "\n";
        }

        for (int i = 0; i < static_cast<int>(kHeartbeatInterval.count()); ++i) {
            if (heartbeat_stop_.load()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::cout << "[StatusGrpcClient.cpp] HeartbeatLoop [HeartbeatLoop] 心跳线程退出\n";
}

const std::string& StatusGrpcClient::ServerId() const
{
    return self_server_id_;
}