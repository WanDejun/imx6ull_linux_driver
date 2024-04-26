#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "linux/ioctl.h"

char* userData = "user data!\n";

#define CLOSE_CMD       (_IO(0xEF, 1))
#define OPEN_CMD        (_IO(0xEF, 2))
#define SETGAP_CMD      (_IOW(0XEF, 3, int))

/**
 * @param:
 *      @argc:引用程序参数个数
 *      @argv: 调用时的内容字符串;
 *              第一个参数为: "./ioctlAPP.c";
 *              第二个参数为: "<filename>";
 *              第三个参数为: "0" / "1" 表示读/写;
 *              第四个参数为: 读取的数据长度/写入的数据的内容;
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("args error, need 4, but read %d\n", argc);
        return 1;
    }

    int fd = 0, ret = 0;
    long arg, cmd;
    char *filename;
    char Buf[128];

    filename = argv[1];

    fd = open(filename, O_RDWR); 
    if (fd < 0) { // open error
        printf("Can't open file %s\n", filename);
        return fd;
    }

    while (1) {
        printf("Input CMD >>> ");
        ret = scanf("%ld", &cmd);
        while (getchar() != '\n');

        switch (cmd) {
        case 1: // 关闭
            ioctl(fd, CLOSE_CMD, &arg);
            break;
        case 2: // 开启
            ioctl(fd, OPEN_CMD, &arg);
            break;
        case 3: // 设置周期
            printf("Input new period time >>> ");
            scanf("%ld", &arg);
            printf("arg: %ld\n", arg);
            ioctl(fd, SETGAP_CMD, &arg);
            break;
        case 4:
            goto quit;
        default:
            printf("illegal CMD!\n");
            break;
        }
    }

quit:
    ret = close(fd);
    if (ret < 0) {
        printf("close file %s failed\n", filename);
        return ret;
    }

    return 0;
}