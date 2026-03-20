// global.h

#pragma once
//namespace beast = boost::beast;         // from <boost/beast.hpp>
//namespace http = beast::http;           // from <boost/beast/http.hpp>
//namespace net = boost::asio;            // from <boost/asio.hpp>
//using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

enum ErrorCodes {
	Success = 0,
	Error_Json = 1001,
	RPC_Failed = 1002,
	VerifyExpired = 1003,
	VerifyCodeError = 1004,
	UserExists = 1005,
	PasswdError = 1006,
	EmailNotMatch = 1007,
	PasswdUpFailed = 1008,
	PasswdInvalid = 1009,
	TokenInvalid = 1010,   //Token失效
	UidInvalid = 1011,  //uid无效
	MySQLFailed = 9999,
};

#define MAX_LENGTH  65535
//头部总长度
#define HEAD_TOTAL_LEN 4
//头部id长度
#define HEAD_ID_LEN 2
//头部数据长度
#define HEAD_DATA_LEN 2
#define MAX_RECVQUE  10000
#define MAX_SENDQUE 1000

enum MSG_IDS {
	MSG_CHAT_LOGIN = 1005, //用户登陆
	MSG_CHAT_LOGIN_RSP = 1006,   // 登录聊天服务器回包
	MSG_SEARCH_USER_REQ = 1007,
	MSG_SEARCH_USER_RSP = 1008,
	MSG_ADD_FRIEND_REQ = 1009,
	MSG_ADD_FRIEND_RSP = 1010,
	MSG_GET_FRIEND_REQUESTS_REQ = 1011,
	MSG_GET_FRIEND_REQUESTS_RSP = 1012,
	MSG_HANDLE_FRIEND_REQUEST_REQ = 1013,
	MSG_HANDLE_FRIEND_REQUEST_RSP = 1014,
	MSG_FRIEND_REQUESTS_PUSH = 1015,
	MSG_FRIEND_LIST_PUSH = 1016,
	MSG_GET_PRIVATE_MESSAGES_REQ = 1017,
	MSG_GET_PRIVATE_MESSAGES_RSP = 1018,
	MSG_SEND_PRIVATE_MESSAGE_REQ = 1019,
	MSG_SEND_PRIVATE_MESSAGE_RSP = 1020,
	MSG_PRIVATE_MESSAGE_PUSH = 1021,
	MSG_MARK_PRIVATE_MESSAGES_READ_REQ = 1022,
	MSG_MARK_PRIVATE_MESSAGES_READ_RSP = 1023,
	MSG_GET_FRIEND_LIST_REQ = 1024,
	MSG_GET_FRIEND_LIST_RSP = 1025,
};
