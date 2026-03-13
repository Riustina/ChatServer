// CServer.cpp

#include "CServer.h"
#include "AsioIOServicePool.h"
#include "LogicSystem.h"
#include <iostream>
#include <vector>

CServer::CServer(boost::asio::io_context& io_context, short port)
    : _io_context(io_context)
    , _port(port)
    , _acceptor(io_context, tcp::endpoint(tcp::v4(), port))
    , _is_shutting_down(false)
{
    std::cout << "[CServer] 启动成功，监听端口: " << _port << "\n";
    StartAccept();
}

CServer::~CServer()
{
    std::cout << "[CServer] 析构，端口: " << _port << "\n";
    Shutdown();
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
            return;
        }
        std::cerr << "[CServer] HandleAccept 错误: " << error.message() << "\n";
    }

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
    std::shared_ptr<CSession> session;
    {
        std::unique_lock<std::shared_mutex> lock(_sessions_mutex);
        auto it = _sessions.find(uuid);
        if (it != _sessions.end()) {
            session = it->second;
            _sessions.erase(it);
        }
    }

    if (session) {
        std::cout << "[CServer] ClearSession 准备清理在线状态，uuid: " << uuid
            << " uid: " << session->GetUid() << "\n";
        LogicSystem::getInstance().OnSessionClosed(session);
    }

    std::cout << "[CServer] 移除会话，uuid: " << uuid << "\n";
}

void CServer::Shutdown()
{
    bool expected = false;
    if (!_is_shutting_down.compare_exchange_strong(expected, true)) {
        return;
    }

    std::cout << "[CServer] 开始执行优雅关闭，端口: " << _port << "\n";

    // acceptor 关闭后，所有 pending 的 async_accept 会以 error 结束，不会再 StartAccept
    boost::system::error_code ec;
    _acceptor.close(ec);
    if (ec) {
        std::cerr << "[CServer] 关闭 acceptor 时出错: " << ec.message() << "\n";
    }

    std::vector<std::shared_ptr<CSession>> sessions;
    {
        std::unique_lock<std::shared_mutex> lock(_sessions_mutex);
        sessions.reserve(_sessions.size());
        for (auto& item : _sessions) {
            sessions.push_back(item.second);
        }
        _sessions.clear();
    }

    for (auto& session : sessions) {
        if (!session) {
            continue;
        }

        std::cout << "[CServer] Shutdown 清理会话，uuid: " << session->GetUuid()
            << " uid: " << session->GetUid() << "\n";
        LogicSystem::getInstance().OnSessionClosed(session);
        session->Close();
    }

    std::cout << "[CServer] 优雅关闭完成，已清理会话数: " << sessions.size() << "\n";
}