#include <stdio.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <poll.h>
#include "signal.h"

// #define USE_SELECT
#define USE_POLL
// #define USE_FASYNC

/**
 * @function: 阻塞(等待队列)或非阻塞(select/poll)读取key信息
 * @argc: 传入参数数量
 * @argv:   第一个参数为可执行文件
 *          第二个参数为设备驱动文件
 * @author: Dejun Wan
 * @time: 2024-4-26
 * @version: v0.0.1
*/

char usrBuf[33];
int fd;

static void sigio_signal_func(int signum){
    int ret = 0;
    ret = read(fd, usrBuf, 2);
    usrBuf[2] = '\0';
    if(ret < 0) { /* 读取错误 */
    
    } 
    else {
        printf("key value=%s\n", usrBuf);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("ERROR: args need be 2 but receive %d\n", argc);
        return;
    }

#ifdef USE_SELECT
    fd_set read_fd;
    struct timeval timeout;
#endif
#ifdef USE_POLL
    struct pollfd fds;
#endif
    int ret = 0;

    fd = open(argv[1], O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        printf("device open error!\n");
    }

        /* 设置信号 SIGIO 的处理函数 */
    signal(SIGIO, sigio_signal_func);
    fcntl(fd, F_SETOWN, getpid()); /* 将当前进程的进程号告诉给内核 */
    int flags = fcntl(fd, F_GETFD); /* 获取当前的进程状态*/
    fcntl(fd, F_SETFL, flags | FASYNC);/* 设置进程启用异步通知功能 */

    while (1) {
#ifdef USE_SELECT
        FD_ZERO(&read_fd);
        FD_SET(fd, &read_fd);
        timeout.tv_sec = 0;
        timeout.tv_usec = 5000000;

        ret = select(fd + 1, &read_fd, NULL, NULL, &timeout);
        if (ret == 0)  printf("time out\n"); 
        else if (ret < 0) printf("select ERROR!\n");
        else {
            if (FD_ISSET(fd, &read_fd)) {
                ret = read(fd, usrBuf, 2);
                if (ret < 0) {
                    //printf("read ERROR!\n");
                }
                else {
                    usrBuf[ret] = '\0';
                    printf("key val = %s\n", usrBuf);
                }
            }
        }
#endif
#ifdef USE_POLL
        fds.fd = fd;
        fds.events = POLLIN;

        ret = poll(&fds, 1, 1000); // 等待1000ms
        if (ret == 0)  printf("time out\n"); 
        else if (ret < 0) printf("poll ERROR!\n");
        else {
            if (fds.revents & POLLIN) {
                ret = read(fd, usrBuf, 2);
                if (ret < 0) {
                    //printf("read ERROR!\n");
                }
                else {
                    usrBuf[ret] = '\0';
                    printf("key val = %s\n", usrBuf);
                }
            }
        }
#endif
    }

    close(fd);
    return 0;
}