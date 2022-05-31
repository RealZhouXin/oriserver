#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <sys/epoll.h>
#include <cstdio>
#include <signal.h>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <errno.h>
#include "fmt/core.h"
#include "locker.h"

namespace STATUS {
// HTTP请求方法，这里只支持GET
enum METHOD { GET = 0,
	          POST,
	          HEAD,
	          PUT,
	          DELETE,
	          TRACE,
	          OPTIONS,
	          CONNECT };

//解析客户端请求时，主状态机的状态
enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, //当前正在分析请求行
	               CHECK_STATE_HEADER,          //当前正在分析头部字段
	               CONTENT };                   //当前正在解析请求体

//服务器处理HTTP请求的可能结果，报文解析的结果
enum ERR_CODE { NO_REQUEST,          //请求不完整，需要继续读取客户数据
	            GET_REQUEST,         //表示获得了一个完成的客户请求
	            BAD_REQUEST,         //表示客户请求语法错误
	            NO_RESOURCE,         //表示服务器没有资源
	            FORBIDDEN_REQUEST,   //表示客户对资源没有足够的访问权限
	            FILE_REQUEST,        //文件请求,获取文件成功
	            INTERNAL_ERROR,      //表示服务器内部错误
	            CLOSED_CONNECTION }; //表示客户端已经关闭连接了

// 从状态机的三种可能状态，即行的读取状态，分别表示

enum LINE_STATUS { LINE_OK = 0, // 0.读取到一个完整的行
	               LINE_BAD,    // 1.行出错
	               LINE_OPEN }; // 2.行数据尚且不完整
};                              // namespace STATUS
using STATUS::METHOD;
using STATUS::CHECK_STATE;
using STATUS::LINE_STATUS;
using STATUS::ERR_CODE;

struct MesInfo {
	char *url;
	char *version;      // http协议版本
	METHOD method;      //请求方法
	char *host;         //主机名
	bool hold;          //是否要保持连接
	int content_length; //请求报文总长度
};

class ConnHTTP {
public:
	static int m_epollfd;                      //所有socket上的事件都被注册到一个epoll实例
	static int m_user_count;                   //统计用户数量
	static const int READ_BUFFER_SIZE = 2048;  //读缓冲区大小
	static const int WRITE_BUFFER_SIZE = 2048; //写缓冲区大小

public:
	ConnHTTP() {
	}
	~ConnHTTP() {
	}
	void Process(); //处理客户端请求
	//初始化连接
	void Init(int sockfd, const sockaddr_in &addr);
	//关闭连接
	void Close();
	// 非阻塞读写
	bool Read();
	bool Write();

private:
	//初始化连接
	void init();
	ERR_CODE parseReq();

	//解析请求报文

	ERR_CODE parse_first_line(char *text);
	ERR_CODE parse_headers(char *text);
	ERR_CODE parse_content(char *text);
	ERR_CODE do_request();
	char *get_line() {
		return m_read_buf + m_start_line;
	}
	LINE_STATUS split_line(int &, int size);

	//HTTP 响应
	void unmap();
	bool add_response(const char* format, ...);
	bool add_content(const char* content);
	bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

private:
	int m_sockfd; //该连接的socket
	sockaddr_in m_address;
	char m_read_buf[READ_BUFFER_SIZE];
	int read_idx;
	int m_checked_idx; // 当前正在分析的字符在读缓冲区中的位置
	int m_start_line;  // 当前正在解析的行的起始位置

	CHECK_STATE m_check_state; // 主状态机当前所处的状态
	MesInfo mesInfo;
};

#endif