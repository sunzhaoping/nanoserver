#ifndef subscriptions_h
#define subscriptions_h

struct sn_client;

// Frame is an atomic message, consisting of a protocol, size, and body.
typedef struct header_t
{
    uint8_t  proto;
    uint32_t  cookie;
    uint32_t  size;
    
} header_t;

#define HEADER_SIZE 9

#define MSG_READ_HEADER 0
#define MSG_READ_BODY 1
#define MSG_READ_OK 2
#define MSG_MAX_SIZE 1024
#define PROTO_LOGIN 0x10
#define PROTO_PUB   0x20
#define PROTO_SUB   0x30
#define PROTO_UNSUB 0x40
#define PROTO_PING  0x50

typedef struct msg_t
{
    header_t  header;
    size_t    read_size;
    void*     creator;
    int       state;
    int       refcount;
    uint8_t*  body;
} msg_t;

// Registers a topic subscription for a peer. Returns 0 on success, -1 on
// failure.
int subscribe(struct sn_client* self, uint32_t channel_id);

// Unregisters a topic subscription for a peer. Returns 0 on success, -1 on
// failure.
int unsubscribe(struct sn_client* self, uint32_t channel_id);

// Publishes the given message to all peers subscribing to the message topic.
// Returns -1 on failure, 0 on success.
int publish(struct sn_client* self,uint32_t channel_id, msg_t*msg);

msg_t* msg_init();
void msg_retain(msg_t*);
void msg_free(msg_t*);

#endif
