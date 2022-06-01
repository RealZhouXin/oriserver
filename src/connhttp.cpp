#include "connhttp.h"
#include "fmt/core.h"
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <fmtlog/fmtlog.h>

using fmt::print;

int ConnHTTP::m_epollfd = -1;
int ConnHTTP::m_user_count = 0;

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

//网站的根目录
const char* doc_root = "/home/xin/cnet/httpserver/data";

void setnonblock(int sock) {
	int opts;
	opts = fcntl(sock, F_GETFL);
	if (opts < 0) {
		perror("fcntl(sock,GETFL)");
		exit(1);
	}
	opts = opts | O_NONBLOCK;
	if (fcntl(sock, F_SETFL, opts) < 0) {
		perror("fcntl(sock,SETFL,opts)");
		exit(1);
	}
}

// addfd 向epoll对象添加要监听的fd
void addfd(int epollfd, int fd, bool one_shot) {
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLRDHUP; // epollrdhup 异常断开
	if (one_shot) {
		event.events |= EPOLLONESHOT;
	}
	//设置文件描述符非阻塞
	setnonblock(fd);
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
}
//删除fd
void rmfd(int epollfd, int fd) {
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}
// modfd 重置socket上的EPOLL ONESHOT事件
void modfd(int epollfd, int fd, int ev) {
	epoll_event event;
	event.data.fd = fd;
	event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 初始化 http 连接
void ConnHTTP::Init(int sockfd, const sockaddr_in &addr) {
	m_sockfd = sockfd;
	m_address = addr;

	//端口复用
	int reuse = 1;
	setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	//添加到epoll对象中
	addfd(m_epollfd, m_sockfd, true);
	init();
}
void ConnHTTP::init() {

	bytes_to_send = 0;
	bytes_have_send = 0;
	m_check_state = STATUS::CHECK_STATE_REQUESTLINE;

	mesInfo.method = STATUS::GET;
	mesInfo.url = 0;
	mesInfo.version = 0;
	mesInfo.host = 0;
	mesInfo.hold = false;//默认不连接
	mesInfo.content_length = 0;

	m_start_line = 0;
	read_idx = 0;
	m_write_idx = 0;

	bzero(m_read_buf, READ_BUFFER_SIZE);
	bzero(m_write_buf, WRITE_BUFFER_SIZE);
	bzero(m_real_file, FILENAME_LEN);
	
}
void ConnHTTP::Close() {
	if (m_sockfd != -1) {
		rmfd(m_epollfd, m_sockfd);
		m_sockfd = -1;
		m_user_count--; // 关闭连接客户数量减一
	}
}
// Read 从一个连接中读取全部数据
bool ConnHTTP::Read() {
	int bytes_read = 0;
	while (true) {
		bytes_read = recv(m_sockfd, m_read_buf + read_idx, READ_BUFFER_SIZE - read_idx, 0);
		if (bytes_read == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			return false;
		}
		if (bytes_read == 0) {
			return false;
		}
		read_idx += bytes_read;
	}
	logi("读取到了数据：\n{}\n", m_read_buf);
	return true;
}

LINE_STATUS ConnHTTP::split_line(int &offset, int size) {
	print("in split_line(), offset = {}, size = {}\n", offset, size);
	for (int i = offset; i < size; ++i) {
		if (m_read_buf[i] == '\r') {
			print("0\n");
			if (i == size - 1) {
				print("1\n");
				return STATUS::LINE_OPEN;
			}
			if (m_read_buf[i + 1] == '\n') {
				m_read_buf[i++] = '\0';
				m_read_buf[i++] = '\0';
				offset = i;
				logi("2");
				print("2\n");
				return STATUS::LINE_OK;
			}
			return STATUS::LINE_BAD;
		}
		if (m_read_buf[i] == '\n') { //'\n'在检测到'\r'后就会被处理掉
			return STATUS::LINE_BAD;
		}
	}
	return STATUS::LINE_OPEN;
}

ERR_CODE ConnHTTP::parseReq() {
	LINE_STATUS lnSt = STATUS::LINE_OK;
	ERR_CODE err = STATUS::NO_REQUEST;
	char *text = 0;
	int split_start = 0;
	logi("in parse request\n");
	while ((m_check_state == CHECK_STATE::CONTENT && lnSt == LINE_STATUS::LINE_OK)
	       || (lnSt = split_line(split_start, read_idx)) == LINE_STATUS::LINE_OK) {
		text = get_line();
		m_start_line = split_start;
		logi("Get a request line: {}", text);
		switch (m_check_state) {
		case STATUS::CHECK_STATE_REQUESTLINE: {
			;
			if ((err = parse_first_line(text)) == STATUS::BAD_REQUEST) {
				loge("parse first line: {}", err);
				fmtlog::poll();
				return err;
			}
			break;
		}
		case STATUS::CHECK_STATE_HEADER: {
			if ((err = parse_headers(text)) == STATUS::BAD_REQUEST) {
				return STATUS::BAD_REQUEST;
			} else if(err == STATUS::GET_REQUEST){
				return do_request();
			}
			break;
		}
		case STATUS::CONTENT: {
			if ((err = parse_content(text)) == STATUS::GET_REQUEST) {
				return do_request();
			}
			lnSt = STATUS::LINE_OPEN;

			break;
		}

		default: {
			return STATUS::INTERNAL_ERROR;
		}
		}
	}

	return STATUS::GET_REQUEST;
}
ERR_CODE ConnHTTP::parse_first_line(char *context) {
	// GET /community/page/get/1 HTTP/1.1
	std::stringstream ss;
	ss.str(context);
	std::string item;
	int i = 0;
	while (ss >> item) {
		if (i == 0 && item == "GET") {
			mesInfo.method = STATUS::GET;
		} else {
			return STATUS::BAD_REQUEST;
		}
		if (i == 1) {
			mesInfo.url = (char *)item.c_str();
		}
		if (i == 2 && item == "HTTP/1.1") {
			mesInfo.version = (char *)item.c_str();
		} else {
			return STATUS::BAD_REQUEST;
		}
	}

	return STATUS::GET_REQUEST;
}
ERR_CODE ConnHTTP::parse_headers(char *text) {
	// 遇到空行，表示头部字段解析完毕
	if (text[0] == '\0') {
		// 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
		// 状态机转移到CHECK_STATE_CONTENT状态
		if (mesInfo.content_length != 0) {
			m_check_state = STATUS::CONTENT;
			return STATUS::NO_REQUEST;
		}
		// 否则说明我们已经得到了一个完整的HTTP请求
		return STATUS::GET_REQUEST;
	} else if (strncasecmp(text, "Connection:", 11) == 0) {
		// 处理Connection 头部字段  Connection: keep-alive
		text += 11;
		text += strspn(text, " \t");
		if (strcasecmp(text, "keep-alive") == 0) {
			mesInfo.hold = true;
		}
	} else if (strncasecmp(text, "Content-Length:", 15) == 0) {
		// 处理Content-Length头部字段
		text += 15;
		text += strspn(text, " \t");
		mesInfo.content_length = atol(text);
	} else if (strncasecmp(text, "Host:", 5) == 0) {
		// 处理Host头部字段
		text += 5;
		text += strspn(text, " \t");
		mesInfo.hold = text;
	} else {
		loge("oop! unknow header %s\n", text);
	}

	return STATUS::NO_REQUEST;
}
ERR_CODE ConnHTTP::parse_content(char *text) {
	return ERR_CODE::GET_REQUEST;
}

ERR_CODE ConnHTTP::do_request() {
	strcpy(m_real_file, doc_root);
	int len = strlen(doc_root);
	strncpy(m_real_file + len, mesInfo.url, FILENAME_LEN - len - 1);

	//获取m_realfile 文件的相关信息， -1 失败，0 成功
	if(int err = stat(m_real_file, &m_file_stat); err < 0) {
		loge("m_real_file state: {}", err);
		return STATUS::NO_RESOURCE;
	}
	//判断访问权限
	if(!(m_file_stat.st_mode & S_IROTH)) {
		loge("无访问权限");
		return STATUS::FORBIDDEN_REQUEST;
	}
	//判断是否是目录
	if(S_ISDIR(m_file_stat.st_mode)) {
		return STATUS::BAD_REQUEST;
	}

	//以只读方式打开文件
	int fd = open(m_real_file, O_RDONLY);
	//创建内存映射
	m_file_address = (char *) mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);


	return STATUS::FILE_REQUEST;
}

// TODO Write
bool ConnHTTP::Write() {
	printf("writing\n");
	return true;
}

// TODO http请求入口函数
void ConnHTTP::Process() {
	//解析请求
	fmt::print("hello pro\n");
	parseReq();
	fmtlog::poll();

	//生成响应
}