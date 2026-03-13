#pragma once
#include <string>
#include <vector>

struct UserInfo {
	std::string name;
	std::string pwd;
	int uid;
	std::string email;
};

struct FriendRequestInfo {
	long long request_id = 0;
	int from_uid = 0;
	std::string from_name;
	int to_uid = 0;
	std::string to_name;
	std::string remark;
	std::string status;
	std::string created_at;
	std::string handled_at;
};