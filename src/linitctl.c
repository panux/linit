#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<errno.h>
#include<string.h>
#include<stdbool.h>
#include<sys/socket.h>
#include<sys/un.h>

//connect to linitd
FILE* connectToServer() {
    char *sp = getenv("LINITSOCK");
    if(sp == NULL) {
        fprintf(stderr, "[FATAL] LINITSOCK not set\n");
        exit(65);
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd == -1) {
        fprintf(stderr, "[FATAL] Failed to create socket: %s\n", strerror(errno));
        exit(65);
    }
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    if((strlen(sp) + 1) > sizeof(addr.sun_path)) {
        fprintf(stderr, "[FATAL] Socket path is too long\n");
        exit(65);
    }
    strcpy(addr.sun_path, sp);
    if(connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1) {
        fprintf(stderr, "[FATAL] Failed to connect: %s\n", strerror(errno));
        exit(65);
    }
    FILE* stream = fdopen(fd, "r+");
    if(stream == NULL) {
        fprintf(stderr, "[FATAL] Failed to convert fd to stream\n");
        exit(65);
    }
    return stream;
}

//recieve a response
char *rcvResp(FILE* stream) {
    char* buf = NULL;
    size_t n = 0;
    if(getdelim(&buf, &n, '\0', stream) == -1) {
        fprintf(stderr, "[FATAL] Failed to read response\n");
        exit(65);
    }
    return buf;
}

//start subcommand
void cmd_start(int argc, char **argv, FILE *stream) {
    if(argc < 1) {
        fprintf(stderr, "[FATAL] Missing arguments\n");
        exit(1);
    }
    //send command
    {
        //calculate length of command string
        size_t n = argc;                        //spaces
        for(size_t i = 0; i < argc; i++) {
            n += strlen(argv[i]);               //strings
        }
        char pre[] = "start";
        n += strlen(pre);                       //length of pre
        n++;                                    //null terminator
        //generate command string
        char cmd[n];
        char *c = cmd;
        strcpy(c, pre); c += strlen(pre);
        for(size_t i = 0; i < argc; i++) {
            *c = ' ';
            c++;
            strcpy(c, argv[i]);
            c += strlen(argv[i]);
        }
        //send command string
        if(fwrite(cmd, n, 1, stream) != 1) {
            fprintf(stderr, "[FATAL] Failed to write command\n");
            exit(65);
        }
        if(fflush(stream) != 0) {
            fprintf(stderr, "[FATAL] Failed to flush stream: %s\n", strerror(errno));
            exit(65);
        }
    }
    //wait for completion
    while(argc > 0) {
        char *resp = rcvResp(stream);
        char *stat = NULL;
        int nspace = 0;
        for(size_t i = 0; (resp[i] != '\0') && (nspace < 2); i++) {
            if(resp[i] == ' ') {
                stat = resp + i;
                nspace++;
            }
        }
        stat++;
        if(nspace < 2) {
            fprintf(stderr, "[FATAL] Bad response: \"%s\"\n", resp);
            exit(65);
        }
        if(strcmp(stat, "running") == 0) {
            argc--;
        }
        free(resp);
    }
}
//command to send a state change command
void cmd_state(int argc, char **argv, FILE *stream) {
    if(argc != 2) {
        fprintf(stderr, "[FATAL] Missing arguments\n");
        exit(1);
    }
    char pre[] = "state ";
    size_t n = strlen(pre) + strlen(argv[0]) + strlen(argv[1]) + 2;
    char buf[n];
    char *b = buf;
    strcpy(b, pre);
    b += strlen(pre);
    strcpy(b, argv[0]);
    b += strlen(argv[0]);
    *b = ' ';
    b++;
    strcpy(b, argv[1]);
    if(fwrite(buf, n, 1, stream) != 1) {
        fprintf(stderr, "[FATAL] Failed to write command\n");
        exit(65);
    }
}
//command to stop a service
void cmd_stop(int argc, char **argv, FILE *stream) {
    if(argc != 1) {
        fprintf(stderr, "[FATAL] Missing arguments\n");
        exit(1);
    }
    char pre[] = "stop ";
    size_t n = strlen(pre) + strlen(argv[0]) + 1;
    char buf[n];
    char *b = buf;
    strcpy(b, pre);
    b += strlen(pre);
    strcpy(b, argv[0]);
    if(fwrite(buf, n, 1, stream) != 1) {
        fprintf(stderr, "[FATAL] Failed to write command\n");
        exit(65);
    }
    //wait for completion
    while(true) {
        char *resp = rcvResp(stream);
        char *stat = NULL;
        int nspace = 0;
        for(size_t i = 0; (resp[i] != '\0') && (nspace < 2); i++) {
            if(resp[i] == ' ') {
                stat = resp + i;
                nspace++;
            }
        }
        stat++;
        if(nspace < 2) {
            fprintf(stderr, "[FATAL] Bad response: \"%s\"\n", resp);
            exit(65);
        }
        if(strcmp(stat, "stopped") == 0) {
            free(resp);
            return;
        }
        free(resp);
    }
}


int main(int argc, char** argv) {
    if(argc < 2) {
        fprintf(stderr, "[FATAL] Insufficient arguments\n");
        return 1;
    }
    FILE* stream = connectToServer();
    if(strcmp(argv[1], "start") == 0) {
        cmd_start(argc - 2, argv + 2, stream);
    } else if(strcmp(argv[1], "stop") == 0) {
        cmd_stop(argc - 2, argv + 2, stream);
    } else if(strcmp(argv[1], "state") == 0) {
        cmd_state(argc - 2, argv + 2, stream);
    } else {
        fprintf(stderr, "[FATAL] Unrecognized subcommand: \"%s\"\n", argv[1]);
        return 1;
    }
    if(fclose(stream) != 0) {
        fprintf(stderr, "[FATAL] Failed to close stream: %s\n", strerror(errno));
        return 65;
    }
    return 0;
}
