#include "LogicSystem.h"
#include "StatusGrpcClient.h"
#include "MySqlMgr.h"
#include "global.h"
#include <iostream>
#include "Defer.h"

// ──────────────────────────────────────────────────────────────
// 构造 / 析构
// ──────────────────────────────────────────────────────────────

LogicSystem::LogicSystem() : _b_stop(false)
{
    RegisterCallBacks();
    _worker_thread = std::thread(&LogicSystem::DealMsg, this);
}

LogicSystem::~LogicSystem()
{
    // 通知工作线程停服，等待它把队列里剩余的消息处理完再退出
    _b_stop = true;
    _consume.notify_one();
    _worker_thread.join();
}

// ──────────────────────────────────────────────────────────────
// PostMsgToQue：入队并通知工作线程
// 先解锁再 notify，避免工作线程被唤醒后还要等当前线程释放锁
// ──────────────────────────────────────────────────────────────

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> msg)
{
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _msg_que.push(std::move(msg));
    } // 锁在这里释放
    _consume.notify_one();
}

// ──────────────────────────────────────────────────────────────
// DealMsg：工作线程主循环
// 停服语义：_b_stop 为 true 时把队列里剩余消息处理完再退出
// ──────────────────────────────────────────────────────────────

void LogicSystem::DealMsg()
{
    for (;;) {
        std::shared_ptr<LogicNode> msg_node;

        {
            std::unique_lock<std::mutex> lock(_mutex);

            // 等待条件：队列非空 或 收到停服信号
            _consume.wait(lock, [this] {
                return !_msg_que.empty() || _b_stop.load();
                });

            // 停服且队列已清空：安全退出
            if (_b_stop.load() && _msg_que.empty()) {
                break;
            }

            msg_node = std::move(_msg_que.front());
            _msg_que.pop();
        } // 锁释放，后续处理不占锁

        if (!msg_node) continue;

        const short msg_id = msg_node->_recvnode->_msg_id;
        std::cout << "[LogicSystem] DealMsg 处理消息，msg_id: " << msg_id << "\n";

        auto it = _fun_callbacks.find(msg_id);
        if (it == _fun_callbacks.end()) {
            std::cerr << "[LogicSystem] DealMsg 未找到 msg_id [" << msg_id << "] 的处理函数\n";
            continue;
        }

        it->second(msg_node->_session, msg_id,
            std::string(msg_node->_recvnode->_data,
                msg_node->_recvnode->_cur_len));
    }

    std::cout << "[LogicSystem] DealMsg 工作线程退出\n";
}

// ──────────────────────────────────────────────────────────────
// RegisterCallBacks：注册所有消息处理回调
// ──────────────────────────────────────────────────────────────

void LogicSystem::RegisterCallBacks()
{
    _fun_callbacks[MSG_CHAT_LOGIN] = std::bind(
        &LogicSystem::LoginHandler, this,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3);
}

// ──────────────────────────────────────────────────────────────
// LoginHandler：处理客户端登录到 ChatServer 的请求
// 流程：校验 token（gRPC → StatusServer）→ 查用户信息（内存缓存 / MySQL）→ 回包
// ──────────────────────────────────────────────────────────────

void LogicSystem::LoginHandler(std::shared_ptr<CSession> session,
    const short msg_id,
    const std::string& msg_data)
{
    Json::Value root;
    Json::Reader reader;

    // 1. 解析 JSON，失败直接回错误包
    if (!reader.parse(msg_data, root)) {
        std::cerr << "[LogicSystem] LoginHandler JSON 解析失败\n";
        Json::Value err;
        err["error"] = ErrorCodes::Error_Json;
        session->Send(err.toStyledString(), msg_id);
        return;
    }

    const int    uid = root["uid"].asInt();
    const std::string token = root["token"].asString();
    std::cout << "[LogicSystem] LoginHandler uid: " << uid << "\n";

    Json::Value rtvalue;

    // Defer 保证任何 return 路径都会把 rtvalue 发回客户端
    // 注意：rtvalue 是栈变量，Defer 析构在同一作用域内，生命周期安全
    Defer defer([&rtvalue, &session, msg_id]() {
        session->Send(rtvalue.toStyledString(), msg_id);
        });

    // 2. 向 StatusServer 校验 token
    // 注意：StatusGrpcClient::Login 是待实现的新接口，
    // 与 GetChatServer 并列，用于验证客户端持有的 token 是否合法
    auto rsp = StatusGrpcClient::getInstance().Login(uid, token);
    rtvalue["error"] = rsp.error();
    if (rsp.error() != ErrorCodes::Success) {
        std::cerr << "[LogicSystem] LoginHandler token 校验失败，uid: " << uid
            << "，error: " << rsp.error() << "\n";
        return;
    }

    // 3. 查内存缓存
    std::shared_ptr<UserInfo> user_info;
    {
        std::shared_lock<std::shared_mutex> lock(_users_mutex);
        auto it = _users.find(uid);
        if (it != _users.end()) {
            user_info = it->second;
        }
    }

    // 4. 缓存未命中，查数据库
    if (!user_info) {
        user_info = MySqlMgr::getInstance().GetUser(uid);
        if (!user_info) {
            std::cerr << "[LogicSystem] LoginHandler 用户不存在，uid: " << uid << "\n";
            rtvalue["error"] = ErrorCodes::UidInvalid;
            return;
        }

        std::unique_lock<std::shared_mutex> lock(_users_mutex);
        _users[uid] = user_info;
    }

    // 5. 组装成功回包
    std::cout << "[LogicSystem] LoginHandler 登录成功，uid: " << uid << "\n";
    rtvalue["uid"] = uid;
    rtvalue["token"] = rsp.token();
    rtvalue["email"] = user_info->email;
}