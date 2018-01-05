#include<stdlib.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<errno.h>
#include<ctype.h>
#include<sys/socket.h>
#include<sys/wait.h>
#include<sys/un.h>
#include<signal.h>

struct chain {
    char *name;
    pid_t pid;
    struct chain *next;
};

#define streq(x, y) (strcmp((x) , (y)) == 0)
int main(int argc, char **argv) {
    if(argc != 1) {
        perror("[FATAL] Should have no arguments\n");
        return 1;
    }
    //create socket
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    {
        if(sockfd == -1) {
            fprintf(stderr, "[FATAL] Failed to create unix socket: %s\n", strerror(errno));
            return 65;
        }
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, "/tmp/linit-supd.sock", sizeof(addr.sun_path)-1);
        if(bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            fprintf(stderr, "[FATAL] Failed to bind unix domain socket: %s\n", strerror(errno));
            return 65;
        }
    }
    //connect to linitd
    FILE* linitdconn;
    {
        char *sp = getenv("LINITSOCK");
        if(sp == NULL) {
            fprintf(stderr, "[FATAL] LINITSOCK not set\n");
            return 65;
        }
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if(fd == -1) {
            fprintf(stderr, "[FATAL] Failed to create socket: %s\n", strerror(errno));
            return 65;
        }
        struct sockaddr_un addr = { .sun_family = AF_UNIX };
        if((strlen(sp) + 1) > sizeof(addr.sun_path)) {
            fprintf(stderr, "[FATAL] Socket path is too long\n");
            return 65;
        }
        strcpy(addr.sun_path, sp);
        if(connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1) {
            fprintf(stderr, "[FATAL] Failed to connect: %s\n", strerror(errno));
            return 65;
        }
        linitdconn = fdopen(fd, "r+");
        if(linitdconn == NULL) {
            fprintf(stderr, "[FATAL] Failed to convert fd to stream\n");
            return 65;
        }
    }
    //chain (starts empty)
    struct chain *ch = NULL;
    //listen
    if(listen(sockfd, 16) == -1) {
        fprintf(stderr, "[FATAL] Failed to listen: %s\n", strerror(errno));
        return 65;
    }
    //handle connections
    printf("Starting linit-supd\n");
    for(;;) {
        printf("Accepting\n");
        int fd = accept(sockfd, NULL, NULL);
        printf("Accepted\n");
        if(fd == -1) {  //something went wrong - notify linitd and exit
            fprintf(stderr, "[FATAL] Failed to accept connection: %s\n", strerror(errno));
            if(fprintf(linitdconn, "state linit-supd failed\n") < 0) {  //failed to notify linitd
                fprintf(stderr, "[FATAL] Failed to notify linitd of failure: %s\n", strerror(errno));
            } else {
                fclose(linitdconn);
            }
            return 65;
        }
        FILE *stream = fdopen(fd, "r+");
        if(stream == NULL) {
            fprintf(stderr, "[ERROR] Failed to open stream: %s\n", strerror(errno));
            close(fd);
            continue;
        }
        char buf[1024];
        if(fgets(buf, 1024, stream) == NULL) {
            fprintf(stderr, "[ERROR] Failed to read from stream: %s\n", strerror(errno));
            fclose(stream);
            continue;
        }
        //strip trailing newline
        if(buf[strlen(buf) - 1] == '\n') {
            buf[strlen(buf) - 1] = '\0';
        } else {
            perror("[ERROR] Oversized command\n");
            fclose(stream);
            continue;
        }
        if(strlen(buf) < 2) {
            perror("[ERROR] Short command\n");
            fclose(stream);
            continue;
        }
        char cmd = buf[0];
        char *arg = buf + 1;
        printf("Recieved %s\n", buf);
        switch(cmd) {
        case '*':   //new supervision
            {
                char *pidnum = arg;
                char *name = arg;
                while(isdigit(*name)) { name++; }
                if(name == arg) {
                    perror("[ERROR] Command missing process ID\n");
                    fclose(stream);
                    continue;
                }
                struct chain **chainend = &ch;
                while(*chainend != NULL) { chainend = &((*chainend)->next); }
                struct chain *link = malloc(sizeof(struct chain));
                if(link == NULL) {
                    perror("[ERROR] OOM\n");
                    fclose(stream);
                    continue;
                }
                link->name = strdup(name);
                if(link->name == NULL) {
                    perror("[ERROR] OOM\n");
                    free(link);
                    fclose(stream);
                    continue;
                }
                *name = '\0';
                link->pid = atoi(pidnum);
                printf("Name: %s PID: %d\n", link->name, (int)link->pid);
                *chainend = link;
            }
            break;
        case '^':   //terminate & delete supervision
            {
                char *signum = arg;
                char *name = arg;
                while(isdigit(*name)) { name++; }
                if(name == arg) {
                    perror("[ERROR] Command missing signal number\n");
                    fclose(stream);
                    continue;
                }
                //find in chain
                struct chain **chainpos = &ch;
                while((*chainpos != NULL) && !streq(name, (*chainpos)->name)) {
                    chainpos = &((*chainpos)->next);
                }
                if(*chainpos == NULL) { //not found
                    perror("[ERROR] process not found");
                    fclose(stream);
                    continue;
                }
                //send signal
                pid_t pid = (**chainpos).pid;
                if(kill(pid, atoi(signum)) == -1) {
                    fprintf(stderr, "[ERROR] Failed to send signal: %s\n", strerror(errno));
                    fclose(stream);
                    continue;
                }
                //wait for exit
                waitpid(pid, NULL, 0);  //note: error ignored because race condition
                //remove from chain
                struct chain *c = *chainpos;
                *chainpos = (**chainpos).next;
                free(c);
            }
            break;
        }
        fprintf(stream, "ok\n");
        fclose(stream);
    }
}
