#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "server.h"
#include "pubsub.h"
#include "hashmap.h"
#include "chan.h"

// Registers a topic subscription for a peer. Returns -1 on failure, 0 on
// success.
int subscribe(struct sn_client * self, uint32_t channel_id)
{
    int err = 0;
    map_t *subscribers;
    if(hashmap_get(self->thread->pubsub, channel_id, (void**) &subscribers) != MAP_OK)
    {
        subscribers = hashmap_new();
        if (!subscribers)
        {
            return -1;
        }
        hashmap_put(self->thread->pubsub, channel_id, subscribers);
    }
    if (hashmap_put(subscribers, self->fd, self) != MAP_OK)
    {
        err = -1;
    }
    return err;
}

// Unregisters a topic subscription for a peer. Returns -1 on failure, 0 on
// success.
int unsubscribe(struct sn_client* self,uint32_t channel_id)
{
    int err = 0;
    char fd_str[10];
    sprintf(fd_str, "%d", self->fd);
    map_t* subscribers;
    if (hashmap_get(self->thread->pubsub, channel_id, (void**) &subscribers) == MAP_OK)
    {
        hashmap_remove(subscribers, self->fd);
        if(hashmap_length(subscribers)<=0){
            hashmap_free(subscribers);
            hashmap_remove(self->thread->pubsub,channel_id);
        };
    }
    else
    {
        err = -1;
    }
    return err;
    
}

int send_msg(void *item, void *data){
    sn_client * client = (sn_client*)data;
    msg_t* msg = (msg_t*)item;
    if(client == msg->creator)
        return MAP_OK;
    msg->header.cookie = client->cookie;
    if(queue_add(client->output, msg) == 0){
        msg_retain(msg);
        ev_io_stop(client->loop, &client->io_wather);
        ev_io_set(&client->io_wather,client->fd, EV_WRITE);
        ev_io_start(client->loop,&client->io_wather);
    }
    return MAP_OK;
}

// Publishes the given message to all peers subscribing to the message topic.
// Returns -1 on failure, 0 on success.
int publish(struct sn_client* self,uint32_t channel_id, msg_t* msg)
{
    map_t* subscribers;
    if (hashmap_get(self->thread->pubsub, channel_id, (void**) &subscribers) == MAP_OK)
    {
        hashmap_iterate(subscribers, send_msg ,msg);
    }else
        return -1;
  
    return 0;
}

msg_t* msg_init(){
    msg_t * msg = malloc(sizeof(msg_t));
    msg->state = MSG_READ_HEADER;
    msg->read_size = 0;
    msg->body = NULL;
    msg->refcount = 0;
    memset(&msg->header, 0, sizeof(header_t));
    return msg;
}

void msg_retain(msg_t *msg){
    msg->refcount++;
}

void msg_free(msg_t* msg){
    msg->refcount--;
    if(msg->refcount <= 0){
        if(msg->body != NULL)
            free(msg->body);
        free(msg);
    }
}
