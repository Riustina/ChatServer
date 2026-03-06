// LogicSystem.h

#pragma once
#include "Singleton.h"
#include "CSession.h"
#include "global.h"
#include "data.h"
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>

// 消息处理回调类型
using FunCallBack = std::function<
    void(std::shared_ptr<CSession>, const short msg_id, const std::string& msg_data)>;

class LogicSystem : public Singleton<LogicSystem>
{
    friend class Singleton<LogicSystem>;
public:
    ~LogicSystem();
    void PostMsgToQue(std::shared_ptr<LogicNode> msg);

private:
    LogicSystem();
    void DealMsg();
    void RegisterCallBacks();

    // ── 消息处理回调 ──────────────────────────────
    void LoginHandler(std::shared_ptr<CSession> session,
        const short msg_id,
        const std::string& msg_data);

    // ── 工作线程 ──────────────────────────────────
    std::thread             _worker_thread;
    std::queue<std::shared_ptr<LogicNode>> _msg_que;
    std::mutex              _mutex;
    std::condition_variable _consume;
    std::atomic<bool>       _b_stop;

    // _fun_callbacks 在构造时注册，之后只读，无需加锁
    // 若未来需要运行时动态注册，需在此加 std::shared_mutex
    std::unordered_map<short, FunCallBack> _fun_callbacks;

    // ── 用户信息内存缓存 ──────────────────────────
    std::shared_mutex _users_mutex;
    std::unordered_map<int, std::shared_ptr<UserInfo>> _users;
};
