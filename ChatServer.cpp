// ChatServer.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
#include <iostream>
#include <boost/asio.hpp>
#include "AsioIOServicePool.h"
#include "CServer.h"
#include "ConfigManager.h"
#include "LogicSystem.h"

#ifdef _WIN32
#include <windows.h>
#endif

int main()
{
    try {
        auto& cfg = ConfigManager::getInstance();
        auto& pool = AsioIOServicePool::getInstance();

        boost::asio::io_context io_context;
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);

        signals.async_wait([&io_context, &pool](
            const boost::system::error_code& ec, int /*signo*/)
            {
                if (ec) return; // 信号等待本身出错，不处理
                std::cout << "[main] 收到退出信号，正在关闭...\n";
                io_context.stop();
                pool.Stop();
            });

        const std::string port_str = cfg["SelfServer"]["Port"];
        if (port_str.empty()) {
            throw std::runtime_error("[main] 配置文件中 SelfServer.Port 为空");
        }

        const int port = std::stoi(port_str); // 比 atoi 更安全，失败时抛异常
        CServer server(io_context, static_cast<short>(port));

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