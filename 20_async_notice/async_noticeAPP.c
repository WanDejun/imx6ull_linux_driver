#include <stdio.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>

// #define USE_SELECT
// #define USE_POLL
#define USE_ASYNC

/**
 * @function: 阻塞(等待队列)或非阻塞(select/poll)读取key信息
 * @argc: 传入参数数量
 * @argv:   第一个参数为可执行文件
 *          第二个参数为设备驱动文件
 * @author: Dejun Wan
 * @time: 2024-4-26
 * @version: v0.0.1
*/

int fd, ret;
char usrBuf[33];

static void sigio_signel_func(int num) {
    ret = read(fd, usrBuf, 2);
    if (ret < 0) {
        printf("read error\n");
    }
    else {
        usrBuf[2] = '\0';
        printf("key value:%s\n", usrBuf);
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
#ifdef USE_ASYNC

#endif

    fd = open(argv[1], O_RDWR | O_NONBLOCK), ret = 0;
    if (fd < 0) {
        printf("device open error!\n");
    }

#ifdef USE_ASYNC
    signal(SIGIO, sigio_signel_func); // 设置回调函数(当该线程被通知SIGIO信号时, sigio_signel_func会被执行)
    fcntl(fd, F_SETOWN, getpid()); // 内部调用fd->fasync()驱动接口, 将当前线程添加到fd->fasync的通知队列中
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | FASYNC); // 设置接收异步通知模式
#endif

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