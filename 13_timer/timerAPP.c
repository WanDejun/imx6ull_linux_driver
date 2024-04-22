#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

char* userData = "user data!\n";

/*
 * @ param:
 *      argc: 引用程序参数个数
 *      argv: 调用时的内容字符串;
 *          第一个参数为: "./CharDevBaseAPP.c";
 *          第二个参数为: "<filename>";
 *          第三个参数为: "0" / "1" 表示读/写;
 *          第四个参数为: 读取的数据长度/写入的数据的内容;
 */
int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("args error, need 4, but read %d\n", argc);
        return 1;
    }

    int fd = 0, ret = 0;
    char *filename, *opcode = argv[2];
    char readBuf[128], writeBuf[128];

    strcpy(writeBuf, userData);

    filename = argv[1];

    fd = open(filename, O_RDWR); 
    if (fd < 0) { // open error
        printf("Can't open file %s\n", filename);
    }

    switch (opcode[0]) {
    case '0': //read
        ret = read(fd, readBuf, atoi(argv[3]));

        if (ret < 0) { // failed
            printf("read file %s failed\n", filename);
        }
        else { // success
            printf("read file %s: %s\n", filename, readBuf);
        }
        break;
    case '1': //write
        ret = write(fd, argv[3], strlen(argv[3]));
        if (ret < 0) { // failed
            printf("%s length: %d\n", argv[3], strlen(argv[3]));
            printf("write file %s failed\n", filename);
        }
        break;
    default: // opcode error
        printf("option code error!\n");
    }

    ret = close(fd);
    if (ret < 0) {
        printf("close file %s failed\n", filename);
    }

    return 0;
}