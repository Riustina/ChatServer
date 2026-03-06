// AsioIOServicePool.cpp

#include "AsioIOServicePool.h"
#include <iostream>
using namespace std;

AsioIOServicePool::AsioIOServicePool(std::size_t size) :
    _ioServices(size),
    _works(size),
    _nextIOService(0) {
    // io_context池，多个银行窗口（比如2个窗口），每个窗口独立处理业务，避免顾客挤在一个窗口排队。
    for (std::size_t i = 0; i < size; ++i) {
        // 使用emplace创建Work对象，而不是错误的赋值方式
		_works[i] = std::make_unique<Work>(_ioServices[i].get_executor());
		// 让每个窗口都处于“营业中”状态
		// 这样可以避免io_context在没有工作时自动退出
		// 也可以使用boost::asio::executor_work_guard来实现
		// _works[i] = std::make_unique<Work>(_ioServices[i]);
    }
    // 窗口的“营业中”牌子（只要挂着，窗口就保持工作状态，即使暂时没顾客）。
    // 遍历多个ioservice，创建多个线程，每个线程内部启动ioservice
    // 在AsioIOServicePool.cpp中添加状态监控
    for (std::size_t i = 0; i < _ioServices.size(); ++i) {
        _threads.emplace_back([this, i]() {
            // std::cout << "IO Service 第 " << i << " 线程启动" << std::endl;
            try {
                _ioServices[i].run();
                // std::cout << "IO Service 第" << i << " 正常结束" << std::endl;
            }
            catch (std::exception& e) {
                std::cerr << "[AsioIOServicePool.cpp] 函数 [AsioIOServicePool()] IO Service 第" << i << " 线程错误： " << e.what() << std::endl;
            }
        });
    }
}

// AsioIOServicePool.cpp 中的析构函数
AsioIOServicePool::~AsioIOServicePool() {
    // std::cout << "AsioIOServicePool destruct beginning" << endl;
    try {
        Stop();
        // std::cout << "AsioIOServicePool destruct complete" << endl;
    }
    catch (const std::exception& e) {
        std::cerr << "[AsioIOServicePool.cpp] 函数 [~AsioIOServicePool()] Error during AsioIOServicePool destruction: " << e.what() << endl;
    }
}

// 叫号机按轮询分配窗口（顾客1去窗口A，顾客2去窗口B，顾客3又回到窗口A，公平分配）。
boost::asio::io_context& AsioIOServicePool::GetIOService() {
    // 使用 fetch_add 实现原子递增并取模
    std::size_t index = _nextIOService.fetch_add(1) % _ioServices.size();
    return _ioServices[index];
}

// Stop 方法优化
void AsioIOServicePool::Stop() {
    // 停止所有工作守卫
    for (auto& work : _works) {
        work.reset();
    }

    // 不要急着 stop，先等待线程自然结束
    for (auto& t : _threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // 线程全部 join 后，再安全地彻底停止 io_services
    for (auto& io_service : _ioServices) {
        io_service.stop();
    }
}