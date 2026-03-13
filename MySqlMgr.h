// MySqlMgr.h

#pragma once
#include "MysqlDao.h"
#include "Singleton.h"

class MySqlMgr : public Singleton<MySqlMgr>
{
    friend class Singleton<MySqlMgr>;
public:
    ~MySqlMgr();
    bool EnsureFriendTables();
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
    int CheckEmail(const std::string& name, const std::string& email);
    int UpdatePwd(const std::string& name, const std::string& pwd);
    int CheckLogin(const std::string& email, const std::string& pwd, UserInfo& userInfo);
    std::shared_ptr<UserInfo> GetUser(int uid);
    std::vector<UserInfo> SearchUsers(const std::string& keyword, std::size_t limit = 20);
    long long CreateFriendRequest(int from_uid, int to_uid, const std::string& remark);
    std::vector<FriendRequestInfo> GetPendingFriendRequests(int to_uid);
    int HandleFriendRequest(long long request_id, int to_uid, bool accept);
private:
    MySqlMgr();
    MySqlDao  _dao;
};
