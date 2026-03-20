// MySqlDao.cpp

#include "MySqlDao.h"
#include <iostream>
#include <chrono>
// Defer头文件实现一个简单的RAII类，用于在作用域结束时自动调用指定的函数
#include "Defer.h"
#include "ConfigManager.h"

SqlConnection::SqlConnection(sql::Connection* con, int64_t lasttime)
    :_con(con), _last_oper_time(lasttime)
{
}

MySqlPool::MySqlPool(const std::string& url, const std::string& user, const std::string& pass, const std::string& schema, int poolSize)
    :url_(url), user_(user), pass_(pass), schema_(schema), poolSize_(poolSize), b_stop_(false)
{
    try {
        for (int i = 0; i < poolSize_; ++i) {
            // Connector C++ 使用单例模式来创建驱动实例，所以需要用指针
            sql::mysql::MySQL_Driver* driver = sql::mysql::get_driver_instance();
            sql::Connection* con = driver->connect(url_, user_, pass_);
            con->setSchema(schema_);
            auto CurrentTime = std::chrono::system_clock::now().time_since_epoch();
            long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(CurrentTime).count();
            // 记录连接和最后操作时间
            pool_.push(std::make_unique<SqlConnection>(con, timestamp));
        }
        _check_thread = std::thread([this]() {
            while (!b_stop_) {
                checkConnection();
                std::this_thread::sleep_for(std::chrono::seconds(60)); // 每60秒检查一次
            }
            });
        _check_thread.detach(); // 分离线程，交给系统管理
        std::cout << "[MySqlDao.cpp] 函数 [MySqlPool()] 现已连接至 " << url << " / " << schema << std::endl;
    }
    catch (sql::SQLException& e) {
        std::cerr << "[MySqlDao.cpp] 函数 [MySqlPool()] Error: " << e.what() << std::endl;
        // 清理已创建的连接
        Close();
        throw; // 重新抛出异常，让调用者知道初始化失败
    }
    catch (std::exception& e) {
        std::cerr << "[MySqlDao.cpp] 函数 [MySqlPool()] Error: " << e.what() << std::endl;
        Close();
        throw;
    }
    catch (...) {
        std::cerr << "[MySqlDao.cpp] 函数 [MySqlPool()] Unknown error" << std::endl;
        Close();
        throw;
    }
}

void MySqlPool::checkConnection()
{
    std::lock_guard<std::mutex> guard(mutex_);

    // 创建临时队列存储连接
    std::queue<std::unique_ptr<SqlConnection>> tempPool;
    auto currentTime = std::chrono::system_clock::now().time_since_epoch();
    long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();

    // 检查所有连接
    while (!pool_.empty()) {
        auto con = std::move(pool_.front());
        pool_.pop();

        // 如果60秒内有操作过，跳过检查
        if (con->_last_oper_time + 60 > timestamp) {
            tempPool.push(std::move(con));
            continue;
        }

        try {
            std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT 1"));
            pstmt->execute();
            con->_last_oper_time = timestamp; // 更新最后操作时间
            // std::cout << "[MySqlDao.cpp] 函数 [checkConnection()] MySQL链接存活，操作时间为：" << timestamp << std::endl;
            tempPool.push(std::move(con));
        }
        catch (sql::SQLException& e) {
            std::cerr << "[MySqlDao.cpp] 函数 [checkConnection()] MySQL链接失效，重新创建连接，错误为：" << e.what() << std::endl;
            try {
                sql::mysql::MySQL_Driver* driver = sql::mysql::get_driver_instance();
                sql::Connection* newCon = driver->connect(url_, user_, pass_);
                newCon->setSchema(schema_);
                con.reset(new SqlConnection(newCon, timestamp)); // 创建新的连接对象
                tempPool.push(std::move(con));
            }
            catch (std::exception& ex) {
                std::cerr << "[MySqlDao.cpp] 函数 [checkConnection()] 重新创建连接失败: " << ex.what() << std::endl;
                // 连接无法恢复，池大小减少
            }
        }
        catch (std::exception& e) {
            std::cerr << "[MySqlDao.cpp] 函数 [checkConnection()] Error: " << e.what() << std::endl;
            // 尝试保留连接，避免池耗尽
            tempPool.push(std::move(con));
        }
        catch (...) {
            std::cerr << "[MySqlDao.cpp] 函数 [checkConnection()] Unknown error" << std::endl;
            // 尝试保留连接，避免池耗尽
            tempPool.push(std::move(con));
        }
    }

    // 将临时池中的连接放回主池
    pool_.swap(tempPool);

    // 如果池大小减少，尝试补充
    while (pool_.size() < poolSize_) {
        try {
            sql::mysql::MySQL_Driver* driver = sql::mysql::get_driver_instance();
            sql::Connection* newCon = driver->connect(url_, user_, pass_);
            newCon->setSchema(schema_);
            auto CurrentTime = std::chrono::system_clock::now().time_since_epoch();
            long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(CurrentTime).count();
            pool_.push(std::make_unique<SqlConnection>(newCon, timestamp));
            std::cerr << "[MySqlDao.cpp] 函数 [checkConnection()] 补充新的MySQL连接到池中" << std::endl;
        }
        catch (std::exception& e) {
            std::cerr << "[MySqlDao.cpp] 函数 [checkConnection()] 补充连接失败: " << e.what() << std::endl;
            break; // 不能创建新连接，停止尝试
        }
    }
}

std::unique_ptr<SqlConnection> MySqlPool::getConnection()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (b_stop_) {
        return nullptr; // 如果池已经停止，返回空指针
    }

    // 等待可用连接
    bool success = cond_.wait_for(lock, std::chrono::seconds(30), [this] {
        return b_stop_ || !pool_.empty();
        });

    if (!success || b_stop_ || pool_.empty()) {
        // 等待超时或池已停止
        return nullptr;
    }

    std::unique_ptr<SqlConnection> con = std::move(pool_.front());
    pool_.pop();

    // 更新最后操作时间
    auto currentTime = std::chrono::system_clock::now().time_since_epoch();
    long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();
    con->_last_oper_time = timestamp;

    return con;
}

void MySqlPool::returnConnection(std::unique_ptr<SqlConnection> con)
{
    if (!con) {
        return; // 忽略空连接
    }

    std::unique_lock<std::mutex> lock(mutex_);
    if (b_stop_) {
        return; // 如果池子已经停止，不用返回连接
    }

    pool_.push(std::move(con));
    cond_.notify_one(); // 通知一个等待的线程
}

void MySqlPool::Close()
{
	{ // 作用域锁，确保线程安全
        std::unique_lock<std::mutex> lock(mutex_);
        if (b_stop_) return; // 防止重复关闭
        b_stop_ = true; // 设置停止标志
    }

    cond_.notify_all(); // 通知所有等待的线程

    // 清理所有连接
    std::unique_lock<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
        pool_.pop();
    }
}

MySqlPool::~MySqlPool()
{
    Close(); // 确保资源被正确释放
}

/// MySqlDao函数
/// //////////////////////////////////////////////////////////////
/// MySqlDao函数

MySqlDao::MySqlDao()
{
    auto& config = ConfigManager::getInstance();
    const auto& host = config["MySQL"]["Host"];
    const auto& port = config["MySQL"]["Port"];
    const auto& user = config["MySQL"]["User"];
    const auto& pwd = config["MySQL"]["Passwd"];
    const auto& schema = config["MySQL"]["Schema"];
    const auto& poolSize = std::stoi(config["MySQL"]["PoolSize"]);
	pool_.reset(new MySqlPool(host + ":" + port, user, pwd, schema, poolSize));
    EnsureFriendTables();
}

bool MySqlDao::EnsureFriendTables()
{
    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MySqlDao.cpp] 函数 [EnsureFriendTables()] 无法获取数据库连接" << std::endl;
        return false;
    }

    try
    {
        std::unique_ptr<sql::Statement> stmt(con->_con->createStatement());
        stmt->execute(
            "CREATE TABLE IF NOT EXISTS friend_relation ("
            "id BIGINT PRIMARY KEY AUTO_INCREMENT,"
            "user_id INT NOT NULL,"
            "friend_id INT NOT NULL,"
            "status VARCHAR(16) NOT NULL DEFAULT 'accepted',"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
            "UNIQUE KEY uniq_user_friend (user_id, friend_id),"
            "KEY idx_friend_id (friend_id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");

        stmt->execute(
            "CREATE TABLE IF NOT EXISTS friend_request ("
            "request_id BIGINT PRIMARY KEY AUTO_INCREMENT,"
            "from_uid INT NOT NULL,"
            "to_uid INT NOT NULL,"
            "remark VARCHAR(255) NOT NULL DEFAULT '',"
            "status VARCHAR(16) NOT NULL DEFAULT 'pending',"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "handled_at TIMESTAMP NULL DEFAULT NULL,"
            "KEY idx_to_uid_status (to_uid, status),"
            "KEY idx_from_uid_status (from_uid, status),"
            "KEY idx_from_to_created_at (from_uid, to_uid, created_at)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");

        stmt->execute(
            "CREATE TABLE IF NOT EXISTS private_message ("
            "msg_id BIGINT PRIMARY KEY AUTO_INCREMENT,"
            "from_uid INT NOT NULL,"
            "to_uid INT NOT NULL,"
            "content_type VARCHAR(16) NOT NULL DEFAULT 'text',"
            "content TEXT NOT NULL,"
            "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "read_at TIMESTAMP NULL DEFAULT NULL,"
            "KEY idx_pair_time (from_uid, to_uid, created_at),"
            "KEY idx_to_uid_time (to_uid, created_at),"
            "KEY idx_to_uid_read (to_uid, read_at)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");

        try {
            stmt->execute("ALTER TABLE friend_request DROP INDEX uniq_from_to");
            std::cout << "[MySqlDao.cpp] 函数 [EnsureFriendTables()] 已移除 friend_request 的旧唯一索引 uniq_from_to" << std::endl;
        }
        catch (sql::SQLException&) {
        }

        try {
            stmt->execute("ALTER TABLE private_message ADD COLUMN read_at TIMESTAMP NULL DEFAULT NULL AFTER created_at");
            std::cout << "[MySqlDao.cpp] 函数 [EnsureFriendTables()] 已为 private_message 补充 read_at 字段" << std::endl;
        }
        catch (sql::SQLException&) {
        }

        try {
            stmt->execute("ALTER TABLE private_message ADD KEY idx_to_uid_read (to_uid, read_at)");
        }
        catch (sql::SQLException&) {
        }

        std::cout << "[MySqlDao.cpp] 函数 [EnsureFriendTables()] 业务相关表检查完成" << std::endl;
        pool_->returnConnection(std::move(con));
        return true;
    }
    catch (sql::SQLException& e)
    {
        std::cerr << "[MySqlDao.cpp] 函数 [EnsureFriendTables()] SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        pool_->returnConnection(std::move(con));
        return false;
    }
}

MySqlDao::~MySqlDao()
{
	pool_->Close();
}

int MySqlDao::RegUser(const std::string& name, const std::string& email, const std::string& pwd)
{
    auto con = pool_->getConnection();
    if (con == nullptr) {
        pool_->returnConnection(std::move(con));
        return -2;
    } // 获取连接失败，返回特定错误码

    try
    {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("CALL reg_user(?,?,?,@result)"));
        pstmt->setString(1, name);
        pstmt->setString(2, email);
        pstmt->setString(3, pwd);
        pstmt->execute();

        std::unique_ptr<sql::Statement> stmtResult(con->_con->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmtResult->executeQuery("SELECT @result AS result"));

        int result = -3; // 默认错误码，表示未获取到结果
        if (res->next()) {
            result = res->getInt("result");
            std::cerr << "[MySqlDao.cpp] 函数 [RegUser()] 用户注册结果: " << result << std::endl;
        }

        // 无论是否获取到结果，都返回连接并返回结果
        pool_->returnConnection(std::move(con));
        return result;
    }
    catch (sql::SQLException& e)
    {
        // 先记录错误
        std::cerr << "[MySqlDao.cpp] 函数 [RegUser()] SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        // 确保连接被返回到池中
        try {
            if (con) {
                pool_->returnConnection(std::move(con));
            }
        }
        catch (...) {
            std::cerr << "[MySqlDao.cpp] 函数 [RegUser()] 返回连接到池中时发生异常" << std::endl;
        }
        return -1; // SQL异常错误码
    }
    catch (std::exception& e)
    {
        std::cerr << "[MySqlDao.cpp] 函数 [RegUser()] 标准异常: " << e.what() << std::endl;
        try {
            if (con) {
                pool_->returnConnection(std::move(con));
            }
        }
        catch (...) {
            std::cerr << "[MySqlDao.cpp] 函数 [RegUser()] 返回连接到池中时发生异常" << std::endl;
        }
        return -4; // 一般异常错误码
    }
    catch (...)
    {
        std::cerr << "[MySqlDao.cpp] 函数 [RegUser()] 未知异常" << std::endl;
        try {
            if (con) {
                pool_->returnConnection(std::move(con));
            }
        }
        catch (...) {
            std::cerr << "[MySqlDao.cpp] 函数 [RegUser()] 返回连接到池中时发生异常" << std::endl;
        }
        return -5; // 未知异常错误码
    }
}

int MySqlDao::CheckEmail(const std::string& name, const std::string& email)
{
    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MySqlDao.cpp] 函数 [checkEmail()] 无法获取数据库连接" << std::endl;
        return -2; // 数据库连接失败
    }

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT email FROM user WHERE name = ?"));
        pstmt->setString(1, name);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        // 如果没有结果，说明用户不存在
        if (!res->next()) {
            std::cout << "[MySqlDao.cpp] 函数 [checkEmail()] 用户 " << name << " 不存在" << std::endl;
            pool_->returnConnection(std::move(con));
            return -6; // 用户不存在
        }

        // 检查邮箱是否匹配
        std::string db_email = res->getString("email");
        std::cout << "[MySqlDao.cpp] 函数 [checkEmail()] 数据库邮箱: " << db_email << ", 请求邮箱: " << email << std::endl;
        if (email != db_email) {
            pool_->returnConnection(std::move(con));
            return -7; // 邮箱不匹配
        }

        // 匹配成功
        pool_->returnConnection(std::move(con));
        return 0; // 成功
    }
    catch (sql::SQLException& e)
    {
        // 记录错误
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;

        try {
            if (con) {
                pool_->returnConnection(std::move(con));
            }
        }
        catch (...) {
            std::cerr << "[MySqlDao.cpp] 函数 [checkEmail()] 返回连接到池中时发生异常" << std::endl;
        }
        return -1; // SQL异常
    }
    catch (std::exception& e)
    {
        std::cerr << "[MySqlDao.cpp] 函数 [checkEmail()] 标准异常: " << e.what() << std::endl;
        try {
            if (con) {
                pool_->returnConnection(std::move(con));
            }
        }
        catch (...) {
            std::cerr << "[MySqlDao.cpp] 函数 [checkEmail()] 返回连接到池中时发生异常" << std::endl;
        }
        return -4; // 一般异常
    }
    catch (...)
    {
        std::cerr << "[MySqlDao.cpp] 函数 [checkEmail()] 未知异常" << std::endl;
        try {
            if (con) {
                pool_->returnConnection(std::move(con));
            }
        }
        catch (...) {
            std::cerr << "[MySqlDao.cpp] 函数 [checkEmail()] 返回连接到池中时发生异常" << std::endl;
        }
        return -5; // 未知异常
    }
}

int MySqlDao::UpdatePwd(const std::string& name, const std::string& newpwd)
{
    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MySqlDao.cpp] 函数 [UpdatePwd()] 无法获取数据库连接" << std::endl;
        return -2; // 数据库连接失败
    }

    try {
        // 准备查询语句
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("UPDATE user SET pwd = ? WHERE name = ?"));
        // 绑定参数
        pstmt->setString(1, newpwd);
        pstmt->setString(2, name);

        // 执行更新
        int updateCount = pstmt->executeUpdate();
        std::cout << "[MySqlDao.cpp] 函数 [UpdatePwd()] Updated rows: " << updateCount << std::endl;

        // 检查是否有行被更新
        if (updateCount == 0) {
            std::cout << "[MySqlDao.cpp] 函数 [UpdatePwd()] 没有找到用户: " << name << std::endl;
            pool_->returnConnection(std::move(con));
            return -6; // 用户不存在或者没有行被更新
        }

        pool_->returnConnection(std::move(con));
        return 0; // 成功
    }
    catch (sql::SQLException& e)
    {
        // 记录错误
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;

        try {
            if (con) {
                pool_->returnConnection(std::move(con));
            }
        }
        catch (...) {
            std::cerr << "[MySqlDao.cpp] 函数 [UpdatePwd()] 返回连接到池中时发生异常" << std::endl;
        }
        return -1; // SQL异常
    }
    catch (std::exception& e)
    {
        std::cerr << "[MySqlDao.cpp] 函数 [UpdatePwd()] 标准异常: " << e.what() << std::endl;
        try {
            if (con) {
                pool_->returnConnection(std::move(con));
            }
        }
        catch (...) {
            std::cerr << "[MySqlDao.cpp] 函数 [UpdatePwd()] 返回连接到池中时发生异常" << std::endl;
        }
        return -4; // 一般异常
    }
    catch (...)
    {
        std::cerr << "[MySqlDao.cpp] 函数 [UpdatePwd()] 未知异常" << std::endl;
        try {
            if (con) {
                pool_->returnConnection(std::move(con));
            }
        }
        catch (...) {
            std::cerr << "[MySqlDao.cpp] 函数 [UpdatePwd()] 返回连接到池中时发生异常" << std::endl;
        }
        return -5; // 未知异常
    }
}

int MySqlDao::CheckLogin(const std::string& email, const std::string& pwd, UserInfo& userInfo)
{
    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MySqlDao.cpp] 函数 [CheckLogin()] 无法获取数据库连接" << std::endl;
        return -2; // 数据库连接失败
    }

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement("SELECT uid, name, email, pwd FROM user WHERE email = ?"));
        pstmt->setString(1, email);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        // 如果没有结果，说明用户不存在
        if (!res->next()) {
            std::cout << "[MySqlDao.cpp] 函数 [CheckLogin()] 用户 " << email << " 不存在" << std::endl;
            pool_->returnConnection(std::move(con));
            return -1; // 用户不存在
        }

        // 检查密码是否匹配
        std::string db_pwd = res->getString("pwd");
        std::cout << "[MySqlDao.cpp] 函数 [CheckLogin()] 数据库密码: " << db_pwd << ", 请求密码: " << pwd << std::endl;
        if (pwd != db_pwd) {
            pool_->returnConnection(std::move(con));
            return -3; // 密码不匹配
        }

        // 匹配成功
        std::cout << "[MySqlDao.cpp] 函数 [CheckLogin()] 密码匹配正确" << std::endl;
        userInfo.name = res->getString("name");
        userInfo.email = res->getString("email");
        userInfo.uid = res->getInt("uid");
        userInfo.pwd = db_pwd;
        pool_->returnConnection(std::move(con));
        return 0; // 成功
    }
    catch (sql::SQLException& e)
    {
        // 记录错误
        std::cerr << "[MySqlDao.cpp] 函数 [CheckLogin()] SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;

        try {
            if (con) {
                pool_->returnConnection(std::move(con));
            }
        }
        catch (...) {
            std::cerr << "[MySqlDao.cpp] 函数 [CheckLogin()] 返回连接到池中时发生异常" << std::endl;
        }
        return -6; // SQL异常
    }
    catch (std::exception& e)
    {
        std::cerr << "[MySqlDao.cpp] 函数 [CheckLogin()] 标准异常: " << e.what() << std::endl;
        try {
            if (con) {
                pool_->returnConnection(std::move(con));
            }
        }
        catch (...) {
            std::cerr << "[MySqlDao.cpp] 函数 [CheckLogin()] 返回连接到池中时发生异常" << std::endl;
        }
        return -4; // 一般异常
    }
    catch (...)
    {
        std::cerr << "[MySqlDao.cpp] 函数 [CheckLogin()] 未知异常" << std::endl;
        try {
            if (con) {
                pool_->returnConnection(std::move(con));
            }
        }
        catch (...) {
            std::cerr << "[MySqlDao.cpp] 函数 [CheckLogin()] 返回连接到池中时发生异常" << std::endl;
        }
        return -5; // 未知异常
    }
}

std::shared_ptr<UserInfo> MySqlDao::GetUser(int uid)
{
    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MysqlDao.cpp] GetUser [GetUser] 获取数据库连接失败，uid: " << uid << "\n";
        return nullptr;
    }

    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
        // 显式列出字段，避免 SELECT * 在表结构变化时出错
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->_con->prepareStatement(
                "SELECT uid, name, email, pwd FROM user WHERE uid = ?"));
        pstmt->setInt(1, uid);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        // 只取第一行，用 if 替代 while + break，语义更清晰
        if (res->next()) {
            auto user = std::make_shared<UserInfo>();
            user->uid = res->getInt("uid");
            user->name = res->getString("name");
            user->email = res->getString("email");
            user->pwd = res->getString("pwd");
            return user;
        }

        std::cout << "[MysqlDao.cpp] GetUser [GetUser] 用户不存在，uid: " << uid << "\n";
        return nullptr;
    }
    catch (const sql::SQLException& e) {
        std::cerr << "[MysqlDao.cpp] GetUser [GetUser] SQLException: " << e.what()
            << "，MySQL error code: " << e.getErrorCode()
            << "，SQLState: " << e.getSQLState() << "\n";
        return nullptr;
    }
}

std::vector<UserInfo> MySqlDao::SearchUsers(const std::string& keyword, std::size_t limit)
{
    std::vector<UserInfo> users;
    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MySqlDao.cpp] 函数 [SearchUsers()] 无法获取数据库连接" << std::endl;
        return users;
    }

    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
        bool all_digits = !keyword.empty();
        for (char ch : keyword) {
            if (ch < '0' || ch > '9') {
                all_digits = false;
                break;
            }
        }

        std::unique_ptr<sql::PreparedStatement> pstmt;
        if (all_digits) {
            pstmt.reset(con->_con->prepareStatement(
                "SELECT uid, name, email, pwd FROM user WHERE uid = ? OR name LIKE ? ORDER BY uid ASC LIMIT ?"));
            pstmt->setInt(1, std::stoi(keyword));
            pstmt->setString(2, "%" + keyword + "%");
            pstmt->setInt(3, static_cast<int>(limit));
        }
        else {
            pstmt.reset(con->_con->prepareStatement(
                "SELECT uid, name, email, pwd FROM user WHERE name LIKE ? ORDER BY uid ASC LIMIT ?"));
            pstmt->setString(1, "%" + keyword + "%");
            pstmt->setInt(2, static_cast<int>(limit));
        }

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        while (res->next()) {
            UserInfo user;
            user.uid = res->getInt("uid");
            user.name = res->getString("name");
            user.email = res->getString("email");
            user.pwd = res->getString("pwd");
            users.push_back(user);
        }

        std::cout << "[MySqlDao.cpp] 函数 [SearchUsers()] 搜索完成，keyword: " << keyword
            << "，结果数: " << users.size() << std::endl;
        return users;
    }
    catch (const sql::SQLException& e) {
        std::cerr << "[MySqlDao.cpp] 函数 [SearchUsers()] SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return users;
    }
}

std::vector<FriendInfo> MySqlDao::GetFriendList(int uid, const std::string& updated_after)
{
    std::vector<FriendInfo> friends;
    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MySqlDao.cpp] 函数 [GetFriendList()] 无法获取数据库连接" << std::endl;
        return friends;
    }

    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt;
        if (!updated_after.empty()) {
            pstmt.reset(con->_con->prepareStatement(
                "SELECT u.uid, u.name, u.email, fr.created_at, "
                "GREATEST(fr.updated_at, "
                "COALESCE((SELECT MAX(pm.created_at) FROM private_message pm "
                " WHERE ((pm.from_uid = fr.user_id AND pm.to_uid = fr.friend_id) "
                "    OR (pm.from_uid = fr.friend_id AND pm.to_uid = fr.user_id))), fr.updated_at), "
                "COALESCE((SELECT MAX(pm.read_at) FROM private_message pm "
                " WHERE pm.from_uid = fr.friend_id AND pm.to_uid = fr.user_id), fr.updated_at)) AS updated_at, "
                "COALESCE(( "
                "  SELECT CASE WHEN pm.content_type = 'image' THEN '[图片]' ELSE pm.content END "
                "  FROM private_message pm "
                "  WHERE ((pm.from_uid = fr.user_id AND pm.to_uid = fr.friend_id) "
                "     OR (pm.from_uid = fr.friend_id AND pm.to_uid = fr.user_id)) "
                "  ORDER BY pm.created_at DESC, pm.msg_id DESC LIMIT 1 "
                "), '') AS last_message, "
                "COALESCE(( "
                "  SELECT DATE_FORMAT(pm.created_at, '%H:%i') "
                "  FROM private_message pm "
                "  WHERE ((pm.from_uid = fr.user_id AND pm.to_uid = fr.friend_id) "
                "     OR (pm.from_uid = fr.friend_id AND pm.to_uid = fr.user_id)) "
                "  ORDER BY pm.created_at DESC, pm.msg_id DESC LIMIT 1 "
                "), '') AS last_time, "
                "COALESCE(( "
                "  SELECT COUNT(*) FROM private_message pm "
                "  WHERE pm.from_uid = fr.friend_id AND pm.to_uid = fr.user_id AND pm.read_at IS NULL "
                "), 0) AS unread_count "
                "FROM friend_relation fr "
                "INNER JOIN user u ON u.uid = fr.friend_id "
                "WHERE fr.user_id = ? AND fr.status = 'accepted' "
                "AND GREATEST(fr.updated_at, "
                "COALESCE((SELECT MAX(pm.created_at) FROM private_message pm "
                " WHERE ((pm.from_uid = fr.user_id AND pm.to_uid = fr.friend_id) "
                "    OR (pm.from_uid = fr.friend_id AND pm.to_uid = fr.user_id))), fr.updated_at), "
                "COALESCE((SELECT MAX(pm.read_at) FROM private_message pm "
                " WHERE pm.from_uid = fr.friend_id AND pm.to_uid = fr.user_id), fr.updated_at)) >= ? "
                "ORDER BY updated_at ASC, u.uid ASC"));
            pstmt->setInt(1, uid);
            pstmt->setString(2, updated_after);
        } else {
            pstmt.reset(con->_con->prepareStatement(
                "SELECT u.uid, u.name, u.email, fr.created_at, "
                "GREATEST(fr.updated_at, "
                "COALESCE((SELECT MAX(pm.created_at) FROM private_message pm "
                " WHERE ((pm.from_uid = fr.user_id AND pm.to_uid = fr.friend_id) "
                "    OR (pm.from_uid = fr.friend_id AND pm.to_uid = fr.user_id))), fr.updated_at), "
                "COALESCE((SELECT MAX(pm.read_at) FROM private_message pm "
                " WHERE pm.from_uid = fr.friend_id AND pm.to_uid = fr.user_id), fr.updated_at)) AS updated_at, "
                "COALESCE(( "
                "  SELECT CASE "
                "    WHEN pm.content_type = 'image' THEN '[图片]' "
                "    ELSE pm.content "
                "  END "
                "  FROM private_message pm "
                "  WHERE ((pm.from_uid = fr.user_id AND pm.to_uid = fr.friend_id) "
                "     OR (pm.from_uid = fr.friend_id AND pm.to_uid = fr.user_id)) "
                "  ORDER BY pm.created_at DESC, pm.msg_id DESC LIMIT 1 "
                "), '') AS last_message, "
                "COALESCE(( "
                "  SELECT DATE_FORMAT(pm.created_at, '%H:%i') "
                "  FROM private_message pm "
                "  WHERE ((pm.from_uid = fr.user_id AND pm.to_uid = fr.friend_id) "
                "     OR (pm.from_uid = fr.friend_id AND pm.to_uid = fr.user_id)) "
                "  ORDER BY pm.created_at DESC, pm.msg_id DESC LIMIT 1 "
                "), '') AS last_time, "
                "COALESCE(( "
                "  SELECT COUNT(*) "
                "  FROM private_message pm "
                "  WHERE pm.from_uid = fr.friend_id "
                "    AND pm.to_uid = fr.user_id "
                "    AND pm.read_at IS NULL "
                "), 0) AS unread_count "
                "FROM friend_relation fr "
                "INNER JOIN user u ON u.uid = fr.friend_id "
                "WHERE fr.user_id = ? AND fr.status = 'accepted' "
                "ORDER BY fr.created_at DESC, u.uid ASC"));
            pstmt->setInt(1, uid);
        }

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        while (res->next()) {
            FriendInfo item;
            item.uid = res->getInt("uid");
            item.name = res->getString("name");
            item.email = res->getString("email");
            item.created_at = res->getString("created_at");
            item.updated_at = res->getString("updated_at");
            item.last_message = res->getString("last_message");
            item.last_time = res->getString("last_time");
            item.unread_count = res->getInt("unread_count");
            friends.push_back(item);
        }

        std::cout << "[MySqlDao.cpp] 函数 [GetFriendList()] uid: " << uid
            << "，好友数: " << friends.size() << std::endl;
        return friends;
    }
    catch (const sql::SQLException& e) {
        std::cerr << "[MySqlDao.cpp] 函数 [GetFriendList()] SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return friends;
    }
}

bool MySqlDao::AreFriends(int uid, int peer_uid)
{
    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MySqlDao.cpp] 函数 [AreFriends()] 无法获取数据库连接" << std::endl;
        return false;
    }

    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->_con->prepareStatement(
                "SELECT 1 FROM friend_relation WHERE user_id = ? AND friend_id = ? AND status = 'accepted' LIMIT 1"));
        pstmt->setInt(1, uid);
        pstmt->setInt(2, peer_uid);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        return res->next();
    }
    catch (const sql::SQLException& e) {
        std::cerr << "[MySqlDao.cpp] 函数 [AreFriends()] SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

long long MySqlDao::CreatePrivateMessage(int from_uid, int to_uid, const std::string& content_type, const std::string& content)
{
    if (from_uid <= 0 || to_uid <= 0 || content.empty()) {
        return -1;
    }

    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MySqlDao.cpp] 函数 [CreatePrivateMessage()] 无法获取数据库连接" << std::endl;
        return -2;
    }

    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->_con->prepareStatement(
                "INSERT INTO private_message(from_uid, to_uid, content_type, content) VALUES(?, ?, ?, ?)"));
        pstmt->setInt(1, from_uid);
        pstmt->setInt(2, to_uid);
        pstmt->setString(3, content_type);
        pstmt->setString(4, content);
        pstmt->executeUpdate();

        std::unique_ptr<sql::Statement> stmt(con->_con->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT LAST_INSERT_ID() AS msg_id"));
        if (res->next()) {
            return res->getInt64("msg_id");
        }
        return -3;
    }
    catch (const sql::SQLException& e) {
        std::cerr << "[MySqlDao.cpp] 函数 [CreatePrivateMessage()] SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return -4;
    }
}

std::vector<PrivateMessageInfo> MySqlDao::GetPrivateMessages(int uid, int peer_uid, std::size_t limit, long long after_msg_id)
{
    std::vector<PrivateMessageInfo> messages;
    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MySqlDao.cpp] 函数 [GetPrivateMessages()] 无法获取数据库连接" << std::endl;
        return messages;
    }

    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt;
        if (after_msg_id > 0) {
            pstmt.reset(con->_con->prepareStatement(
                "SELECT pm.msg_id, pm.from_uid, fu.name AS from_name, pm.to_uid, tu.name AS to_name, "
                "pm.content_type, pm.content, pm.created_at "
                "FROM private_message pm "
                "JOIN user fu ON fu.uid = pm.from_uid "
                "JOIN user tu ON tu.uid = pm.to_uid "
                "WHERE ((pm.from_uid = ? AND pm.to_uid = ?) OR (pm.from_uid = ? AND pm.to_uid = ?)) "
                "AND pm.msg_id > ? "
                "ORDER BY pm.created_at ASC, pm.msg_id ASC "
                "LIMIT ?"));
            pstmt->setInt(1, uid);
            pstmt->setInt(2, peer_uid);
            pstmt->setInt(3, peer_uid);
            pstmt->setInt(4, uid);
            pstmt->setInt64(5, after_msg_id);
            pstmt->setInt(6, static_cast<int>(limit));
        } else {
            pstmt.reset(con->_con->prepareStatement(
                "SELECT t.msg_id, t.from_uid, fu.name AS from_name, t.to_uid, tu.name AS to_name, "
                "t.content_type, t.content, t.created_at "
                "FROM ("
                "  SELECT pm.msg_id, pm.from_uid, pm.to_uid, pm.content_type, pm.content, pm.created_at "
                "  FROM private_message pm "
                "  WHERE ((pm.from_uid = ? AND pm.to_uid = ?) OR (pm.from_uid = ? AND pm.to_uid = ?)) "
                "  ORDER BY pm.created_at DESC, pm.msg_id DESC "
                "  LIMIT ?"
                ") t "
                "JOIN user fu ON fu.uid = t.from_uid "
                "JOIN user tu ON tu.uid = t.to_uid "
                "ORDER BY t.created_at ASC, t.msg_id ASC"));
            pstmt->setInt(1, uid);
            pstmt->setInt(2, peer_uid);
            pstmt->setInt(3, peer_uid);
            pstmt->setInt(4, uid);
            pstmt->setInt(5, static_cast<int>(limit));
        }

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        while (res->next()) {
            PrivateMessageInfo item;
            item.msg_id = res->getInt64("msg_id");
            item.from_uid = res->getInt("from_uid");
            item.from_name = res->getString("from_name");
            item.to_uid = res->getInt("to_uid");
            item.to_name = res->getString("to_name");
            item.content_type = res->getString("content_type");
            item.content = res->getString("content");
            item.created_at = res->getString("created_at");
            messages.push_back(item);
        }
        return messages;
    }
    catch (const sql::SQLException& e) {
        std::cerr << "[MySqlDao.cpp] 函数 [GetPrivateMessages()] SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return messages;
    }
}

int MySqlDao::MarkPrivateMessagesRead(int uid, int peer_uid)
{
    if (uid <= 0 || peer_uid <= 0) {
        return -1;
    }

    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MySqlDao.cpp] 函数 [MarkPrivateMessagesRead()] 无法获取数据库连接" << std::endl;
        return -2;
    }

    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->_con->prepareStatement(
                "UPDATE private_message "
                "SET read_at = CURRENT_TIMESTAMP "
                "WHERE from_uid = ? AND to_uid = ? AND read_at IS NULL"));
        pstmt->setInt(1, peer_uid);
        pstmt->setInt(2, uid);
        return pstmt->executeUpdate();
    }
    catch (const sql::SQLException& e) {
        std::cerr << "[MySqlDao.cpp] 函数 [MarkPrivateMessagesRead()] SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return -3;
    }
}

long long MySqlDao::CreateFriendRequest(int from_uid, int to_uid, const std::string& remark)
{
    if (from_uid <= 0 || to_uid <= 0 || from_uid == to_uid) {
        return -1;
    }

    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MySqlDao.cpp] 函数 [CreateFriendRequest()] 无法获取数据库连接" << std::endl;
        return -2;
    }

    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
        std::unique_ptr<sql::PreparedStatement> relation_check_pstmt(
            con->_con->prepareStatement(
                "SELECT 1 FROM friend_relation WHERE user_id = ? AND friend_id = ? AND status = 'accepted' LIMIT 1"));
        relation_check_pstmt->setInt(1, from_uid);
        relation_check_pstmt->setInt(2, to_uid);
        std::unique_ptr<sql::ResultSet> relation_check_res(relation_check_pstmt->executeQuery());
        if (relation_check_res->next()) {
            std::cout << "[MySqlDao.cpp] 函数 [CreateFriendRequest()] 双方已经是好友，from_uid: "
                << from_uid << "，to_uid: " << to_uid << std::endl;
            return -3;
        }

        std::unique_ptr<sql::PreparedStatement> check_pstmt(
            con->_con->prepareStatement(
                "SELECT request_id FROM friend_request "
                "WHERE ((from_uid = ? AND to_uid = ?) OR (from_uid = ? AND to_uid = ?)) "
                "AND status = 'pending' LIMIT 1"));
        check_pstmt->setInt(1, from_uid);
        check_pstmt->setInt(2, to_uid);
        check_pstmt->setInt(3, to_uid);
        check_pstmt->setInt(4, from_uid);
        std::unique_ptr<sql::ResultSet> check_res(check_pstmt->executeQuery());
        if (check_res->next()) {
            std::cout << "[MySqlDao.cpp] 函数 [CreateFriendRequest()] 已存在待处理好友申请，from_uid: "
                << from_uid << "，to_uid: " << to_uid << std::endl;
            return -4;
        }

        std::unique_ptr<sql::PreparedStatement> pstmt(
            con->_con->prepareStatement(
                "INSERT INTO friend_request(from_uid, to_uid, remark, status) VALUES(?, ?, ?, 'pending')"));
        pstmt->setInt(1, from_uid);
        pstmt->setInt(2, to_uid);
        pstmt->setString(3, remark);
        pstmt->executeUpdate();

        std::unique_ptr<sql::Statement> stmt(con->_con->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT LAST_INSERT_ID() AS request_id"));
        if (res->next()) {
            return res->getInt64("request_id");
        }
        return -5;
    }
    catch (const sql::SQLException& e) {
        std::cerr << "[MySqlDao.cpp] 函数 [CreateFriendRequest()] SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return -6;
    }
}

std::vector<FriendRequestInfo> MySqlDao::GetPendingFriendRequests(int to_uid, const std::string& updated_after)
{
    std::vector<FriendRequestInfo> requests;
    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MySqlDao.cpp] 函数 [GetPendingFriendRequests()] 无法获取数据库连接" << std::endl;
        return requests;
    }

    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt;
        if (!updated_after.empty()) {
            pstmt.reset(con->_con->prepareStatement(
                "SELECT fr.request_id, fr.from_uid, fu.name AS from_name, fr.to_uid, tu.name AS to_name, "
                "fr.remark, fr.status, fr.created_at, fr.handled_at, "
                "COALESCE(fr.handled_at, fr.created_at) AS updated_at "
                "FROM friend_request fr "
                "JOIN user fu ON fu.uid = fr.from_uid "
                "JOIN user tu ON tu.uid = fr.to_uid "
                "WHERE (fr.to_uid = ? OR fr.from_uid = ?) "
                "AND COALESCE(fr.handled_at, fr.created_at) >= ? "
                "ORDER BY updated_at ASC, fr.request_id ASC"));
            pstmt->setInt(1, to_uid);
            pstmt->setInt(2, to_uid);
            pstmt->setString(3, updated_after);
        } else {
            pstmt.reset(con->_con->prepareStatement(
                "SELECT fr.request_id, fr.from_uid, fu.name AS from_name, fr.to_uid, tu.name AS to_name, "
                "fr.remark, fr.status, fr.created_at, fr.handled_at, "
                "COALESCE(fr.handled_at, fr.created_at) AS updated_at "
                "FROM friend_request fr "
                "JOIN user fu ON fu.uid = fr.from_uid "
                "JOIN user tu ON tu.uid = fr.to_uid "
                "WHERE fr.to_uid = ? OR fr.from_uid = ? "
                "ORDER BY fr.request_id DESC"));
            pstmt->setInt(1, to_uid);
            pstmt->setInt(2, to_uid);
        }
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while (res->next()) {
            FriendRequestInfo item;
            item.request_id = res->getInt64("request_id");
            item.from_uid = res->getInt("from_uid");
            item.from_name = res->getString("from_name");
            item.to_uid = res->getInt("to_uid");
            item.to_name = res->getString("to_name");
            item.remark = res->getString("remark");
            item.status = res->getString("status");
            item.created_at = res->getString("created_at");
            item.handled_at = res->isNull("handled_at") ? "" : res->getString("handled_at");
            item.updated_at = res->getString("updated_at");
            requests.push_back(item);
        }
        return requests;
    }
    catch (const sql::SQLException& e) {
        std::cerr << "[MySqlDao.cpp] 函数 [GetPendingFriendRequests()] SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return requests;
    }
}

int MySqlDao::HandleFriendRequest(long long request_id, int to_uid, bool accept)
{
    auto con = pool_->getConnection();
    if (con == nullptr) {
        std::cerr << "[MySqlDao.cpp] 函数 [HandleFriendRequest()] 无法获取数据库连接" << std::endl;
        return -1;
    }

    Defer defer([this, &con]() {
        pool_->returnConnection(std::move(con));
        });

    try {
        con->_con->setAutoCommit(false);

        std::unique_ptr<sql::PreparedStatement> select_pstmt(
            con->_con->prepareStatement(
                "SELECT from_uid, to_uid, status FROM friend_request WHERE request_id = ? AND to_uid = ? FOR UPDATE"));
        select_pstmt->setInt64(1, request_id);
        select_pstmt->setInt(2, to_uid);
        std::unique_ptr<sql::ResultSet> res(select_pstmt->executeQuery());
        if (!res->next()) {
            con->_con->rollback();
            con->_con->setAutoCommit(true);
            return -2;
        }

        const int from_uid = res->getInt("from_uid");
        const std::string status = res->getString("status");
        if (status != "pending") {
            con->_con->rollback();
            con->_con->setAutoCommit(true);
            return -3;
        }

        std::unique_ptr<sql::PreparedStatement> update_pstmt(
            con->_con->prepareStatement(
                "UPDATE friend_request SET status = ?, handled_at = CURRENT_TIMESTAMP WHERE request_id = ?"));
        update_pstmt->setString(1, accept ? "accepted" : "rejected");
        update_pstmt->setInt64(2, request_id);
        update_pstmt->executeUpdate();

        if (accept) {
            std::unique_ptr<sql::PreparedStatement> relation_pstmt(
                con->_con->prepareStatement(
                    "INSERT INTO friend_relation(user_id, friend_id, status) VALUES(?, ?, 'accepted') "
                    "ON DUPLICATE KEY UPDATE status = 'accepted', updated_at = CURRENT_TIMESTAMP"));
            relation_pstmt->setInt(1, from_uid);
            relation_pstmt->setInt(2, to_uid);
            relation_pstmt->executeUpdate();
            relation_pstmt->setInt(1, to_uid);
            relation_pstmt->setInt(2, from_uid);
            relation_pstmt->executeUpdate();
        }

        con->_con->commit();
        con->_con->setAutoCommit(true);
        return 0;
    }
    catch (const sql::SQLException& e) {
        try {
            con->_con->rollback();
            con->_con->setAutoCommit(true);
        }
        catch (...) {
        }
        std::cerr << "[MySqlDao.cpp] 函数 [HandleFriendRequest()] SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return -4;
    }
}
