#include "LogicSystem.h"
#include "StatusGrpcClient.h"
#include "MySqlMgr.h"
#include "global.h"
#include "message.grpc.pb.h"
#include <iostream>
#include "Defer.h"

// ──────────────────────────────────────────────────────────────
// 构造 / 析构
// ──────────────────────────────────────────────────────────────

namespace {
std::string BuildChatGrpcAddress(const std::string& host, const std::string& grpc_port)
{
    return host + ":" + grpc_port;
}
}

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
    }
    _consume.notify_one();
}

// ──────────────────────────────────────────────────────────────
// OnSessionClosed：连接断开后的清理逻辑
// 包括本机在线表删除，以及向 StatusServer 上报下线
// ──────────────────────────────────────────────────────────────

void LogicSystem::OnSessionClosed(std::shared_ptr<CSession> session)
{
    if (!session) {
        return;
    }

    const int uid = session->GetUid();
    if (uid <= 0) {
        return;
    }

    std::cout << "[LogicSystem] OnSessionClosed uid: " << uid << "\n";
    RemoveUserSession(uid);
    auto rsp = StatusGrpcClient::getInstance().ReportUserOffline(uid);
    if (rsp.error() != ErrorCodes::Success) {
        std::cerr << "[LogicSystem] OnSessionClosed 上报下线失败，uid: " << uid
            << "，error: " << rsp.error() << "\n";
        return;
    }

    std::cout << "[LogicSystem] OnSessionClosed 上报下线成功，uid: " << uid << "\n";
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
            _consume.wait(lock, [this] {
                return !_msg_que.empty() || _b_stop.load();
                });

            if (_b_stop.load() && _msg_que.empty()) {
                break;
            }

            msg_node = std::move(_msg_que.front());
            _msg_que.pop();
        }

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
    _fun_callbacks[MSG_SEARCH_USER_REQ] = std::bind(
        &LogicSystem::SearchUserHandler, this,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3);
    _fun_callbacks[MSG_ADD_FRIEND_REQ] = std::bind(
        &LogicSystem::AddFriendHandler, this,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3);
    _fun_callbacks[MSG_GET_FRIEND_REQUESTS_REQ] = std::bind(
        &LogicSystem::GetFriendRequestsHandler, this,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3);
    _fun_callbacks[MSG_HANDLE_FRIEND_REQUEST_REQ] = std::bind(
        &LogicSystem::HandleFriendRequestHandler, this,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3);
}

// ──────────────────────────────────────────────────────────────
// LoginHandler：处理客户端登录到 ChatServer 的请求
// 流程：校验 token -> 查用户信息 -> 上报在线 -> 本机绑定 uid/session -> 回包
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

    const int uid = root["uid"].asInt();
    const std::string token = root["token"].asString();
    std::cout << "[LogicSystem] LoginHandler uid: " << uid << "\n";

    Json::Value rtvalue;

    // 2. 先向 StatusServer 校验 token
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

    // 5. 向 StatusServer 上报在线路由
    auto onlineRsp = StatusGrpcClient::getInstance().ReportUserOnline(uid, token);
    if (onlineRsp.error() != ErrorCodes::Success) {
        std::cerr << "[LogicSystem] LoginHandler 上报在线失败，uid: " << uid
            << "，error: " << onlineRsp.error() << "\n";
        rtvalue["error"] = onlineRsp.error();
        return;
    }

    // 6. 本机绑定 uid 和 session
    session->SetUid(uid);
    BindUserSession(uid, session);

    // 7. 组装成功回包
    std::cout << "[LogicSystem] LoginHandler 登录成功，uid: " << uid << "\n";
    rtvalue["error"] = ErrorCodes::Success;
    rtvalue["uid"] = uid;
    rtvalue["token"] = rsp.token();
    rtvalue["name"] = user_info->name;
    session->Send(rtvalue.toStyledString(), MSG_CHAT_LOGIN_RSP);
    PushFriendRequestsToUser(uid);
}

void LogicSystem::BindUserSession(int uid, std::shared_ptr<CSession> session)
{
    std::unique_lock<std::shared_mutex> lock(_online_users_mutex);
    _online_users[uid] = session;
    std::cout << "[LogicSystem] BindUserSession 记录本机在线用户，uid: " << uid << "\n";
}

void LogicSystem::RemoveUserSession(int uid)
{
    std::unique_lock<std::shared_mutex> lock(_online_users_mutex);
    _online_users.erase(uid);
    std::cout << "[LogicSystem] RemoveUserSession 移除本机在线用户，uid: " << uid << "\n";
}

Json::Value LogicSystem::BuildFriendRequestsPayload(int uid)
{
    Json::Value reply;
    if (uid <= 0) {
        reply["error"] = ErrorCodes::UidInvalid;
        return reply;
    }

    const auto requests = MySqlMgr::getInstance().GetPendingFriendRequests(uid);
    reply["error"] = ErrorCodes::Success;
    Json::Value items(Json::arrayValue);
    for (const auto& item : requests) {
        Json::Value node;
        node["request_id"] = Json::Int64(item.request_id);
        node["from_uid"] = item.from_uid;
        node["from_name"] = item.from_name;
        node["to_uid"] = item.to_uid;
        node["to_name"] = item.to_name;
        node["remark"] = item.remark;
        node["status"] = item.status;
        node["created_at"] = item.created_at;
        node["handled_at"] = item.handled_at;
        items.append(node);
    }
    reply["requests"] = items;
    return reply;
}

bool LogicSystem::PushFriendRequestsToLocalUser(int uid)
{
    if (uid <= 0) {
        return false;
    }

    std::shared_ptr<CSession> session;
    {
        std::shared_lock<std::shared_mutex> lock(_online_users_mutex);
        auto it = _online_users.find(uid);
        if (it == _online_users.end()) {
            return false;
        }
        session = it->second.lock();
    }

    if (!session) {
        return false;
    }

    const Json::Value payload = BuildFriendRequestsPayload(uid);
    session->Send(payload.toStyledString(), MSG_FRIEND_REQUESTS_PUSH);
    return true;
}

void LogicSystem::PushFriendRequestsToUser(int uid)
{
    if (uid <= 0) {
        return;
    }

    if (PushFriendRequestsToLocalUser(uid)) {
        return;
    }

    const auto routeRsp = StatusGrpcClient::getInstance().QueryUserRoute(uid);
    if (routeRsp.error() != ErrorCodes::Success || !routeRsp.online()) {
        return;
    }

    if (routeRsp.server_id() == StatusGrpcClient::getInstance().ServerId()) {
        PushFriendRequestsToLocalUser(uid);
        return;
    }

    const std::string address = BuildChatGrpcAddress(routeRsp.host(), routeRsp.grpc_port());
    auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    auto stub = message::ChatService::NewStub(channel);
    message::PushFriendRequestsReq request;
    message::PushFriendRequestsRsp response;
    request.set_uid(uid);
    grpc::ClientContext context;
    grpc::Status status = stub->PushFriendRequests(&context, request, &response);
    if (!status.ok() || response.error() != ErrorCodes::Success) {
        std::cerr << "[LogicSystem] PushFriendRequestsToUser 跨服推送失败，uid: " << uid
            << "，server_id: " << routeRsp.server_id()
            << "，error: " << (status.ok() ? response.error() : ErrorCodes::RPC_Failed) << "\n";
    }
}

void LogicSystem::SearchUserHandler(std::shared_ptr<CSession> session,
    const short msg_id,
    const std::string& msg_data)
{
    Json::Value root;
    Json::Reader reader;
    Json::Value reply;
    Defer defer([&reply, &session]() {
        session->Send(reply.toStyledString(), MSG_SEARCH_USER_RSP);
        });

    if (!reader.parse(msg_data, root)) {
        reply["error"] = ErrorCodes::Error_Json;
        return;
    }

    const int uid = session ? session->GetUid() : 0;
    if (uid <= 0) {
        reply["error"] = ErrorCodes::UidInvalid;
        return;
    }

    const std::string keyword = root["keyword"].asString();
    const int limit = root.isMember("limit") ? root["limit"].asInt() : 20;
    const auto users = MySqlMgr::getInstance().SearchUsers(keyword, static_cast<std::size_t>(std::max(1, limit)));

    reply["error"] = ErrorCodes::Success;
    Json::Value items(Json::arrayValue);
    for (const auto& user : users) {
        Json::Value item;
        item["uid"] = user.uid;
        item["name"] = user.name;
        item["email"] = user.email;
        items.append(item);
    }
    reply["users"] = items;
}

void LogicSystem::AddFriendHandler(std::shared_ptr<CSession> session,
    const short msg_id,
    const std::string& msg_data)
{
    Json::Value root;
    Json::Reader reader;
    Json::Value reply;
    Defer defer([&reply, &session]() {
        session->Send(reply.toStyledString(), MSG_ADD_FRIEND_RSP);
        });

    if (!reader.parse(msg_data, root)) {
        reply["error"] = ErrorCodes::Error_Json;
        return;
    }

    const int from_uid = session ? session->GetUid() : 0;
    const int to_uid = root["to_uid"].asInt();
    const std::string remark = root.isMember("remark") ? root["remark"].asString() : "";
    if (from_uid <= 0 || to_uid <= 0) {
        reply["error"] = ErrorCodes::UidInvalid;
        return;
    }

      const auto existingRequests = MySqlMgr::getInstance().GetPendingFriendRequests(from_uid);
      for (const auto &item : existingRequests) {
          const bool sameTarget = (item.from_uid == from_uid && item.to_uid == to_uid)
              || (item.from_uid == to_uid && item.to_uid == from_uid);
        if (!sameTarget) {
            continue;
        }

          if (item.status == "pending" || item.status == "accepted") {
              reply["error"] = ErrorCodes::MySQLFailed;
              reply["db_result"] = (item.status == "accepted") ? -3 : -4;
              reply["message"] = (item.status == "accepted")
                  ? "你们已经是好友了"
                  : "已存在待处理的好友申请";
              return;
          }
      }

      const long long request_id = MySqlMgr::getInstance().CreateFriendRequest(from_uid, to_uid, remark);
      if (request_id <= 0) {
          reply["error"] = ErrorCodes::MySQLFailed;
          reply["db_result"] = Json::Int64(request_id);
          switch (request_id) {
          case -1:
          case -2:
              reply["message"] = "好友申请参数无效";
              break;
          case -3:
              reply["message"] = "你们已经是好友了";
              break;
          case -4:
              reply["message"] = "已存在待处理的好友申请";
              break;
          default:
              reply["message"] = "好友申请发送失败，请稍后再试";
              break;
          }
          return;
      }

      reply["error"] = ErrorCodes::Success;
      reply["request_id"] = Json::Int64(request_id);
      reply["to_uid"] = to_uid;
      reply["message"] = "好友申请发送成功";
      std::cout << "[LogicSystem] AddFriendHandler 用户 " << from_uid
          << " 向用户 " << to_uid << " 发送了好友申请，request_id: " << request_id << "\n";
      PushFriendRequestsToUser(from_uid);
      PushFriendRequestsToUser(to_uid);
}

void LogicSystem::GetFriendRequestsHandler(std::shared_ptr<CSession> session,
    const short msg_id,
    const std::string& msg_data)
{
    (void)msg_id;
    (void)msg_data;

    Json::Value reply;
    Defer defer([&reply, &session]() {
        session->Send(reply.toStyledString(), MSG_GET_FRIEND_REQUESTS_RSP);
        });

    const int to_uid = session ? session->GetUid() : 0;
    if (to_uid <= 0) {
        reply["error"] = ErrorCodes::UidInvalid;
        return;
    }

    reply = BuildFriendRequestsPayload(to_uid);
}

void LogicSystem::HandleFriendRequestHandler(std::shared_ptr<CSession> session,
    const short msg_id,
    const std::string& msg_data)
{
    Json::Value root;
    Json::Reader reader;
    Json::Value reply;
    Defer defer([&reply, &session]() {
        session->Send(reply.toStyledString(), MSG_HANDLE_FRIEND_REQUEST_RSP);
        });

    if (!reader.parse(msg_data, root)) {
        reply["error"] = ErrorCodes::Error_Json;
        return;
    }

      const int to_uid = session ? session->GetUid() : 0;
      if (to_uid <= 0) {
          reply["error"] = ErrorCodes::UidInvalid;
          return;
      }

      const long long request_id = root["request_id"].asInt64();
      const bool accept = root["accept"].asBool();
      int from_uid = 0;
      const auto requests = MySqlMgr::getInstance().GetPendingFriendRequests(to_uid);
      for (const auto &item : requests) {
          if (item.request_id == request_id) {
              from_uid = item.from_uid;
              break;
          }
      }

      const int result = MySqlMgr::getInstance().HandleFriendRequest(request_id, to_uid, accept);
      if (result != 0) {
          reply["error"] = ErrorCodes::MySQLFailed;
          reply["db_result"] = result;
          return;
    }

      reply["error"] = ErrorCodes::Success;
      reply["request_id"] = Json::Int64(request_id);
      reply["accept"] = accept;
      std::cout << "[LogicSystem] HandleFriendRequestHandler 用户 " << to_uid
          << (accept ? " 同意了 " : " 拒绝了 ")
          << "用户 " << from_uid << " 的好友申请，request_id: " << request_id << "\n";
      PushFriendRequestsToUser(to_uid);
      PushFriendRequestsToUser(from_uid);
}
