// ChatServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
#include <iostream>
#include <thread>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <grpcpp/grpcpp.h>
#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ConfigManager.h"
#include "LogicSystem.h"
#include "StatusGrpcClient.h"
#include "message.grpc.pb.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::string BuildChatGrpcAddress(const std::string& host, const std::string& grpc_port)
{
    return host + ":" + grpc_port;
}

class ChatGrpcServiceImpl final : public message::ChatService::Service {
public:
    grpc::Status PushFriendRequests(grpc::ServerContext* context,
        const message::PushFriendRequestsReq* request,
        message::PushFriendRequestsRsp* response) override
    {
        (void)context;
        const bool delivered = LogicSystem::getInstance().PushFriendRequestsToLocalUser(request->uid());
        response->set_error(ErrorCodes::Success);
        response->set_delivered(delivered);
        return grpc::Status::OK;
    }

    grpc::Status PushFriendList(grpc::ServerContext* context,
        const message::PushFriendListReq* request,
        message::PushFriendListRsp* response) override
    {
        (void)context;
        const bool delivered = LogicSystem::getInstance().PushFriendListToLocalUser(request->uid());
        response->set_error(ErrorCodes::Success);
        response->set_delivered(delivered);
        return grpc::Status::OK;
    }

    grpc::Status PushPrivateMessage(grpc::ServerContext* context,
        const message::PushPrivateMessageReq* request,
        message::PushPrivateMessageRsp* response) override
    {
        (void)context;
        PrivateMessageInfo message;
        message.msg_id = request->msg_id();
        message.from_uid = request->from_uid();
        message.from_name = request->from_name();
        message.to_uid = request->to_uid();
        message.to_name = request->to_name();
        message.content_type = request->content_type();
        message.content = request->content();
        message.created_at = request->created_at();
        const bool delivered = LogicSystem::getInstance().PushPrivateMessageToLocalUser(message);
        response->set_error(ErrorCodes::Success);
        response->set_delivered(delivered);
        return grpc::Status::OK;
    }
};
}

int main(int argc, char* argv[])
{
    try {
        if (argc > 1) {
            boost::filesystem::path config_path = boost::filesystem::path(argv[1]);
            if (config_path.is_relative()) {
                config_path = boost::filesystem::current_path() / config_path;
            }
            ConfigManager::SetConfigPath(config_path.string());
            std::cout << "[main] 使用命令行指定配置文件: " << config_path.string() << "\n";
        }

        auto& cfg = ConfigManager::getInstance();
        auto& pool = AsioIOServicePool::getInstance();
        auto& status_client = StatusGrpcClient::getInstance();

        boost::asio::io_context io_context;
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);

        const std::string port_str = cfg["SelfServer"]["Port"];
        if (port_str.empty()) {
            throw std::runtime_error("[main] 配置文件中 SelfServer.Port 为空");
        }
        const std::string grpc_port_str = cfg["SelfServer"]["GrpcPort"];
        if (grpc_port_str.empty()) {
            throw std::runtime_error("[main] 配置文件中 SelfServer.GrpcPort 为空");
        }
        const std::string host_str = cfg["SelfServer"]["Host"].empty() ? "0.0.0.0" : cfg["SelfServer"]["Host"];

        const int port = std::stoi(port_str); // 比 atoi 更安全，失败时抛异常
        CServer server(io_context, static_cast<short>(port));

        ChatGrpcServiceImpl chat_grpc_service;
        grpc::ServerBuilder grpc_builder;
        const std::string grpc_address = BuildChatGrpcAddress(host_str, grpc_port_str);
        grpc_builder.AddListeningPort(grpc_address, grpc::InsecureServerCredentials());
        grpc_builder.RegisterService(&chat_grpc_service);
        auto grpc_server = grpc_builder.BuildAndStart();
        if (!grpc_server) {
            throw std::runtime_error("[main] ChatServer gRPC 服务启动失败");
        }
        std::thread grpc_thread([&grpc_server]() {
            grpc_server->Wait();
        });

        auto register_rsp = status_client.RegisterChatServer();
        if (register_rsp.error() != ErrorCodes::Success) {
            std::cerr << "[main] ChatServer 注册到 StatusServer 失败，error: "
                << register_rsp.error() << "，后续将依赖心跳自动恢复\n";
        }
        status_client.StartHeartbeat();

        signals.async_wait([&io_context, &pool, &server, &status_client, &grpc_server](
            const boost::system::error_code& ec, int signo)
            {
                if (ec) return; // 信号等待本身出错，不处理
                std::cout << "[main] 收到退出信号，signo: " << signo << "，正在关闭...\n";
                grpc_server->Shutdown();
                status_client.StopHeartbeat();
                server.Shutdown();
                io_context.stop();
                pool.Stop();
            });

        std::cout << "[main] ChatServer 启动，TCP端口: " << port
            << "，gRPC地址: " << grpc_address << "\n";
        io_context.run();
        grpc_server->Shutdown();
        if (grpc_thread.joinable()) {
            grpc_thread.join();
        }
        std::cout << "[main] io_context 已退出，程序结束\n";
    }
    catch (const std::exception& e) {
        std::cerr << "[main] 致命错误: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
