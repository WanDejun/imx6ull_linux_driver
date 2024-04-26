#include <stdio.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

/**
 * @function: 阻塞读取key信息
 * @argc: 传入参数数量
 * @argv:   第一个参数为可执行文件
 *          第二个参数为设备驱动文件
 * @author: Dejun Wan
 * @time: 2024-4-26
 * @version: v0.0.1
*/

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("ERROR: args need be 2 but receive %d\n", argc);
        return;
    }
    int fd = open(argv[1], O_RDWR), ret;
    char usrBuf[33];
    if (fd < 0) {
        printf("device open error!\n");
    }

    while (1) {
        ret = read(fd, usrBuf, 1);
        if (ret < 0) continue;
        
        printf("%s\n", (usrBuf[0] == '1') ? "key0: on" : "key0: off");
    }
    return 0;
}