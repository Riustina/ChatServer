// CSession.h

#pragma once
#include <boost/asio.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include "global.h"
#include "MsgNode.h"

using boost::asio::ip::tcp;

class CServer;
class LogicSystem;

class CSession : public std::enable_shared_from_this<CSession>
{
public:
    CSession(boost::asio::io_context& io_context, CServer* server);
    ~CSession();

    tcp::socket& GetSocket();
    const std::string& GetUuid() const;  // const 引用，防止外部修改

    void Start();
    void Send(const std::string& msg, short msgid);  // const 引用，避免拷贝
    void Send(const char* msg, short max_length, short msgid);
    void Close();

    std::shared_ptr<CSession> SharedSelf();

private:
    // 读流程：Start → AsyncReadHead → AsyncReadBody → AsyncReadHead → ...
    void AsyncReadHead(int total_len);
    void AsyncReadBody(int total_len);

    // 底层读取：保证读满 maxLength 字节后才回调
    void asyncReadFull(
        std::size_t maxLength,
        std::function<void(const boost::system::error_code&, std::size_t)> handler);

    void asyncReadLen(
        std::size_t read_len,
        std::size_t total_len,
        std::function<void(const boost::system::error_code&, std::size_t)> handler);

    void HandleWrite(const boost::system::error_code& error,
        std::shared_ptr<CSession> shared_self);

    // ── 网络 ─────────────────────────────────────
    tcp::socket _socket;
    CServer* _server;
    std::string _uuid;

    // ── 接收 ─────────────────────────────────────
    // _data 只在 asyncReadLen 的回调链中被写入（单条链，无并发写），
    // 通过 _data_mutex 保护与 AsyncReadHead/Body 之间的 memcpy 竞争
    char        _data[MAX_LENGTH];
    std::mutex  _data_mutex;

    std::shared_ptr<MsgNode>  _recv_head_node;  // 固定大小，复用
    std::shared_ptr<RecvNode> _recv_msg_node;   // 每条消息重新分配
    std::mutex  _recv_mutex;                    // 保护 _recv_head_node / _recv_msg_node 的替换

    // ── 发送 ─────────────────────────────────────
    std::queue<std::shared_ptr<SendNode>> _send_que;
    std::mutex  _send_lock;

    // ── 状态 ─────────────────────────────────────
    std::atomic<bool> _b_close;
};

// ─────────────────────────────────────────────────────────────
// LogicNode：投递给 LogicSystem 的工作单元
// ─────────────────────────────────────────────────────────────
class LogicNode {
    friend class LogicSystem;
public:
    LogicNode(std::shared_ptr<CSession> session,
        std::shared_ptr<RecvNode> recvnode);
private:
    std::shared_ptr<CSession> _session;
    std::shared_ptr<RecvNode> _recvnode;
};