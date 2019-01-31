#ifndef ANET_AE_H
#define ANET_AE_H

#include <time.h>

#define AE_OK        0
#define AE_ERR      -1

#define AE_NONE      0
#define AE_READABLE  1
#define AE_WRITABLE  2

#define AE_FILE_EVENTS  1
#define AE_TIME_EVENTS  2
#define AE_ALL_EVENTS    (AE_FILE_EVENTS|AE_TIME_EVENTS)
#define AE_DONT_WAIT    4

#define AE_NOMORE            -1
#define AE_DELETED_EVENT_ID  -1

/* Macros */
#define AE_NOTUSED(V) ((void) V)

struct aeEventLoop;
struct aeApiState;
/* Types and data structures */
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *sessionData, int mask);

typedef int aeTimeProc(struct aeEventLoop *eventLoop, int64_t id, void *sessionData);

typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *sessionData);

typedef void aeBeforeSleepProc();

/* File event structure */
typedef struct aeFileEvent {
    int mask;       /* one of AE_(READABLE|WRITABLE) */
    aeFileProc *rfileProc;
    aeFileProc *wfileProc;
    void *sessionData;
} aeFileEvent;

/* Time event structure */
typedef struct aeTimeEvent {
    int64_t id;       /* time event identifier. */
    long when_sec;      /* seconds */
    long when_ms;       /* milliseconds */
    aeTimeProc *timeProc;
    aeEventFinalizerProc *finalizerProc;
    void *sessionData;
    struct aeTimeEvent *next;
} aeTimeEvent;

/* A fired event */
typedef struct aeFiredEvent {
    int fd;
    int mask;
} aeFiredEvent;

/* State of an event based program */
class aeEventLoop final{
public:
    int init(int size);
    void stop();

    int aeCreateFileEvent(int fd, int mask, aeFileProc* proc, void* sessionData);

    int aeGetFileEvents(int fd);
    void aeDeleteFileEvent(int fd, int mask);

    int aeGetSetSize();

    int aeResizeSetSize(int setsize);

    int64_t aeCreateTimeEvent(int64_t milliseconds, aeTimeProc *proc, void *sessionData, aeEventFinalizerProc *finalizerProc);

    int aeDeleteTimeEvent(int64_t id);

    int aeProcessEvents(int flags);




void aeMain();
void aeSetBeforeSleepProc(aeBeforeSleepProc *beforesleep);

 ~aeEventLoop();

    int maxfd; /* highest file descriptor currently registered */
    int setsize; /* max number of file descriptors tracked */
    int64_t timeEventNextId;
    time_t lastTime; /* Used to detect system clock skew */
    aeFileEvent* events; /* Registered events */
    aeFiredEvent* fired; /* Fired events */
    aeTimeEvent* timeEventHead;
    int stop_;
    aeApiState* apidata; /* This is used for polling API specific data */
    aeBeforeSleepProc* beforesleep;
};

#endif //ANET_AE_H
