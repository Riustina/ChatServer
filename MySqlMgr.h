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
    int CheckLogin(const std::string& email, const std::string& pwd, UserInfo userInfo);
    std::shared_ptr<UserInfo> GetUser(int uid);
private:
    MySqlMgr();
    MySqlDao  _dao;
};

