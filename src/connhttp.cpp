#include "connhttp.h"
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>


int ConnHTTP::m_epollfd = -1;
int ConnHTTP::m_user_count = 0;


void setnonblocking(int sock) {
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
    setnonblocking(fd);
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);


}
//删除fd
void rmfd(int epollfd, int fd) {
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}
// modfd 重置socket上的EPOLLONESHOT事件
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
}
void ConnHTTP::Close() {
	if (m_sockfd != -1) {
		rmfd(m_epollfd, m_sockfd);
		m_sockfd = -1;
		m_user_count--; // 关闭连接客户数量减一
	}
}
//Read 从一个连接中读取全部数据
bool ConnHTTP::Read() {
	int bytes_read = 0;
	while(true) {
		bytes_read = recv(m_sockfd, m_read_buf + read_idx, READ_BUFFER_SIZE - read_idx, 0);
		if(bytes_read == -1) {
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			return false;
		}
		if(bytes_read == 0) {
			return false;
		}
		read_idx += bytes_read;
	}
	printf("读取到了数据：\n%s\n", m_read_buf);
	return true;
}


ConnHTTP::ERR_CODE ConnHTTP::parseReq() {


	return GET_REQUEST;
}
ConnHTTP::ERR_CODE ConnHTTP::parse_request_line(char *context) {


	return GET_REQUEST;
}
ConnHTTP::ERR_CODE ConnHTTP::parse_headers(char *context) {


	return GET_REQUEST;
}
ConnHTTP::ERR_CODE ConnHTTP::parse_content(char *context) {


	return GET_REQUEST;
}



//TODO Write
bool ConnHTTP::Write() {
	printf("writing\n");
	return true;
}

//TODO http请求入口函数
void ConnHTTP::Process() {
    //解析请求
	parseReq();
	
    //生成响应

}