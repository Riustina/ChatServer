// CServer.h

#pragma once
#include <boost/asio.hpp>
#include "CSession.h"
#include <memory>
#include <unordered_map>  // O(1) 查找，替换 map 的 O(log n)
#include <shared_mutex>

using boost::asio::ip::tcp;

class CServer
{
public:
    CServer(boost::asio::io_context& io_context, short port);
    ~CServer();

    void ClearSession(const std::string& uuid); // const 引用，避免不必要的拷贝

private:
    void HandleAccept(std::shared_ptr<CSession> session,
        const boost::system::error_code& error);
    void StartAccept();

    boost::asio::io_context& _io_context;
    short                    _port;
    tcp::acceptor            _acceptor;

    std::unordered_map<std::string, std::shared_ptr<CSession>> _sessions;
    std::shared_mutex _sessions_mutex; // 读多写少时比 mutex 更高效
};