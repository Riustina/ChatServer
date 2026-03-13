// CSession.cpp

#include "CSession.h"
#include "CServer.h"
#include "LogicSystem.h"
#include <iostream>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "global.h"

// ──────────────────────────────────────────────────────────────
// 构造 / 析构
// ──────────────────────────────────────────────────────────────

CSession::CSession(boost::asio::io_context& io_context, CServer* server)
    : _socket(io_context)
    , _server(server)
    , _b_close(false)
    , _uid(0)
{
    boost::uuids::uuid a_uuid = boost::uuids::random_generator()();
    _uuid = boost::uuids::to_string(a_uuid);
    _recv_head_node = std::make_shared<MsgNode>(HEAD_TOTAL_LEN);
    std::cout << "[CSession] 创建，uuid: " << _uuid << "\n";
}

CSession::~CSession()
{
    std::cout << "[CSession] 析构，uuid: " << _uuid << "\n";
}

tcp::socket& CSession::GetSocket()
{
    return _socket;
}

const std::string& CSession::GetUuid() const
{
    return _uuid;
}

std::shared_ptr<CSession> CSession::SharedSelf()
{
    return shared_from_this();
}

void CSession::SetUid(int uid)
{
    _uid.store(uid);
}

int CSession::GetUid() const
{
    return _uid.load();
}

// ──────────────────────────────────────────────────────────────
// 关闭：使用 atomic CAS 保证只关闭一次
// ──────────────────────────────────────────────────────────────

void CSession::Close()
{
    bool expected = false;
    if (_b_close.compare_exchange_strong(expected, true)) {
        boost::system::error_code ec;
        _socket.shutdown(tcp::socket::shutdown_both, ec);
        _socket.close(ec);
        std::cout << "[CSession] 连接关闭，uuid: " << _uuid << " uid: " << _uid.load() << "\n";
    }
}

void CSession::Start()
{
    AsyncReadHead(HEAD_TOTAL_LEN);
}

// ──────────────────────────────────────────────────────────────
// 发送（两个重载）
// 队列为空时立即投递，否则 HandleWrite 回调中会自动续投
// ──────────────────────────────────────────────────────────────

void CSession::Send(const std::string& msg, short msgid)
{
    Send(msg.c_str(), static_cast<short>(msg.length()), msgid);
}

void CSession::Send(const char* msg, short max_length, short msgid)
{
    std::lock_guard<std::mutex> lock(_send_lock);
    int send_que_size = static_cast<int>(_send_que.size());

    if (send_que_size >= MAX_SENDQUE) {
        std::cout << "[CSession] Send 队列已满，uuid: " << _uuid << "\n";
        return;
    }

    _send_que.push(std::make_shared<SendNode>(msg, max_length, msgid));
    if (send_que_size > 0) {
        return;
    }

    auto& msgnode = _send_que.front();
    boost::asio::async_write(
        _socket,
        boost::asio::buffer(msgnode->_data, msgnode->_total_len),
        std::bind(&CSession::HandleWrite, this,
            std::placeholders::_1, SharedSelf()));
}

void CSession::HandleWrite(const boost::system::error_code& error,
    std::shared_ptr<CSession> shared_self)
{
    try {
        if (error) {
            std::cerr << "[CSession] HandleWrite 错误: " << error.message()
                << "，uuid: " << _uuid << "\n";
            Close();
            _server->ClearSession(_uuid);
            return;
        }

        std::lock_guard<std::mutex> lock(_send_lock);
        _send_que.pop();

        if (!_send_que.empty()) {
            auto& msgnode = _send_que.front();
            boost::asio::async_write(
                _socket,
                boost::asio::buffer(msgnode->_data, msgnode->_total_len),
                std::bind(&CSession::HandleWrite, this,
                    std::placeholders::_1, shared_self));
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[CSession] HandleWrite 异常: " << e.what() << "\n";
    }
}

void CSession::AsyncReadHead(int total_len)
{
    (void)total_len;
    auto self = shared_from_this();

    asyncReadFull(HEAD_TOTAL_LEN,
        [self, this](const boost::system::error_code& ec, std::size_t bytes_transferred)
        {
            try {
                if (ec) {
                    std::cerr << "[CSession] AsyncReadHead 读取错误: " << ec.message()
                        << "，uuid: " << _uuid << "\n";
                    Close();
                    _server->ClearSession(_uuid);
                    return;
                }

                if (bytes_transferred < HEAD_TOTAL_LEN) {
                    std::cerr << "[CSession] AsyncReadHead 长度不足，期望: " << HEAD_TOTAL_LEN
                        << "，实际: " << bytes_transferred << "\n";
                    Close();
                    _server->ClearSession(_uuid);
                    return;
                }

                {
                    std::lock_guard<std::mutex> recv_lock(_recv_mutex);
                    std::lock_guard<std::mutex> data_lock(_data_mutex);
                    _recv_head_node->Clear();
                    memcpy(_recv_head_node->_data, _data, bytes_transferred);
                }

                short msg_id = 0;
                memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
                msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
                std::cout << "[CSession] AsyncReadHead msg_id: " << msg_id << "\n";
                if (msg_id <= 0) {
                    std::cerr << "[CSession] AsyncReadHead 非法 msg_id: " << msg_id << "\n";
                    Close();
                    _server->ClearSession(_uuid);
                    return;
                }

                short msg_len = 0;
                memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
                msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);
                std::cout << "[CSession] AsyncReadHead msg_len: " << msg_len << "\n";
                if (msg_len <= 0 || msg_len > MAX_LENGTH) {
                    std::cerr << "[CSession] AsyncReadHead 非法 msg_len: " << msg_len << "\n";
                    Close();
                    _server->ClearSession(_uuid);
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(_recv_mutex);
                    _recv_msg_node = std::make_shared<RecvNode>(msg_len, msg_id);
                }

                AsyncReadBody(msg_len);
            }
            catch (const std::exception& e) {
                std::cerr << "[CSession] AsyncReadHead 异常: " << e.what() << "\n";
            }
        });
}

void CSession::AsyncReadBody(int total_len)
{
    auto self = shared_from_this();

    asyncReadFull(total_len,
        [self, this, total_len](const boost::system::error_code& ec, std::size_t bytes_transferred)
        {
            try {
                if (ec) {
                    std::cerr << "[CSession] AsyncReadBody 读取错误: " << ec.message()
                        << "，uuid: " << _uuid << "\n";
                    Close();
                    _server->ClearSession(_uuid);
                    return;
                }

                if (static_cast<int>(bytes_transferred) < total_len) {
                    std::cerr << "[CSession] AsyncReadBody 长度不足，期望: " << total_len
                        << "，实际: " << bytes_transferred << "\n";
                    Close();
                    _server->ClearSession(_uuid);
                    return;
                }

                {
                    std::lock_guard<std::mutex> recv_lock(_recv_mutex);
                    std::lock_guard<std::mutex> data_lock(_data_mutex);
                    memcpy(_recv_msg_node->_data, _data, bytes_transferred);
                    _recv_msg_node->_cur_len += static_cast<int>(bytes_transferred);
                    _recv_msg_node->_data[total_len] = '\0';
                }

                std::cout << "[CSession] AsyncReadBody 收到消息体，uuid: " << _uuid << "\n";
                LogicSystem::getInstance().PostMsgToQue(
                    std::make_shared<LogicNode>(shared_from_this(), _recv_msg_node));
                AsyncReadHead(HEAD_TOTAL_LEN);
            }
            catch (const std::exception& e) {
                std::cerr << "[CSession] AsyncReadBody 异常: " << e.what() << "\n";
            }
        });
}

void CSession::asyncReadFull(
    std::size_t maxLength,
    std::function<void(const boost::system::error_code&, std::size_t)> handler)
{
    {
        std::lock_guard<std::mutex> lock(_data_mutex);
        ::memset(_data, 0, MAX_LENGTH);
    }
    asyncReadLen(0, maxLength, handler);
}

void CSession::asyncReadLen(
    std::size_t read_len,
    std::size_t total_len,
    std::function<void(const boost::system::error_code&, std::size_t)> handler)
{
    auto self = shared_from_this();
    _socket.async_read_some(
        boost::asio::buffer(_data + read_len, total_len - read_len),
        [read_len, total_len, handler, self, this](
            const boost::system::error_code& ec,
            std::size_t bytes_transferred)
        {
            if (ec) {
                handler(ec, read_len + bytes_transferred);
                return;
            }

            if (read_len + bytes_transferred >= total_len) {
                handler(ec, read_len + bytes_transferred);
                return;
            }

            asyncReadLen(read_len + bytes_transferred, total_len, handler);
        });
}

LogicNode::LogicNode(std::shared_ptr<CSession> session,
    std::shared_ptr<RecvNode> recvnode)
    : _session(session)
    , _recvnode(recvnode)
{
}
