// MySqlDao.h

#pragma once

#include <memory>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <queue>
#include <string>
#include <vector>
#include <jdbc/mysql_driver.h>
#include <jdbc/cppconn/connection.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/cppconn/exception.h>
#include "data.h"

class SqlConnection
{
public:
    SqlConnection(sql::Connection* con, int64_t lasttime);
    std::unique_ptr<sql::Connection> _con;
    int64_t _last_oper_time;
};

class MySqlPool
{
public:
    MySqlPool(
        const std::string& url,
        const std::string& user,
        const std::string& pass,
        const std::string& schema,
        int poolSize);
    void checkConnection();
    std::unique_ptr<SqlConnection> getConnection();
    void returnConnection(std::unique_ptr<SqlConnection>);
    void Close();
    ~MySqlPool();

private:
    std::string url_;
    std::string user_;
    std::string pass_;
    std::string schema_;
    int poolSize_;
    std::queue<std::unique_ptr<SqlConnection>> pool_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> b_stop_{ false };
    std::thread _check_thread;
};

class MySqlDao
{
public:
    MySqlDao();
    ~MySqlDao();
    bool EnsureFriendTables();
    int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
    int CheckEmail(const std::string& name, const std::string& email);
    int UpdatePwd(const std::string& name, const std::string& newpwd);
    int CheckLogin(const std::string& email, const std::string& pwd, UserInfo& userInfo);
    std::shared_ptr<UserInfo> GetUser(int uid);
    std::vector<UserInfo> SearchUsers(const std::string& keyword, std::size_t limit = 20);
    std::vector<FriendInfo> GetFriendList(int uid, const std::string& updated_after = "");
    bool AreFriends(int uid, int peer_uid);
    long long CreatePrivateMessage(int from_uid, int to_uid, const std::string& content_type, const std::string& content);
    std::vector<PrivateMessageInfo> GetPrivateMessages(int uid, int peer_uid, std::size_t limit = 50, long long after_msg_id = 0, long long before_msg_id = 0);
    int MarkPrivateMessagesRead(int uid, int peer_uid);
    long long CreateFriendRequest(int from_uid, int to_uid, const std::string& remark);
    std::vector<FriendRequestInfo> GetPendingFriendRequests(int to_uid, const std::string& updated_after = "");
    int HandleFriendRequest(long long request_id, int to_uid, bool accept);

private:
    std::unique_ptr<MySqlPool> pool_;
};
