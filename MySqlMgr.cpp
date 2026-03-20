// MySqlMgr.cpp

#include "MySqlMgr.h"

MySqlMgr::~MySqlMgr() 
{
}

bool MySqlMgr::EnsureFriendTables()
{
    return _dao.EnsureFriendTables();
}

int MySqlMgr::RegUser(const std::string& name, const std::string& email, const std::string& pwd)
{
    return _dao.RegUser(name, email, pwd);
}

int MySqlMgr::CheckEmail(const std::string& name, const std::string& email) {
    return _dao.CheckEmail(name, email);
}

int MySqlMgr::UpdatePwd(const std::string& name, const std::string& pwd) {
    return _dao.UpdatePwd(name, pwd);
}

int MySqlMgr::CheckLogin(const std::string& email, const std::string& pwd, UserInfo& userInfo) {
    return _dao.CheckLogin(email, pwd, userInfo);
}

std::shared_ptr<UserInfo> MySqlMgr::GetUser(int uid)
{
    return _dao.GetUser(uid);
}

std::vector<UserInfo> MySqlMgr::SearchUsers(const std::string& keyword, std::size_t limit)
{
    return _dao.SearchUsers(keyword, limit);
}

std::vector<FriendInfo> MySqlMgr::GetFriendList(int uid, const std::string& updated_after)
{
    return _dao.GetFriendList(uid, updated_after);
}

bool MySqlMgr::AreFriends(int uid, int peer_uid)
{
    return _dao.AreFriends(uid, peer_uid);
}

long long MySqlMgr::CreatePrivateMessage(int from_uid, int to_uid, const std::string& content_type, const std::string& content)
{
    return _dao.CreatePrivateMessage(from_uid, to_uid, content_type, content);
}

std::vector<PrivateMessageInfo> MySqlMgr::GetPrivateMessages(int uid, int peer_uid, std::size_t limit, long long after_msg_id)
{
    return _dao.GetPrivateMessages(uid, peer_uid, limit, after_msg_id);
}

int MySqlMgr::MarkPrivateMessagesRead(int uid, int peer_uid)
{
    return _dao.MarkPrivateMessagesRead(uid, peer_uid);
}

long long MySqlMgr::CreateFriendRequest(int from_uid, int to_uid, const std::string& remark)
{
    return _dao.CreateFriendRequest(from_uid, to_uid, remark);
}

std::vector<FriendRequestInfo> MySqlMgr::GetPendingFriendRequests(int to_uid, const std::string& updated_after)
{
    return _dao.GetPendingFriendRequests(to_uid, updated_after);
}

int MySqlMgr::HandleFriendRequest(long long request_id, int to_uid, bool accept)
{
    return _dao.HandleFriendRequest(request_id, to_uid, accept);
}

MySqlMgr::MySqlMgr() 
{
}
