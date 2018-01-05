#define _GNU_SOURCE
#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<errno.h>
#include<unistd.h>
#include<sys/socket.h>
#include<sys/un.h>
#include<sys/wait.h>

#define streq(x, y) (strcmp((x) , (y)) == 0)
int main(int argc, char **argv) {
    //parse args
    char *logfile = NULL;
    char *pidf = NULL;
    char *name = NULL;
    int cmd_argc = 0;
    char **cmd_argv = NULL;
    for(int i = 1; i < argc; i++) {
        if(streq(argv[i], "--log")) {
            i++;
            if(i == argc) {
                perror("Missing arg for --log\n");
                return 1;
            }
            logfile = argv[i];
        } else if(streq(argv[i], "--pid")) {
            i++;
            if(i == argc) {
                perror("Missing arg for --pid\n");
                return 1;
            }
            pidf = argv[i];
        } else if(streq(argv[i], "--name")) {
            i++;
            if(i == argc) {
                perror("Missing arg for --name\n");
                return 1;
            }
            name = argv[i];
        } else if(streq(argv[i], "--")) {
            i++;
            cmd_argv = argv + i;
            cmd_argc = argc - i;
            i = argc;
        } else {
            fprintf(stderr, "Invalid arg: %s\n", argv[i]);
            return 1;
        }
    }
    if(name == NULL) {
        fprintf(stderr, "Name not set\n");
        return 1;
    }
    //get environment var
    char *sp = getenv("LINITSOCK");
    if(sp == NULL) {
        perror("LINITSOCK not set\n");
        return 1;
    }
    //connect to linitd
    FILE* linitdconn;
    {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if(fd == -1) {
            fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
            return 65;
        }
        struct sockaddr_un addr = { .sun_family = AF_UNIX };
        if((strlen(sp) + 1) > sizeof(addr.sun_path)) {
            fprintf(stderr, "Socket path is too long\n");
            return 65;
        }
        strcpy(addr.sun_path, sp);
        if(connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1) {
            fprintf(stderr, "Failed to connect: %s\n", strerror(errno));
            return 65;
        }
        linitdconn = fdopen(fd, "r+");
        if(linitdconn == NULL) {
            fprintf(stderr, "Failed to convert fd to stream: %s\n", strerror(errno));
            return 65;
        }
    }
    //open log file
    FILE* log_file = NULL;
    if(logfile != NULL) {
        log_file = fopen(logfile, "w");
        if(log_file == NULL) {
            fprintf(stderr, "Failed to open log file: %s\n", strerror(errno));
            return 65;
        }
    }
    pid_t parentpid = getpid();
    pid_t childpid;
    switch(childpid = fork()) {
    case -1:
        perror("Failed to fork\n");
        return 65;
    case 0:
        {
            //open connection to supervisor daemon
            FILE* supconn = NULL;
            {
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
                supconn = fdopen(fd, "r+");
                if(supconn == NULL) {
                    fprintf(stderr, "Failed to convert fd to stream: %s\n", strerror(errno));
                    return 65;
                }
            }
            if(fprintf(supconn, "*%d%s\n", (int)childpid, name) == -1) {
                fprintf(stderr, "Failed to write to linit-supd connection: %s\n", strerror(errno));
                return 65;
            }
            if(fflush(supconn) != 0) {
                fprintf(stderr, "Failed to flush linit-supd connection: %s\n", strerror(errno));
                return 65;
            }
            char resp[80];
            if(fgets(resp, 80, supconn) == NULL) {
                fprintf(stderr, "Failed to get response from linit-supd connection: %s\n", strerror(errno));
                return 65;
            }
            fclose(supconn);
            if(!streq(resp, "ok\n")) {
                fprintf(stderr, "Bad response: %s\n", resp);
                return 65;
            }
            switch(childpid = fork()) {
            case -1:
                perror("Fork failed");
                fprintf(linitdconn, "state %s failed\n", name);
                fclose(linitdconn);
                fclose(log_file);
                return 65;
            case 0:
                //set log_file as stdout and stderr
                printf("Execing\n");
                fclose(linitdconn);
                if(log_file != NULL) {
                    dup2(fileno(log_file), 2);
                    dup2(fileno(log_file), 3);
                    fclose(log_file);
                }
                char *exec_argv[cmd_argc + 1];
                memcpy(exec_argv, cmd_argv, sizeof(*cmd_argv) * cmd_argc);
                exec_argv[cmd_argc] = NULL;
                printf("Here we go!\n");
                if(execvpe(exec_argv[0], exec_argv, environ) == -1) {
                    fprintf(stderr, "exec failed: %s\n", strerror(errno));
                    return 65;
                }
                return 65;  //should never be run
            }
            int code = 65;
            waitpid(childpid, &code, 0);
            if(code == 0) {
                fprintf(linitdconn, "state %s stopped\n", name);
            } else {
                fprintf(linitdconn, "state %s failed\n", name);
            }
            return 0;
        }
    }
    return 0;
}
