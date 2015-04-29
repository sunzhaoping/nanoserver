#ifndef SN_SERVER_HEADER
#define SN_SERVER_HEADER
#include <ev.h>
#include "chan.h"
#include "hashmap.h"

struct msg_t;

typedef struct {
    pthread_t thread_id;               /* unique ID of this thread */
    struct ev_loop *loop;              /* libev loop this thread uses */
    struct ev_async async_watcher;     /* async watcher for new connect */
    map_t pubsub;
    chan_t * chan;
} WORK_THREAD;

typedef struct sn_options {
    int verbose;
    int daemonize;
    char* pidfile;
    int port;
    char * host;
    int backlog;
    int maxclients;
    int tnum;
} sn_options;

typedef struct sn_client{
    int fd;
    char szAddr[16];
    int port;
    uint32_t cookie;
    queue_t* input;
    queue_t* output;
    struct msg_t * msg;
    struct ev_loop * loop;
    struct ev_io io_wather;
    int32_t channel_id;
    int state;
    int dispatch_sub;
    WORK_THREAD* thread;
} sn_client;

typedef struct sn_server {
    struct ev_loop * loop;
    sn_options options;
    long long mstime;
    long long unixtime;
    int hz;
    int ipfd;
    int header_size;
    int shutdown_server;
    int loading;
    pthread_t thread_id;               /* unique ID of this thread */
    struct ev_io accept_watcher;       /* accept watcher for new connect */
} sn_server;


void *worker_libev(void *arg);
void create_worker(void *(*func)(void *), void *arg);
void async_cb(EV_P_ ev_async *w, int revents);
void setup_thread(WORK_THREAD *self);
void client_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
int process_message(struct ev_loop *loop, struct ev_io *watcher, sn_client * client);
//void pub_cb(EV_P_ ev_async *w, int revents);
void thread_init();

/*
 * Each libev instance has a async_watcher, which other threads
 * can use to signal that they've put a new connection on its queue.
 */
WORK_THREAD *work_threads;

sn_client *client_create(int fd);
void client_destroy(sn_client * client);
sn_server server;
#endif