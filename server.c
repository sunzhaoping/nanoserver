#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include "options.h"
#include "logging.h"
#include "pubsub.h"
#include "server.h"
#include "hashmap.h"

static int init_count = 0;
static pthread_mutex_t init_lock;
static pthread_cond_t init_cond;
static int round_robin = 0;

#define BUFFER_SIZE 128
#define CHAN_QUEUE 1

#define SN_NO_PROVIDES 0
#define SN_NO_CONFLICTS 0
#define SN_NO_REQUIRES 0

struct sn_option options[] = {
    /* Generic options */
    {"verbose", 'v', NULL,
        SN_OPT_INCREMENT, offsetof (sn_options, verbose), NULL,
        SN_NO_PROVIDES, SN_NO_CONFLICTS, SN_NO_REQUIRES,
        "Generic", NULL, "Increase verbosity of the server"},
    
    {"help", 'h', NULL,
        SN_OPT_HELP, 0, NULL,
        SN_NO_PROVIDES, SN_NO_CONFLICTS, SN_NO_REQUIRES,
        "Generic", NULL, "This help text"},

    {"daemonize", 'd', NULL,
        SN_OPT_INCREMENT, offsetof (sn_options, daemonize), NULL,
        SN_NO_PROVIDES, SN_NO_CONFLICTS, SN_NO_REQUIRES,
       "SERVER", "DMN", "server run in daemon mode"},

    {"pidfile", 'c', NULL,
        SN_OPT_STRING,  offsetof (sn_options, pidfile), NULL,
        SN_NO_PROVIDES, SN_NO_CONFLICTS, SN_NO_REQUIRES,
        "SERVER", "PATH", "if server running as a daemon save the process id to a file"},

    {"port", 'p', NULL,
        SN_OPT_INT,  offsetof (sn_options, port), NULL,
        SN_NO_PROVIDES, SN_NO_CONFLICTS, SN_NO_REQUIRES,
        "SERVER", "PORT", "server bind port"},
    
    {"host", 'h', NULL,
        SN_OPT_STRING,  offsetof (sn_options, host), NULL,
        SN_NO_PROVIDES, SN_NO_CONFLICTS, SN_NO_REQUIRES,
        "SERVER", "IP", "server bind IP address"},
    
    {"backlog", 0, NULL,
        SN_OPT_INT,  offsetof (sn_options, backlog), NULL,
        SN_NO_PROVIDES, SN_NO_CONFLICTS, SN_NO_REQUIRES,
        "SERVER", "BACKLOG", "server socket backlog number"},

    {"maxclients", 0, NULL,
        SN_OPT_INT,  offsetof (sn_options, maxclients), NULL,
        SN_NO_PROVIDES, SN_NO_CONFLICTS, SN_NO_REQUIRES,
        "SERVER", "CLIENTS", "max clients number limit"},

    {"tnum", 0, NULL,
        SN_OPT_INT,  offsetof (sn_options, tnum), NULL,
        SN_NO_PROVIDES, SN_NO_CONFLICTS, SN_NO_REQUIRES,
        "SERVER", "CLIENTS", "worker thread number"},

    /* Sentinel */
    {NULL, 0, NULL,
        0, 0, NULL,
        0, 0, 0,
        NULL, NULL, NULL},
};

struct sn_commandline sn_cli = {
    "",
    "",
    options,
    0,
};


uint16_t sn_gets (const uint8_t *buf)
{
    return (((uint16_t) buf [0]) << 8) |
    ((uint16_t) buf [1]);
}

void sn_puts (uint8_t *buf, uint16_t val)
{
    buf [0] = (uint8_t) (((val) >> 8) & 0xff);
    buf [1] = (uint8_t) (val & 0xff);
}

uint32_t sn_getl (const uint8_t *buf)
{
    return (((uint32_t) buf [0]) << 24) |
    (((uint32_t) buf [1]) << 16) |
    (((uint32_t) buf [2]) << 8) |
    ((uint32_t) buf [3]);
}

void sn_putl (uint8_t *buf, uint32_t val)
{
    buf [0] = (uint8_t) (((val) >> 24) & 0xff);
    buf [1] = (uint8_t) (((val) >> 16) & 0xff);
    buf [2] = (uint8_t) (((val) >> 8) & 0xff);
    buf [3] = (uint8_t) (val & 0xff);
}

uint64_t sn_getll (const uint8_t *buf)
{
    return (((uint64_t) buf [0]) << 56) |
    (((uint64_t) buf [1]) << 48) |
    (((uint64_t) buf [2]) << 40) |
    (((uint64_t) buf [3]) << 32) |
    (((uint64_t) buf [4]) << 24) |
    (((uint64_t) buf [5]) << 16) |
    (((uint64_t) buf [6]) << 8) |
    (((uint64_t) buf [7] << 0));
}

void sn_putll (uint8_t *buf, uint64_t val)
{
    buf [0] = (uint8_t) ((val >> 56) & 0xff);
    buf [1] = (uint8_t) ((val >> 48) & 0xff);
    buf [2] = (uint8_t) ((val >> 40) & 0xff);
    buf [3] = (uint8_t) ((val >> 32) & 0xff);
    buf [4] = (uint8_t) ((val >> 24) & 0xff);
    buf [5] = (uint8_t) ((val >> 16) & 0xff);
    buf [6] = (uint8_t) ((val >> 8) & 0xff);
    buf [7] = (uint8_t) (val & 0xff);
}

/*
 * Worker thread: main event loop
 */
void *worker_libev(void *arg) {
    WORK_THREAD *self = arg;
    /* Any per-thread setup can happen here; thread_init() will block until
     * all threads have finished initializing.
     */
    pthread_mutex_lock(&init_lock);
    init_count++;
    pthread_cond_signal(&init_cond);
    pthread_mutex_unlock(&init_lock);
	
    self->thread_id = pthread_self();

    ev_loop(self->loop, 0);
    ev_loop_destroy(self->loop);
    chan_close(self->chan);
    chan_dispose(self->chan);
    hashmap_free(self->pubsub);
    return NULL;
}

/*
 * Creates a worker thread.
 */
void create_worker(void *(*func)(void *), void *arg) {
    pthread_t       thread;
    pthread_attr_t  attr;
    int             ret;
    pthread_attr_init(&attr);
    if ((ret = pthread_create(&thread, &attr, func, arg)) != 0) {
        log_err( "Can't create thread: %s\n",strerror(ret));
        exit(1);
    }
}

void async_cb (EV_P_ ev_async *w, int revents){
    sn_client* client = NULL;
    chan_recv(((WORK_THREAD*)(w->data))->chan, (void**)&client);
    if (NULL != client) {
        client->io_wather.data = client;
        client->thread = (WORK_THREAD*)(w->data);
        client->loop = ((WORK_THREAD*)(w->data))->loop;
        if(client->dispatch_sub){
            client->dispatch_sub = 0;
            ev_io_set(&client->io_wather,client->fd, EV_READ);
            ev_io_start(((WORK_THREAD*)(w->data))->loop,&client->io_wather);
            process_message(client->loop, &client->io_wather, client);
        }else{
            client->dispatch_sub = 0;
            ev_io_init(&client->io_wather,client_cb,client->fd,EV_READ);
            ev_io_start(((WORK_THREAD*)(w->data))->loop,&client->io_wather);
            log_info("thread[%lu] accept: fd :%d  addr:%s port:%d\n",
                     (unsigned long)((WORK_THREAD*)(w->data))->thread_id,
                     client->fd,
                     client->szAddr,
                     client->port);
        }
    }
}

/*
 * Set up a thread's information.
 */
void setup_thread(WORK_THREAD *self) {
    self->loop = ev_loop_new(0);
    if (! self->loop) {
        log_err("Can't allocate event base\n");
        exit(1);
    }

    self->async_watcher.data = self;
    self->pubsub = hashmap_new();
    /* Listen for notifications from other threads */
    ev_async_init(&self->async_watcher, async_cb);
    ev_async_start(self->loop, &self->async_watcher);
    self->chan = chan_init(CHAN_QUEUE);
}

void thread_init(){
    int nthreads = server.options.tnum;
    pthread_mutex_init(&init_lock, NULL);
    pthread_cond_init(&init_cond, NULL);
    work_threads = calloc(nthreads, sizeof(WORK_THREAD));
    if (! work_threads) {
        log_err("Can't allocate thread descriptors\n");
        exit(1);
    }
    server.thread_id = pthread_self();
    int i = 0;
    for (i = 0; i < nthreads; i++) {
        setup_thread(&work_threads[i]);
    }
    
    sigset_t sigpipe_mask;
    sigemptyset(&sigpipe_mask);
    sigaddset(&sigpipe_mask, SIGPIPE);
    sigset_t saved_mask;
    if (pthread_sigmask(SIG_BLOCK, &sigpipe_mask, &saved_mask) == -1) {
        perror("pthread_sigmask");
        exit(1);
    }
    

    /* Create threads after we've done all the libevent setup. */
    for (i = 0; i < nthreads; i++) {
        create_worker(worker_libev, &work_threads[i]);
    }

    /* Wait for all the threads to set themselves up before returning. */
    pthread_mutex_lock(&init_lock);
    while (init_count < nthreads) {
        pthread_cond_wait(&init_cond, &init_lock);
    }
    pthread_mutex_unlock(&init_lock);
}

int get_ncpus()
{
#if MACOS
        int nm[2];
        size_t len = 4;
        uint32_t count;

        nm[0] = CTL_HW; nm[1] = HW_AVAILCPU;
        sysctl(nm, 2, &count, &len, NULL, 0);

        if(count < 1) {
            nm[1] = HW_NCPU;
            sysctl(nm, 2, &count, &len, NULL, 0);
            if(count < 1) { count = 1; }
        }
        return count;
 #else
        return (int)sysconf(_SC_NPROCESSORS_CONF);
#endif
}

int ignore_sigpipe()
{
    struct sigaction sa, osa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    return sigaction(SIGPIPE, &sa, &osa);
}

static int setTcpNoDelay(int fd, int val)
{
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == -1)
    {
        log_err( "set tcp nodelay failed\n");
        return -1;
    }
    return 0;
}

static int setTcpNonBlocking(int fd)
{
    int opts;
    opts = fcntl(fd, F_GETFL);
    if (opts < 0) {
        log_err( "fcntl failed\n");
        return -1;
    }
    opts = opts | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, opts) < 0) {
        log_err( "fcntl failed\n");
        return -1;
    }
    return 0;
}

/* While server stop close the opened socket and pid file */
void beforeShutdown(){
    if(server.options.daemonize){
        log_info("Removing the pid file.");
        unlink(server.options.pidfile);
    }
    close(server.ipfd);
}

static int tcpSetReuseAddr(int fd) {
    int yes = 1;
    /* Make sure connection-intensive things like the redis benckmark
     * will be able to close/open sockets a zillion of times */
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        log_err("setsockopt SO_REUSEADDR: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int tcpListen(int s, struct sockaddr *sa, socklen_t len, int backlog) {
    if (bind(s,sa,len) == -1) {
        log_err("bind: %s", strerror(errno));
        close(s);
        return -1;
    }

    if (listen(s, backlog) == -1) {
        log_err( "listen: %s", strerror(errno));
        close(s);
        return -1;
    }
    return 0;
}

static int tcpV6Only(int s) {
    int yes = 1;
    if (setsockopt(s,IPPROTO_IPV6,IPV6_V6ONLY,&yes,sizeof(yes)) == -1) {
        log_err( "setsockopt: %s", strerror(errno));
        close(s);
        return -1;
    }
    return 0;
}

void dispath_conn(int anewfd,struct sockaddr_in asin)
{ 
    sn_client* client = client_create(anewfd);
    if(client == NULL){
        return;
    }
    client->fd = anewfd;
    strcpy(client->szAddr,(char*)inet_ntoa(asin.sin_addr));
    client->port = asin.sin_port;
    char tmp[64];
    sprintf(tmp, "%s:%d", client->szAddr , client->port);
    client->cookie = hash_func(tmp);

    // libev default loop, accept the new connection, round-robin
    // dispath to a work_thread.
    int robin = round_robin % init_count;
    chan_send(work_threads[robin].chan, client);
    ev_async_send(work_threads[robin].loop, &(work_threads[robin].async_watcher));
    round_robin = robin + 1;
} 

sn_client * client_create(int fd){
    sn_client* client = malloc(sizeof(sn_client));
    if(client == NULL){
        return NULL;
    }
    client->fd = fd;
    client->input = queue_init(BUFFER_SIZE);
    if(client->input == NULL){
        free(client);
        return NULL;
    }
    client->output = queue_init(BUFFER_SIZE);
    if(client->output == NULL){
        free(client->input);
        free(client);
        return NULL;
    }
    client->cookie = 0;
    client->dispatch_sub = 0;
    client->channel_id = 0;
    client->msg = NULL;
    return client;
}

void client_destroy(sn_client * client){
    if(client->input != NULL){
        while(client->input->size > 0){
            msg_t * msg = queue_remove(client->input);
            msg_free(msg);
        }
        queue_dispose(client->input);
        client->input = NULL;
    }
    if(client->output != NULL){
        while(client->output->size > 0){
            msg_t * msg = queue_remove(client->output);
            msg_free(msg);
        }
        queue_dispose(client->output);
        client->output = NULL;
    }

    if(client->msg != NULL){
        msg_free(client->msg);
    }
    
    if(client->channel_id > 0)
        unsubscribe(client, client->channel_id);
    free(client);
}

void stop_client(struct ev_loop *loop, struct ev_io *watcher){
    close(watcher->fd);
    if(watcher->data != NULL){
        sn_client * client = (sn_client *)watcher->data;
        client_destroy(client);
    }
    ev_io_stop(loop,watcher);
}

int process_message(struct ev_loop *loop, struct ev_io *watcher, sn_client * client){
    int msg_need_send = 0;
    while(client->input->size > 0){
        msg_t * msg = queue_peek(client->input);
        if(msg->state == MSG_READ_OK){
            
            msg_need_send ++;
            if(msg->header.proto == PROTO_PING)
            {
                if(msg->header.size != 8){
                    stop_client(loop, watcher);
                    return -1;
                }
                queue_remove(client->input);
                uint64_t server_ts = (uint64_t) (ev_now(loop) * 1000);
                uint64_t client_ts = sn_getll(msg->body);
                msg->header.cookie = client->cookie;
                msg->body = realloc(msg->body, 16);
                msg->header.size = 16;
                msg->read_size = 0;
                sn_putll(msg->body, client_ts);
                sn_putll((msg->body+8), server_ts);
                queue_add(client->output, msg);
                ev_io_stop(loop, watcher);
                ev_io_set(watcher,client->fd, EV_WRITE);
                ev_io_start(loop,watcher);
            }
            else if(msg->header.proto == PROTO_PING)
            {
                if(msg->header.size != 8){
                    stop_client(loop, watcher);
                    return -1;
                }
                queue_remove(client->input);
                uint64_t server_ts = (uint64_t) (ev_now(loop) * 1000);
                uint64_t client_ts = sn_getll(msg->body);
                msg->header.cookie = client->cookie;
                msg->body = realloc(msg->body, 16);
                msg->header.size = 16;
                msg->read_size = 0;
                sn_putll(msg->body, client_ts);
                sn_putll((msg->body+8), server_ts);
                queue_add(client->output, msg);
                ev_io_stop(loop, watcher);
                ev_io_set(watcher,client->fd, EV_WRITE);
                ev_io_start(loop,watcher);
            }
            else if(msg->header.proto == PROTO_PUB)
            {
                queue_remove(client->input);
                publish(client,client->channel_id,msg);
                msg_free(msg);
                
            }else if(msg->header.proto == PROTO_SUB){
                uint32_t channel_id = sn_getl(msg->body);
                if(client->channel_id >0){
                    unsubscribe(client, client->channel_id);
                    client->channel_id = 0;
                }
                int threadnum = server.options.tnum;
                uint32_t hash = hash_func_int(channel_id);
                int index = hash % threadnum;
                if(client->thread != &work_threads[index]){
                    ev_io_stop(loop, watcher);
                    ev_io_stop(client->loop, &client->io_wather);
                    client->dispatch_sub = 1;
                    chan_send(work_threads[index].chan, client);
                    ev_async_send(work_threads[index].loop, &(work_threads[index].async_watcher));
                    return -1;
                }else{
                    queue_remove(client->input);
                    msg_free(msg);
                    if(subscribe(client, channel_id)==0)
                        client->channel_id = channel_id;
                }
            }else if(msg->header.proto == PROTO_UNSUB){
                uint32_t channel_id = sn_getl(msg->body);
                unsubscribe(client, channel_id);
                client->channel_id = 0;
                queue_remove(client->input);
                msg_free(msg);
            }else{
                queue_remove(client->input);
                msg_free(msg);
                stop_client(loop, watcher);
                return -1;
            }
        }
    }
    return msg_need_send;
}

void client_cb(struct ev_loop *loop, struct ev_io *watcher, int revents){
    if(EV_ERROR & revents)
    {
        log_err("got a invalid event when read from client");
        return;
    }
    sn_client *client = (sn_client*) watcher->data;

    if(revents & EV_READ){
        ssize_t nread = 1;
        
        //如果接收队列未满，一直接接收，直到没有可接收的数据
        while(client->input->size < client->input->capacity){
            if(client->msg == NULL){
                client->msg = msg_init();
                msg_retain(client->msg);
                client->msg->creator = client;
            }
            msg_t * msg = client->msg;
            nread = 0;
            
            // 读取消息头
            if(msg->state == MSG_READ_HEADER){
                ssize_t read_size = HEADER_SIZE - msg->read_size;
                nread = read(watcher->fd, (&msg->header + msg->read_size), read_size);
                msg->read_size += nread;
                if(nread == read_size){
                    msg->state = MSG_READ_BODY;
                    msg->read_size = 0;
                    // 如果消息过长则关闭客户端
                    if(msg->header.size > MSG_MAX_SIZE){
                        stop_client(loop, watcher);
                        return;
                    }
                    // 如果数据包中有cookie且与服务器不一致则关闭客户端
                    if(msg->header.cookie != 0 && msg->header.cookie != client->cookie){
                        stop_client(loop, watcher);
                        return;
                    }
                }else
                    break;
            }
            // 读取消息内容
            if(msg->state == MSG_READ_BODY ){
                // 如果消息体无则直接进入完成状态
                if(msg->header.size <=0){
                    msg->state = MSG_READ_OK;
                    queue_add(client->input, client->msg);
                    client->msg = NULL;
                // 否则读取消息体内容
                }else{
                    if(msg->body == NULL )
                        msg->body = malloc(sizeof(uint8_t) * msg->header.size);
                    ssize_t read_size = msg->header.size - msg->read_size;
                    nread = read(watcher->fd, msg->body, read_size);
                    msg->read_size += nread;
                    if(nread == read_size){
                        msg->state = MSG_READ_OK;
                        queue_add(client->input, client->msg);
                        client->msg = NULL;
                    }else
                        break;
                }
            }
        }
        if(client->msg != NULL){
            msg_free(client->msg);
            client->msg = NULL;
        }
        
        // 如果读取消息过程出错，关闭连接
        if (nread < 0) {
            if((errno == EAGAIN) ||(errno == EWOULDBLOCK) || (errno == EINTR)){
                
            }else{
                log_err("Read on descriptor %d failed: %m",watcher->fd);
                stop_client(loop, watcher);
                return;
            }
        }
        
        // 如果客户端关闭，关闭连接
        if(nread == 0){
            stop_client(loop, watcher);
            return;
        }
        
        // 处理接收到的消息
        int result = process_message(loop, watcher, client);
        if(result == -1 )
            return;
    }
    
    if(revents & EV_WRITE){
        if(client->output != NULL){
            ssize_t nwrite = 1;
            int sent_num = 0;
            while(client->output->size > 0){
                msg_t * msg = queue_remove(client->output);
                ssize_t msg_size = HEADER_SIZE + msg->header.size;
                uint8_t * data = malloc(HEADER_SIZE + msg->header.size);
                data[0] = msg->header.proto;
                sn_putl((data +1), msg->header.cookie);
                sn_putl((data + 1 + 4), msg->header.size);
                memcpy((data + 1 + 4 + 4), msg->body ,msg->header.size);
                nwrite = write(watcher->fd, data, msg_size);
                free(data);
                msg_free(msg);
                sent_num ++;
                if(nwrite <= 0){
                    stop_client(loop, watcher);
                    return;
                }
            }
            if(sent_num>0){
                ev_io_stop(loop, watcher);
                ev_io_set(watcher,client->fd, EV_READ);
                ev_io_start(loop,watcher);
            }
        }
    }
}

void accept_cb(struct ev_loop *loop, struct ev_io *watcher, int revents){
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd;
    //struct ev_io *w_client = (struct ev_io*) malloc (sizeof(struct ev_io));

    if(EV_ERROR & revents)
    {
        log_err("got invalid event when accept");
        return;
    }
    client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_len);
    while (client_fd<0){
        if (errno == EAGAIN || errno == EWOULDBLOCK) 
        {
             client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_len);
             continue; 
        }
        else
        {
            log_err("accept error.[%s]\n", strerror(errno));
            return;
        }
    }

    if(setTcpNoDelay(client_fd,1) == -1){
        return;
    }

    if(setTcpNonBlocking(client_fd) == -1){
        return;
    }
    
    // Initialize and start watcher to read client requests
    //ev_io_init(w_client, read_cb, client_sd, EV_READ);
    //ev_io_start(loop, w_client);
    dispath_conn(client_fd, client_addr);
}

static int tcpServer(int port, char *bindaddr, int af, int backlog)
{
    int s, rv;
    char _port[6];  /* strlen("65535") */
    struct addrinfo hints, *servinfo, *p;

    snprintf(_port,6,"%d",port);
    memset(&hints,0,sizeof(hints));
    hints.ai_family = af;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    /* No effect if bindaddr != NULL */

    if ((rv = getaddrinfo(bindaddr,_port,&hints,&servinfo)) != 0) {
        log_err("%s", gai_strerror(rv));
        return -1;
    }
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((s = socket(p->ai_family,p->ai_socktype,p->ai_protocol)) == -1)
            continue;

        if(af == AF_INET6 && tcpV6Only(s) == -1) goto error;
        if(tcpSetReuseAddr(s) == -1) goto error;
        if(tcpListen(s,p->ai_addr,p->ai_addrlen,backlog) ==  -1) goto error;
        goto end;
    }
    if (p == NULL) {
        log_err("unable to bind socket");
        goto error;
    }

error:
    s = -1;
end:
    freeaddrinfo(servinfo);
    return s;
}

void initServer(void){
    server.ipfd = tcpServer(server.options.port,
                            server.options.host,
                            AF_INET ,
                            server.options.backlog);
    
    assert(server.ipfd != -1);
    ev_io_init(&(server.accept_watcher), accept_cb,server.ipfd, EV_READ);
    ev_io_start(server.loop,&(server.accept_watcher));
}

void createPidFile(void) {
    /* Try to write the pid file in a best-effort way. */
    FILE *fp = fopen(server.options.pidfile,"w");
    if (fp) {
        fprintf(fp,"%d\n",(int)getpid());
        fclose(fp);
    }
}

void daemonize(void) {
    int fd;
    if (fork() != 0) exit(0);
    setsid(); 
    if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) 
            close(fd);
    }
}

static void hup_on_signal(EV_P_ ev_signal *w, int revents)
{
    
}

static void exit_on_signal(EV_P_ ev_signal *w, int revents)
{
    char *msg;
    switch (w->signum) {
    case SIGINT:
        msg = "Received SIGINT scheduling shutdown...";
        break;
    case SIGTERM:
        msg = "Received SIGTERM scheduling shutdown...";
        break;
    default:
        msg = "Received shutdown signal, scheduling shutdown...";
    };
    log_info("%s\n",msg);
    ev_unloop(server.loop, EVUNLOOP_ALL);
}

int main(int argc, char **argv)
{
    memset(&server, 0, sizeof(sn_server));
    server.loading = 1;
    server.shutdown_server = 0;
    server.hz = 10;

    server.options =(sn_options){
        /* Global options */
        /* int verbose */  0,
        /* Socket options */
        /*int daemonize */ 0,
        /* char* pidfile */"/tmp/snowpear.pid",
        /* int port */    1997,
        /* char* host */  "0.0.0.0",
        /* int backlog */ 128,
        /* int maxclients */ 100000,
        /* INT tnum*/ get_ncpus()
    };
    
    sn_parse_options (&sn_cli, &(server.options), argc, argv);
    if (server.options.daemonize) daemonize();

    // ignore sigpipe signal when client closed
    ignore_sigpipe();
    server.loop = ev_default_loop (0);
    thread_init();
    
    if (server.options.daemonize) createPidFile();

    ev_signal       sigint;
    ev_signal       sighup;
    ev_signal       sigterm;
    
    ev_signal_init(&sigint, exit_on_signal, SIGINT);
    ev_signal_init(&sigterm, exit_on_signal, SIGTERM);
    ev_signal_init(&sighup, hup_on_signal, SIGHUP);

    ev_signal_start(EV_DEFAULT_ &sigint);
    ev_signal_start(EV_DEFAULT_ &sighup);
    ev_signal_start(EV_DEFAULT_ &sigterm);

    initServer();

    log_info("server start at %s:%d with %d workers",
             server.options.host,
             server.options.port,
             server.options.tnum)

    // now wait for events to arrive
    ev_loop(server.loop,0);
    ev_loop_destroy(server.loop);

    beforeShutdown();
    // unloop was called, so exit
    return 0;
}
