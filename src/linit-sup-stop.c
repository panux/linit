#include<stdlib.h>
#include<stdio.h>
#include<errno.h>
#include<unistd.h>
#include<sys/socket.h>
#include<sys/un.h>
#include<string.h>

#define streq(x, y) (strcmp((x) , (y)) == 0)
int main(int argc, char **argv) {
    if(argc != 3) {
        perror("Missing args\n");
        return 1;
    }
    //connect
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd == -1) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        return 65;
    }
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strcpy(addr.sun_path, "/tmp/linit-supd.sock");
    if(connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1) {
        fprintf(stderr, "Failed to connect: %s\n", strerror(errno));
        return 65;
    }
    FILE *supconn = fdopen(fd, "r+");
    if(supconn == NULL) {
        fprintf(stderr, "Failed to convert fd to stream: %s\n", strerror(errno));
        return 65;
    }
    //write command
    if(fprintf(supconn, "^%d%s\n", atoi(argv[1]), argv[2]) == -1) {
        fprintf(stderr, "Failed to write to linit-supd connection: %s\n", strerror(errno));
        return 65;
    }
    if(fflush(supconn) != 0) {
        fprintf(stderr, "Failed to flush linit-supd connection: %s\n", strerror(errno));
        return 65;
    }
    //get response
    char resp[80];
    if(fgets(resp, 80, supconn) == NULL) {
        fprintf(stderr, "Failed to get response from linit-supd connection: %s\n", strerror(errno));
        return 65;
    }
    fclose(supconn);
    if(!streq(resp, "ok\n")) {
        return 65;
    }
    return 0;
}
