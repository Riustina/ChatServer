// CServer.cpp

#include "CServer.h"
#include "AsioIOServicePool.h"
#include <iostream>

CServer::CServer(boost::asio::io_context& io_context, short port)
    : _io_context(io_context)
    , _port(port)
    , _acceptor(io_context, tcp::endpoint(tcp::v4(), port))
{
    std::cout << "[CServer] 启动成功，监听端口: " << _port << "\n";
    StartAccept();
}

CServer::~CServer()
{
    std::cout << "[CServer] 析构，端口: " << _port << "\n";

    // acceptor 关闭后，所有 pending 的 async_accept 会以 error 结束，不会再 StartAccept
    boost::system::error_code ec;
    _acceptor.close(ec);
    if (ec) {
        std::cerr << "[CServer] 关闭 acceptor 时出错: " << ec.message() << "\n";
    }

    std::unique_lock<std::shared_mutex> lock(_sessions_mutex);
    _sessions.clear();
}

void CServer::HandleAccept(std::shared_ptr<CSession> new_session,
    const boost::system::error_code& error)
{
    if (!error) {
        new_session->Start();
        {
            std::unique_lock<std::shared_mutex> lock(_sessions_mutex);
            _sessions.emplace(new_session->GetUuid(), new_session);
        }
        std::cout << "[CServer] 新连接接入，uuid: " << new_session->GetUuid() << "\n";
    }
    else {
        // acceptor 被主动关闭时会触发 operation_aborted，属于正常关闭流程，不打印错误
        if (error == boost::asio::error::operation_aborted) {
            std::cout << "[CServer] acceptor 已关闭，停止接受新连接\n";
            return; // 不再 StartAccept
        }
        std::cerr << "[CServer] HandleAccept 错误: " << error.message() << "\n";
    }

    // 只有 acceptor 仍然开着才继续投递下一次 accept
    if (_acceptor.is_open()) {
        StartAccept();
    }
}

void CServer::StartAccept()
{
    auto& io_context = AsioIOServicePool::getInstance().GetIOService();
    auto  new_session = std::make_shared<CSession>(io_context, this);

    _acceptor.async_accept(
        new_session->GetSocket(),
        std::bind(&CServer::HandleAccept, this, new_session, std::placeholders::_1));
}

void CServer::ClearSession(const std::string& uuid)
{
    std::unique_lock<std::shared_mutex> lock(_sessions_mutex);
    _sessions.erase(uuid);
    std::cout << "[CServer] 移除会话，uuid: " << uuid << "\n";
}