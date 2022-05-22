#include <asm-generic/errno-base.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "connhttp.h"
#include "locker.h"
#include "threadpool.h"
#include <libgen.h>
#include <csignal>
#include <cstring>

const int MAX_FD = 65535; //最大文件描述符个数
const int MAX_EVENT_NUM = 10000;
//添加信号捕捉
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

//添加文件描述符到epoll中
extern int addfd(int epollfd, int fd, bool one_shot);
//删除fd
extern int rmfd(int epollfd, int fd);
extern int modfd(int epollfd, int fd, int ev);

//proactor 主线程监听并读取数据后将数据封装成任务类，插入到线程池的工作队列
int main(int argc, char* argv[]) {

    // if(argc <= 1) {
    //     printf("按照如下格式运行: %s port_number\n", basename(argv[0]));
    //     exit(-1);
    // }
    //获取端口号
    int port = 12000;
    //对SIGPIE信号做处理
    addsig(SIGPIPE, SIG_IGN);
    //创建线程池 初始化线程池
    threadpool<ConnHTTP> *pool = NULL;
    try {
        pool = new threadpool<ConnHTTP>;
    } catch(...) {
        exit(-1);
    }

    //创建一个数组用于保存所有的客户信息
    ConnHTTP *users = new ConnHTTP[MAX_FD];

    int lsnSock = socket(PF_INET, SOCK_STREAM, 0);
    //设置端口复用
    int reuse = 1;
    setsockopt(lsnSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr.s_addr);
    address.sin_port = htons(port);
    int err = bind(lsnSock, (struct sockaddr*)&address,sizeof(address));
    if (err < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    // 监听
    listen(lsnSock, 8);
    //创建epoll对象，事件数组，添加
    int epollfd = epoll_create(8);
    epoll_event events[MAX_EVENT_NUM];

    // 将监听的文件描述符到epoll对象中
    addfd(epollfd, lsnSock, false);
    ConnHTTP::m_epollfd = epollfd;

    while (true) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUM, -1);
        if((num < 0) && (errno != EINTR)) {
            printf("epoll failue\n");
            break;
        }
        //循环遍历
        for(int i = 0; i < num; i++) {
            int sockfd = events[i].data.fd;
            if(sockfd == lsnSock) {
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(lsnSock, (struct sockaddr*)&client_address, &client_addrlen);
                // 如果目前的连接数满了
                if(ConnHTTP::m_user_count >= MAX_FD) {


                    //TODO 给客户端信息：服务器正忙
                    close(connfd);
                    continue;
                }
                // 将新的客户的数据初始化，放到数组中
                users[connfd].Init(connfd, client_address);
            } else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {//对方异常断开
                users[sockfd].Close();
            } else if(events[i].events & EPOLLIN) {
                if(users[sockfd].Read()) {//一次性读完所有数据
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].Close();
                }
            } else if(events[i].events &EPOLLOUT) {
                if(!users[sockfd].Write()) {
                    users[sockfd].Close();
                }
            }
        }
    }
    close(epollfd);
    close(lsnSock);
    delete [] users;
    delete pool;
    return 0;
}