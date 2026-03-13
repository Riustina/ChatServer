// ChatServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
#include <iostream>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ConfigManager.h"
#include "LogicSystem.h"
#include "StatusGrpcClient.h"

#ifdef _WIN32
#include <windows.h>
#endif

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

        const int port = std::stoi(port_str); // 比 atoi 更安全，失败时抛异常
        CServer server(io_context, static_cast<short>(port));

        auto register_rsp = status_client.RegisterChatServer();
        if (register_rsp.error() != ErrorCodes::Success) {
            std::cerr << "[main] ChatServer 注册到 StatusServer 失败，error: "
                << register_rsp.error() << "，后续将依赖心跳自动恢复\n";
        }
        status_client.StartHeartbeat();

        signals.async_wait([&io_context, &pool, &server, &status_client](
            const boost::system::error_code& ec, int signo)
            {
                if (ec) return; // 信号等待本身出错，不处理
                std::cout << "[main] 收到退出信号，signo: " << signo << "，正在关闭...\n";
                status_client.StopHeartbeat();
                server.Shutdown();
                io_context.stop();
                pool.Stop();
            });

        std::cout << "[main] ChatServer 启动，端口: " << port << "\n";
        io_context.run();
        std::cout << "[main] io_context 已退出，程序结束\n";
    }
    catch (const std::exception& e) {
        std::cerr << "[main] 致命错误: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}