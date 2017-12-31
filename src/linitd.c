#define _GNU_SOURCE
#include<stdint.h>
#include<stdlib.h>
#include<strings.h>
#include<stdbool.h>
#include<unistd.h>
#include<string.h>
#include<sys/epoll.h>
#include<errno.h>
#include<stdio.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<sys/un.h>
#include<arpa/inet.h>
#include<sys/wait.h>

#define streq(x, y) (strcmp((x) , (y)) == 0)

//byte type - for working with arbitrary data
typedef uint8_t byte;
void addSock(int fd);
void epoll_cycle();
int backlog = 64;

//epoll fd
int epollfd;

//forward declare structs
typedef uint32_t refcnt;
struct conn;
struct epolldat;
struct service;
struct servicepool;
struct buf;
struct buf {
    byte* dat;
    size_t len;
};
struct epolldat {
    enum{ epoll_conn, epoll_socket } typecode;
    union {
        struct conn* c;
        int sockfd;
    };
};
struct conn {
    struct epolldat e;
    refcnt ref;
    int fd;
    bool closed;
    bool txoff;
    struct buf rx, tx;
};


//quick malloc wrappers
void *lalloc(size_t n);
#define alalloc(t) (lalloc(sizeof(t)))
#define arralloc(t, n) (alalloc(t[(n)]))
void *lalloc(size_t n) {
    void *dat = malloc(n);
    if(dat != NULL) {
        bzero(dat, n);
    }
    return dat;
}

//reference counting
#define ref(x) (((x)->ref)++)
#define deref(x) if((((x)->ref)--) == 0) { free(x); }

//services
typedef enum{
    service_status_null,        //service not running
    service_status_starting,    //service starting
    service_status_running,     //service running
    service_status_fail,        //service failed
    service_status_stopping,    //service stopping
    service_status_stopped      //service stopped
} servicestate;

void close_conn(struct conn *c);

//get a string representing a state
char *statestr(servicestate state) {
    switch(state) {
    case service_status_null:
        return "null";
    case service_status_starting:
        return "starting";
    case service_status_running:
        return "running";
    case service_status_stopping:
        return "stopping";
    case service_status_stopped:
        return "stopped";
    default:
        return "bad";
    }
}
struct service {
    char *name;             //name of service
    servicestate state;     //state of service
    struct conn **notify;   //connections to notify of state changes
    size_t notify_n;        //number of elements in notify
    struct service *next;   //next service on chain
};
//mark a conn as to be notified on state change
bool setNotify(struct service* s, struct conn *c) {
    struct conn **onot = s->notify;
    size_t onot_n = s->notify_n;
    size_t nnot_n = onot_n + 1;
    struct conn **nnot = arralloc(struct conn*, nnot_n);
    if(nnot == NULL) {
        return false;
    }
    memcpy(nnot, onot, sizeof(struct notify*) * onot_n);
    nnot[onot_n] = c;
    ref(c);
    free(onot);
    s->notify = nnot;
    s->notify_n++;
    return true;
}
//place notification in tx buffer and activate writing if necessary
enum{ notify_fail, notify_success, notify_gone } notify(struct conn *c, struct service* s, struct buf msg) {
    if(c->closed == true) {
        deref(c);   //get rid of reference so the conn can be freed
        return notify_gone;
    }
    byte* ndat = realloc(c->tx.dat, c->tx.len + msg.len);
    if(ndat == NULL) {
        return notify_fail;
    }
    memcpy(ndat + c->tx.len, msg.dat, msg.len);
    c->tx.dat = ndat;
    c->tx.len += msg.len;
    if(c->txoff && (c->tx.len > 0)) {  //tx buffer was just populated
        //enable write in epoll
        struct epoll_event ev = {.events = EPOLLIN | EPOLLOUT, .data = {.ptr = &c->e}};
        if(epoll_ctl(epollfd, EPOLL_CTL_MOD, c->fd, &ev) == -1) {   //failed to enable - close and forget
            fprintf(stderr, "[ERROR] Failed to enable writing on conn %d\n", c->fd);
            close_conn(c);
        } else {
            c->txoff = false;
        }
    }
    return notify_success;
}
//run on a state change to notify all associated conns of the change
bool notifyAll(struct service* s) {
    char *cmds = NULL;
    if(asprintf(&cmds, "notify %s %s", s->name, statestr(s->state)) == -1) {
        return false;
    }
    struct buf cmdb = { .dat = cmds, .len = strlen(cmds) + 1 };
    bool oks[s->notify_n];
    size_t nok = 0;
    for(size_t i = 0; i < s->notify_n; i++) {
        switch(notify(s->notify[i], s, cmdb)) {
        case notify_success:
            oks[i] = true;
            nok++;
            break;
        case notify_gone:
            oks[i] = false;
            break;
        case notify_fail:
            free(cmds);
            return false;
        }
    }
    if(nok != s->notify_n) {
        //compact the buffer
        struct conn **not = s->notify;
        for(size_t i = 0; i < s->notify_n; i++) {
            if(oks[i]) { *(not++) = s->notify[i]; }
        }
        //shrink memory allocation with realloc
        not = realloc(s->notify, sizeof(struct conn *) * nok);
        if(not == NULL) {   //reallocation failed - just change the notify_n
            s->notify_n = nok;
        } else {
            s->notify = not;
            s->notify_n = nok;
        }
    }
    free(cmds);
    return true;
}
//thing containing services
struct servicepool {
    struct service* first;
};
struct servicepool services;
struct service* findService(char* name) {
    struct service *s;
    for(s = services.first; s != NULL; s = s->next) {
        if(streq(s->name, name)) {
            return s;
        }
    }
    return s;
}
struct service* newService() {
    struct service* new = alalloc(struct service);
    if(new == NULL) {
        return NULL;
    }
    if(services.first == NULL) {
        services.first = new;
        return new;
    }
    struct service *s, *n;
    for(s = n = services.first; n != NULL; n = n->next) { s = n; }
    s->next = new;
    return new;
}
void rmService(char* name) {
    struct service *s, *n;
    for(s = n = services.first; (n != NULL) && (!streq(n->name, name)); n = n->next) { s = n; }
    if(n == NULL) {
        fprintf(stderr, "[FATAL] Service %s does not exist\n", name);
        exit(65);
    }
    s->next = n->next;
    //notifications should already have been dealt with
    free(n->name);
    free(n);
}

//arbitrary buffer
bool readBuf(struct buf *b, int fd) {  //read data from fd into the buffer
    byte dat[1024];
    size_t n = read(fd, dat, 1024);
    if(n < 0) {
        return false;
    } else if(n == 0) {
        return true;
    }
    byte* nd = arralloc(byte, n + b->len);
    if(nd == NULL) {
        return false;
    }
    memcpy(nd, b->dat, b->len);
    memcpy(nd + b->len, dat, n);
    free(b->dat);
    b->dat = nd;
    b->len += n;
    return true;
}
bool writeBuf(struct buf* b, int fd) { //write data from buffer into fd
    size_t n = write(fd, b->dat, b->len);
    if(n < 1) {
        return false;
    }
    byte* nd = arralloc(byte, n - b->len);
    if(nd == NULL) {
        return false;
    }
    memcpy(nd, b->dat + n, b->len - n);
    free(b->dat);
    b->dat = nd;
    b->len -= n;
    return true;
}

//misc io
void set_noblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags < 0) {
        goto fail;
    }
    flags |= O_NONBLOCK;
    if(fcntl(fd, F_SETFL, flags) == -1) {
        goto fail;
    }
    return;
fail:
    fprintf(stderr, "[FATAL] Failed to SET O_NONBLOCK on %d: %s\n", fd, strerror(errno));
    exit(65);
}


//epoll-associated data
void addSock(int fd) {  //add a socket to epoll
    printf("Sock: %d\n", fd);
    if(listen(fd, backlog) == -1) {
        fprintf(stderr, "[FATAL] Failed to run listen on %d: %s\n", fd, strerror(errno));
        exit(65);
    }
    struct epolldat *d = alalloc(struct epolldat);
    if(d == NULL) {
        fprintf(stderr, "[FATAL] Failed to allocate epolldat for %d: %s\n", fd, strerror(errno));
        exit(65);
    }
    d->typecode = epoll_socket;
    d->sockfd = fd;
    //register with epoll
    struct epoll_event ev = {.events = EPOLLIN, .data = {.ptr = d}};
    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        fprintf(stderr, "[FATAL] Failed to register with epoll: %s\n", strerror(errno));
        exit(65);
    }
}
void handle_conn(struct conn* c, uint32_t events);
void handle_socket(int sockfd, uint32_t events);
void handle_epoll(struct epoll_event ev) {  //handle an epoll event
    struct epolldat d = *((struct epolldat*)ev.data.ptr);
    switch(d.typecode) {
    case epoll_conn:
        handle_conn(d.c, ev.events);
        break;
    case epoll_socket:
        handle_socket(d.sockfd, ev.events);
        break;
    }
}
void epoll_cycle() {    //run epoll wait with a timeout and handle the events
    struct epoll_event ev[4];
    int n = epoll_wait(epollfd, ev, 4, 50);
    if(n == -1) {
        fprintf(stderr, "[FATAL] epoll_wait failed: %s\n", strerror(errno));
        exit(65);
        return;
    }
    for(int i = 0; i < n; i++) {
        handle_epoll(ev[i]);
    }
}

//connection info
struct conn *newConn(int fd) { //create a new conn with fd
    printf("[INFO] New connection: %d\n", fd);
    //set up conn data
    struct conn *c = alalloc(struct conn);
    if(c == NULL) {
        return NULL;
    }
    c->e.typecode = epoll_conn;
    c->e.c = c;
    c->fd = fd;
    c->txoff = true;
    //register with epoll
    struct epoll_event ev = {.events = EPOLLIN, .data = {.ptr = &c->e}};
    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        fprintf(stderr, "[FATAL] Failed to register with epoll: %s\n", strerror(errno));
        exit(65);
    }
    ref(c); //referenced by epoll
    //tell read and write not to block
    set_noblock(fd);
    return c;
}

//accepting connections
#define EPOLL_MODE(evs, mode) (((evs) & (mode)) != 0)
void handle_socket(int sockfd, uint32_t events) {
    if(EPOLL_MODE(events, EPOLLIN)) {
        int fd = accept(sockfd, NULL, NULL);
        if(fd == -1) {
            fprintf(stderr, "[ERROR] Failed to accept connection: %s\n", strerror(errno));
            return;
        }
        struct conn *c = newConn(fd);
        if(c == NULL) {
            fprintf(stderr, "[ERROR] Failed to create conn with fd %d\n", fd);
            if(close(fd) == -1) {
                fprintf(stderr, "[FATAL] Failed to close fd %d\n", fd);
                exit(65);
            }
            return;
        }
    } else if(EPOLL_MODE(events, EPOLLPRI)) {
        fprintf(stderr, "[FATAL] Recieved EPOLLPRI on socket fd %d\n", sockfd);
        exit(65);
    } else if(EPOLL_MODE(events, EPOLLERR)) {
        fprintf(stderr, "[FATAL] Recieved EPOLLERR on socket fd %d\n", sockfd);
        exit(65);
    } else {
        fprintf(stderr, "[FATAL] Recieved unrecognized epoll event %d on socket fd %d\n", events, sockfd);
        exit(65);
    }
}

void handle_scmd(struct conn *c);
bool conn_has_cmd(struct conn *c) { //is there is a full command in the buffer?
    if(c->closed) { return false; }
    struct buf b = c->rx;
    for(size_t i = 0; i < b.len; i++) {
        switch(b.dat[i]) {
        case '\n':
            b.dat[i] = '\0';
        case '\0':
            return true;
        }
    }
    return false;
}
void close_conn(struct conn *c) {   //close a connection
    printf("[INFO] Disconnecting connection %d\n", c->fd);
    if(epoll_ctl(epollfd, EPOLL_CTL_DEL, c->fd, NULL) == -1) {  //not compat before Linux 2.6.9
        fprintf(stderr, "[FATAL] Failed to deregister conn with epoll\n");
        exit(65);
    }
    if(close(c->fd) == -1) {
        fprintf(stderr, "[FATAL] Failed to close fd %d: %s\n", c->fd, strerror(errno));
        exit(65);
    }
    c->closed = true;
    c->fd = -1;
    //wipe rx/tx buffers
    free(c->rx.dat);
    free(c->tx.dat);
    bzero(&c->rx, sizeof(c->rx));
    bzero(&c->tx, sizeof(c->tx));
    deref(c);
}
void handle_conn(struct conn *c, uint32_t events) {
    ref(c); //make sure that c isn't freed in the middle
    if(EPOLL_MODE(events, EPOLLIN)) {
        if(!readBuf(&c->rx, c->fd)) {
            fprintf(stderr, "[ERROR] Failed to read from connection %d\n", c->fd);
            close_conn(c);
        }
        if(conn_has_cmd(c)) {
            handle_scmd(c);
            if(c->closed) { //command closed connection
                deref(c);
                return;
            }
            if(c->txoff && (c->tx.len > 0)) {  //tx buffer was just populated
                //enable write in epoll
                struct epoll_event ev = {.events = EPOLLIN | EPOLLOUT, .data = {.ptr = &c->e}};
                if(epoll_ctl(epollfd, EPOLL_CTL_MOD, c->fd, &ev) == -1) {   //failed to enable - close and forget
                    fprintf(stderr, "[ERROR] Failed to enable writing on conn %d\n", c->fd);
                    close_conn(c);
                } else {
                    c->txoff = false;
                }
            }
        }
    } else if(EPOLL_MODE(events, EPOLLOUT)) {   //available for writing
        if(!writeBuf(&c->tx, c->fd)) {
            fprintf(stderr, "[ERROR] Failed to write to connection %d\n", c->fd);
            close_conn(c);
        } else {
            if(c->tx.len == 0) {    //write done - disable it in epoll
                struct epoll_event ev = {.events = EPOLLIN, .data = {.ptr = &c->e}};
                if(epoll_ctl(epollfd, EPOLL_CTL_MOD, c->fd, &ev) == -1) {   //failed to disable - close and forget
                    fprintf(stderr, "[ERROR] Failed to disable writing on conn %d\n", c->fd);
                    close_conn(c);
                } else {
                    c->txoff = true;
                }
            }
        }
    } else if(EPOLL_MODE(events, EPOLLRDHUP) || EPOLL_MODE(events, EPOLLHUP)) {
        printf("[INFO] Connection %d closed by peer\n", c->fd);
        close_conn(c);
    } else {
        fprintf(stderr, "[FATAL] Unrecognized epoll event %d on fd %d\n", events, c->fd);
        exit(65);
    }
    deref(c);
}

//forking/execing (arguments are like main)
bool fexec(int argc, char **argv) { //forks and exec's
    switch(fork()) {
    case -1:    //failure
        return false;
    case 0:;    //child
        char *args[argc + 1];
        for(int i = 0; i < argc; i++) {
            args[i] = argv[i];
        }
        args[argc] = NULL;
        if(execvpe(args[0], args, environ) == -1) {
            fprintf(stderr, "[FATAL] exec failed: %s\n", strerror(errno));
            exit(65);
        }
    }
    return true;
}
//run the start command
bool runStart(char *svc) {
    int argc = 2;
    char *argv[2] = {"linit-start", svc};
    return fexec(argc, argv);
}
//run the stop command
bool runStop(char *svc) {
    int argc = 2;
    char *argv[2] = {"linit-stop", svc};
    return fexec(argc, argv);
}

bool hasSpace(char *str) {
    for(; *str != '\0'; str++) {
        if(*str == ' ') {
            return true;
        }
    }
    return false;
}

//handle a request to start a service
//will notify of the state of the service if it has already been started
//command arguments: servicename1 servicename2 ...
void cmd_start(struct conn *c, char *args) {
    if(c->closed) {
        return;
    }
    ref(c);
    if(hasSpace(args)) {    //starting multiple - break into multiple commands
        char *arg, *a;
        arg = a = args;
        for(;;) {
            switch(*a) {
            case '\0':
                cmd_start(c, arg);
                goto end;
            case ' ':
                *a = '\0';
                cmd_start(c, arg);
                a = arg = a + 1;
                break;
            default:
                a++;
            }
        }
    } else {
        struct service *svc = findService(args);
        if(svc != NULL) {   //service already started
            if(!setNotify(svc, c)) {
                fprintf(stderr, "Failed to set conn %d to be notified about %s\n", c->fd, args);
                close_conn(c);
                goto end;
            }
            //format: "notify %s %s", args, status
            char not[] = "notify ";
            char *status = statestr(svc->state);
            char ntxt[strlen(not) + strlen(args) + strlen(status) + 2];
            char *nt = ntxt;
            strcpy(nt, not);
            nt += strlen(not);
            strcpy(nt, args);
            nt += strlen(args);
            *nt = ' ';
            nt++;
            strcpy(nt, status);
            struct buf dat = { .dat = ntxt, .len = strlen(ntxt) + 1 };
            if(notify(c, svc, dat) != notify_success) {
                fprintf(stderr, "Failed to notify conn %d about the state of %s\n", c->fd, args);
                close_conn(c);
                goto end;
            }
        } else {            //start service and mark to be notified
            struct service *ns = newService();
            if(ns == NULL) {
                fprintf(stderr, "[ERROR] Failed to start service %s: out of memory\n", args);
                close_conn(c);  //we cant notify the service because we are out of memory
                goto end;
            }
            ns->name = strdup(args);
            ns->state = service_status_starting;
            setNotify(ns, c);
            if(!runStart(args)) {
                fprintf(stderr, "[ERROR] Failed to start service %s: fork/exec error\n", args);
                ns->state = service_status_fail;
            }
            if(!notifyAll(ns)) {    //notify client of start
                fprintf(stderr, "[ERROR] Failed to send notification about %s to conn %d\n", args, c->fd);
                close_conn(c);
                goto end;
            }
            if(ns->state == service_status_fail) {  //delete the service if it failed
                rmService(ns->name);
            }
        }
    }
end:
    deref(c);
}
//handle a request to stop a service
//will notify of the state of the service if it has already been stopped
//command argument: servicename
void cmd_stop(struct conn *c, char *arg) {
    ref(c);
    if(hasSpace(arg)) {
        //bad command syntax
        fprintf(stderr, "[ERROR] Bad stop command syntax recieved on conn %d: \"%s\"\n", c->fd, arg);
        close_conn(c);
        goto end;
    }
    struct service *svc = findService(arg);
    if(svc == NULL) {   //already stopped
        //format: "notify %s null"
        char not[] = "notify ";
        char nul[] = " null";
        char ntxt[strlen(not) + strlen(arg) + strlen(nul) + 1];
        memcpy(ntxt, not, strlen(not));
        memcpy(ntxt + strlen(not), arg, strlen(arg));
        memcpy(ntxt + strlen(not) + strlen(arg), nul, strlen(nul) + 1);
        //send it
        struct buf otx = c->tx;
        struct buf ntx = {
            .dat = realloc(otx.dat, otx.len + strlen(ntxt) + 1),
            .len = otx.len + strlen(ntxt) + 1,
        };
        memcpy(ntx.dat + otx.len, ntxt, strlen(ntxt) + 1);
        if(ntx.dat == NULL) {
            fprintf(stderr, "[ERROR] failed to notify %d that %s is already stopped\n", c->fd, arg);
            close_conn(c);
            goto end;
        }
        c->tx = ntx;
        if(c->txoff && (c->tx.len > 0)) {  //tx buffer was just populated
            //enable write in epoll
            struct epoll_event ev = {.events = EPOLLIN | EPOLLOUT, .data = {.ptr = &c->e}};
            if(epoll_ctl(epollfd, EPOLL_CTL_MOD, c->fd, &ev) == -1) {   //failed to enable - close and forget
                fprintf(stderr, "[ERROR] Failed to enable writing on conn %d\n", c->fd);
                close_conn(c);
            } else {
                c->txoff = false;
            }
        }
        goto end;
    }
    setNotify(svc, c);
    //stop service
    if(!runStop(arg)) {
        fprintf(stderr, "[ERROR] Failed to stop service %s: fork/exec error\n", arg);
        close_conn(c);
        goto end;
    }
    svc->state = service_status_stopping;
    if(!notifyAll(svc)) {
        fprintf(stderr, "[FATAL] Failed to notify clients of service %s stopping\n", arg);
        exit(65);
    }
end:
    deref(c);
}
//handles a request to update the state of a service
//command arguments: servicename state
void cmd_state(struct conn *c, char *args) {
    ref(c);
    if(!hasSpace(args)) {
    bad:
        //bad command syntax
        fprintf(stderr, "[ERROR] Bad state command syntax recieved on conn %d: \"%s\"\n", c->fd, args);
        close_conn(c);
        goto end;
    }
    //split up args
    char *sname, *states;
    for(sname = states = args; *states != ' '; states++) {}
    *states = '\0';
    states++;
    //identify state
    servicestate s = service_status_null;
    if(streq(states, "running")) {
        s = service_status_running;
        printf("[INFO] Finished %s\n", sname);
    } else if(streq(states, "stopped")) {
        s = service_status_stopped;
    } else {
        goto bad;
    }
    //get service and set state
    struct service *svc = findService(sname);
    if(svc == NULL) {
        fprintf(stderr, "[ERROR] Service %s not found\n", sname);
        close_conn(c);
        goto end;
    }
    svc->state = s;
    //notify of change
    if(!notifyAll(svc)) {
        fprintf(stderr, "[FATAL] Failed to notify conns of service %s state change (to %s)\n", sname, states);
        exit(65);
    }
    if(s == service_status_stopped) {   //service has stopped - clean it up
        for(size_t i = 0; i < svc->notify_n; i++) {
            deref(svc->notify[i]);
        }
        free(svc->notify);
        rmService(sname);
    }
end:
    deref(c);
}

void handle_scmd(struct conn *c) {
    ref(c);
    //extract command from rx buffer
    char *cmds = strdup(c->rx.dat);
    if(cmds == NULL) {
        fprintf(stderr, "[ERROR] Failed to extract command from conn %d\n", c->fd);
        close_conn(c);
        goto end;
    }
    //slide & shrink rx buffer
    memmove(c->rx.dat, c->rx.dat + strlen(cmds) + 1, c->rx.len - (strlen(cmds) + 1));
    c->rx.len -= strlen(cmds) + 1;
    byte *ndat = realloc(c->rx.dat, c->rx.len);
    if(ndat != NULL) {
        c->rx.dat = ndat;
    }
    //split command and args
    if(!hasSpace(cmds)) {
        fprintf(stderr, "[ERROR] Invalid command syntax (sent by conn %d): \"%s\"\n", c->fd, cmds);
        free(cmds);
        close_conn(c);
        goto end;
    }
    char *cmd, *args;
    for(cmd = args = cmds; *args != ' '; args++) {}
    *args = '\0';
    args++;
    //run handler
    if(streq(cmd, "start")) {
        cmd_start(c, args);
    } else if(streq(cmd, "stop")) {
        cmd_stop(c, args);
    } else if(streq(cmd, "state")) {
        cmd_state(c, args);
    } else {
        fprintf(stderr, "[ERROR] Unrecognized command \"%s\" sent on conn %d\n", cmd, c->fd);
        free(cmds);
        close_conn(c);
        goto end;
    }
    //cleanup
    free(cmds);
end:
    deref(c);
}


int main(int argc, char** argv) {
    int nservers = 0;
    if((epollfd = epoll_create(1)) == -1) {
        fprintf(stderr, "[FATAL] Failed to create an epoll file descriptor: %s\n", strerror(errno));
        return 65;
    }
    for(int i = 1; i < argc; i++) {
        if(streq(argv[i], "--unix")) {
            i++;
            if(i == argc) {
                fprintf(stderr, "[FATAL] Missing argument: unix socket path\n");
                return 65;
            }
            int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if(fd == -1) {
                fprintf(stderr, "[FATAL] Failed to create unix socket: %s\n", strerror(errno));
                return 65;
            }
            printf("[INFO] Starting unix domain socket server at %s with fd %d\n", argv[i], fd);
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            if(strlen(argv[i]) > sizeof(addr.sun_path)) {
                fprintf(stderr, "[FATAL] Bad argument: unix domain socket path is too long\n");
                return 65;
            }
            strncpy(addr.sun_path, argv[i], sizeof(addr.sun_path)-1);
            if(bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
                fprintf(stderr, "[FATAL] Failed to bind unix domain socket: %s\n", argv[i]);
                return 65;
            }
            addSock(fd);
            //format: "LINITSOCK=%s", argv[i]
            char lsk[] = "LINITSOCK=";
            char envs[strlen(lsk) + strlen(argv[i]) + 1];
            memcpy(envs, lsk, strlen(lsk));
            memcpy(envs + strlen(lsk), argv[i], strlen(argv[i]) + 1);
            char *lsks = strdup(envs);
            if((lsks == NULL) || (putenv(lsks) != 0)) {
                fprintf(stderr, "[FATAL] Failed to set LINITSOCK variable: %s\n", strerror(errno));
                return 65;
            }
            nservers++;
        } else if(streq(argv[i], "--tcp")) {
            i++;
            if(i == argc) {
                fprintf(stderr, "[FATAL] Missing argument: tcp server port\n");
                return 65;
            }
            int port = atoi(argv[i]);
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if(fd == -1) {
                fprintf(stderr, "[FATAL] Failed to create tcp socket: %s\n", strerror(errno));
                return 65;
            }
            printf("[INFO] Starting tcp server on port %d with fd %d\n", port, fd);
            struct sockaddr_in addr;
            bzero((char *) &addr, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port = htons((unsigned short)port);
            if(bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
                fprintf(stderr, "[FATAL] Failed to bind tcp socket: %s\n", strerror(errno));
                return 65;
            }
            addSock(fd);
            nservers++;
        } else if(streq(argv[i], "--backlog")) {
            i++;
            if(i == argc) {
                fprintf(stderr, "[FATAL] Missing argument: backlog value\n");
                return 65;
            }
            backlog = atoi(argv[i]);
            if((backlog < 1) || (backlog > SOMAXCONN)) {
                fprintf(stderr, "[FATAL] backlog value %d out of range (should be between 1 and %d)\n", backlog, SOMAXCONN);
                return 65;
            }
        } else {
            fprintf(stderr, "[FATAL] Bad argument: %s\n", argv[i]);
            return 65;
        }
    }
    if(nservers == 0) {
        fprintf(stderr, "[FATAL] No servers specified\n");
        return 65;
    }
    {
        struct service *bs = newService();
        if(bs == NULL) {
            fprintf(stderr, "[FATAL] Failed to start boot service: out of memory\n");
            return 65;
        }
        if((bs->name = strdup("boot")) == NULL) {
            fprintf(stderr, "[FATAL] Failed to start boot service: out of memory\n");
            return 65;
        }
        bs->state = service_status_starting;
        if(!runStart("boot")) {
            fprintf(stderr, "[FATAL] Failed to start boot service: fork/exec error\n");
            return 65;
        }
    }
    printf("[INFO] Starting server\n");
    for(;;) {
        epoll_cycle();
        waitpid(-1, NULL, WNOHANG);
    }
}
