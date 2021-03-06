#include <stdio.h>
#include <sys/time.h>

#include <poll.h>
#include <errno.h>

#include <stdlib.h>
#include <unistd.h>
#include <cstring>
#include <sys/epoll.h>

#include "define.h"
#include "ae.h"

typedef struct aeApiState {
    int epfd;
    struct epoll_event *events;
} aeApiState;

static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = new aeApiState();
    
    if (!state) {
        goto err;
    }
    state->events = new struct epoll_event[eventLoop->setsize];
    if (!state->events) {
       goto err;
    }
    state->epfd = epoll_create(1024);  /* 1024 is just a hint for the kernel */
    if (state->epfd == -1) {
        goto err;
    }
    eventLoop->apidata = state;

    return 0;

err:
    if (state) {
        zfree(state->events);
        zfree(state);
    }

    return -1;
}

static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    aeApiState *state = eventLoop->apidata;

    auto new_events = new struct epoll_event[eventLoop->setsize];
    memcpy(state->events, new_events, sizeof(struct epoll_event) * setsize);    
    delete [] state->events;
    state->events = new_events;

    return 0;
}

static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;

    close(state->epfd);
    zfree(state->events);
    zfree(state);
}

static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = eventLoop->apidata;
    struct epoll_event ee = {0};    /* avoid valgrind warning */

    /* If the fd was already monitored for some event, we need a MOD
     * operation. Otherwise we need an ADD operation. */
    int op = eventLoop->events[fd].mask == AE_NONE ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    ee.events = 0;
    mask |= eventLoop->events[fd].mask; /* Merge old events */
    if (mask & AE_READABLE) {
        ee.events |= EPOLLIN;
    }
    if (mask & AE_WRITABLE) {
        ee.events |= EPOLLOUT;
    }

    ee.data.fd = fd;
    if (epoll_ctl(state->epfd, op, fd, &ee) == -1) {
        return -1;
    }

    return 0;
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int delmask) {
    aeApiState *state = eventLoop->apidata;
    struct epoll_event ee = {0}; /* avoid valgrind warning */
    int mask = eventLoop->events[fd].mask & (~delmask);

    ee.events = 0;
    if (mask & AE_READABLE) {
        ee.events |= EPOLLIN;
    }

    if (mask & AE_WRITABLE) {
        ee.events |= EPOLLOUT;
    }

    ee.data.fd = fd;
    if (mask != AE_NONE) {
        epoll_ctl(state->epfd, EPOLL_CTL_MOD, fd, &ee);
    } else {
        /* Note, Kernel < 2.6.9 requires a non null event pointer even for EPOLL_CTL_DEL. */
        epoll_ctl(state->epfd, EPOLL_CTL_DEL, fd, &ee);
    }
}

static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, numevents = 0;

    retval = epoll_wait(state->epfd, state->events, eventLoop->setsize,
                        tvp ? (tvp->tv_sec * 1000 + tvp->tv_usec / 1000) : -1);
    if (retval > 0) {
        numevents = retval;
        for (int i = 0; i < numevents; i++) {
            int mask = 0;
            struct epoll_event *e = state->events + i;

            if (e->events & EPOLLIN) {
                mask |= AE_READABLE;
            }
            if (e->events & EPOLLOUT) {
                mask |= AE_WRITABLE;
            }
            if (e->events & EPOLLERR) {
                mask |= AE_WRITABLE;
            }
            if (e->events & EPOLLHUP) {
                mask |= AE_WRITABLE;
            }
            eventLoop->fired[i].fd = e->data.fd;
            eventLoop->fired[i].mask = mask;
        }
    }

    return numevents;
}

int aeEventLoop::init(int size) {
    events = new aeFileEvent[size];
    fired = new aeFiredEvent[size];
    if (events == NULL || fired == NULL) {
        goto err;
    }

    setsize = size;
    lastTime = time(NULL);
    timeEventHead = NULL;
    timeEventNextId = 0;
    stop_ = 0;
    maxfd = -1;
    beforesleep = NULL;

    if (aeApiCreate(this) == -1) {
        goto err;
    }

    /* Events with mask == AE_NONE are not set. So let's initialize the vector with it. */
    for (int i = 0; i < size; i++) {
        events[i].mask = AE_NONE;
    }

    return 0;

err:
    zfree(events);
    zfree(fired);

    return -1;
}

void aeEventLoop::stop() {
    stop_ = 1;
}

/* Return the current set size. */
int aeEventLoop::aeGetSetSize() {
    return this->setsize;
}

/* Resize the maximum set size of the event loop.
 * If the requested set size is smaller than the current set size, but
 * there is already a file descriptor in use that is >= the requested
 * set size minus one, AE_ERR is returned and the operation is not
 * performed at all.
 *
 * Otherwise AE_OK is returned and the operation is successful. */
int aeEventLoop::aeResizeSetSize(int setsize) {
    if (setsize == this->setsize) {
        return AE_OK;
    }

    if (this->maxfd >= setsize) {
        return AE_ERR;
    }

    if (aeApiResize(this, setsize) == -1) {
        return AE_ERR;
    }

    auto new_events = new aeFileEvent[setsize];
    memcpy(this->events, new_events, sizeof(struct aeFileEvent) * setsize);    
    delete [] this->events;
    this->events = new_events;

    auto new_fired = new aeFiredEvent[setsize];
    memcpy(this->fired, new_fired, sizeof(struct aeFiredEvent) * setsize);    
    delete [] this->fired;
    this->fired = new_fired;

    this->setsize = setsize;

    /* Make sure that if we created new slots, they are initialized with an AE_NONE mask. */
    for (int i = this->maxfd + 1; i < setsize; i++) {
        this->events[i].mask = AE_NONE;
    }

    return AE_OK;
}

aeEventLoop::~aeEventLoop() {
    aeApiFree(this);
    zfree(events);
    zfree(fired);
}

int aeEventLoop::aeCreateFileEvent(int fd, int mask, aeFileProc *proc, void *sessionData) {
    if (fd >= this->setsize) {
        errno = ERANGE;
        return AE_ERR;
    }
    aeFileEvent *fe = &this->events[fd];

    if (aeApiAddEvent(this, fd, mask) == -1) {
        return AE_ERR;
    }

    fe->mask |= mask;
    if (mask & AE_READABLE) {
        fe->rfileProc = proc;
    }

    if (mask & AE_WRITABLE) {
        fe->wfileProc = proc;
    }

    fe->sessionData = sessionData;
    if (fd > this->maxfd) {
        this->maxfd = fd;
    }

    return AE_OK;
}

void aeEventLoop::aeDeleteFileEvent(int fd, int mask) {
    if (fd >= this->setsize) {
        return;
    }

    aeFileEvent *fe = &this->events[fd];
    if (fe->mask == AE_NONE) {
        return;
    }

    aeApiDelEvent(this, fd, mask);

    fe->mask &= ~mask;
    if (fd == this->maxfd && fe->mask == AE_NONE) {
        /* Update the max fd */
        int j;
        for (j = this->maxfd - 1; j >= 0; j--) {
            if (this->events[j].mask != AE_NONE) {
                break;
            }
        }
        this->maxfd = j;
    }
}

int aeEventLoop::aeGetFileEvents( int fd) {
    if (fd >= this->setsize) {
        return 0;
    }

    aeFileEvent *fe = &this->events[fd];

    return fe->mask;
}

static void aeGetTime(long *seconds, long *milliseconds) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec / 1000;
}

static void aeAddMillisecondsToNow(int64_t milliseconds, long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;

    aeGetTime(&cur_sec, &cur_ms);
    when_sec = cur_sec + milliseconds / 1000;
    when_ms = cur_ms + milliseconds % 1000;
    if (when_ms >= 1000) {
        when_sec++;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms = when_ms;
}

int64_t aeEventLoop::aeCreateTimeEvent(int64_t milliseconds,
                            aeTimeProc *proc, void *sessionData, aeEventFinalizerProc *finalizerProc) {
    int64_t id = this->timeEventNextId++;
    aeTimeEvent *te = new aeTimeEvent;
    if (te == NULL) {
        return AE_ERR;
    }

    te->id = id;
    aeAddMillisecondsToNow(milliseconds, &te->when_sec, &te->when_ms);
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    te->sessionData = sessionData;

    te->next = this->timeEventHead;
    this->timeEventHead = te;

    return id;
}

int aeEventLoop::aeDeleteTimeEvent(int64_t id) {
    aeTimeEvent *te = this->timeEventHead;
    while (te) {
        if (te->id == id) {
            te->id = AE_DELETED_EVENT_ID;
            return AE_OK;
        }
        te = te->next;
    }

    return AE_ERR; /* NO event with the specified ID found */
}

/* Search the first timer to fire.
 * This operation is useful to know how many time the select can be
 * put in sleep without to delay any event.
 * If there are no timers NULL is returned.
 *
 * Note that's O(N) since time events are unsorted.
 * Possible optimizations (not needed by Redis so far, but...):
 * 1) Insert the event in order, so that the nearest is just the head.
 *    Much better but still insertion or deletion of timers is O(N).
 * 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
 */
static aeTimeEvent *aeSearchNearestTimer(aeEventLoop *eventLoop) {
    aeTimeEvent *te = eventLoop->timeEventHead;
    aeTimeEvent *nearest = NULL;

    while (te) {
        if (!nearest || te->when_sec < nearest->when_sec ||
            (te->when_sec == nearest->when_sec && te->when_ms < nearest->when_ms)) {
            nearest = te;
        }
        te = te->next;
    }

    return nearest;
}

/* Process time events */
static int processTimeEvents(aeEventLoop *eventLoop) {
    int processed = 0;
    aeTimeEvent *te, *prev;
    int64_t maxId;
    time_t now = time(NULL);

    /* If the system clock is moved to the future, and then set back to the
     * right value, time events may be delayed in a random way. Often this
     * means that scheduled operations will not be performed soon enough.
     *
     * Here we try to detect system clock skews, and force all the time
     * events to be processed ASAP when this happens: the idea is that
     * processing events earlier is less dangerous than delaying them
     * indefinitely, and practice suggests it is. */
    if (now < eventLoop->lastTime) {
        te = eventLoop->timeEventHead;
        while (te) {
            te->when_sec = 0;
            te = te->next;
        }
    }
    eventLoop->lastTime = now;

    prev = NULL;
    te = eventLoop->timeEventHead;
    maxId = eventLoop->timeEventNextId - 1;
    while (te) {
        long now_sec, now_ms;
        int64_t id;

        /* Remove events scheduled for deletion. */
        if (te->id == AE_DELETED_EVENT_ID) {
            aeTimeEvent *next = te->next;
            if (prev == NULL) {
                eventLoop->timeEventHead = te->next;
            } else {
                prev->next = te->next;
            }

            if (te->finalizerProc) {
                te->finalizerProc(eventLoop, te->sessionData);
            }

            zfree(te);
            te = next;
            continue;
        }

        /* Make sure we don't process time events created by time events in
         * this iteration. Note that this check is currently useless: we always
         * add new timers on the head, however if we change the implementation
         * detail, this check may be useful again: we keep it here for future
         * defense. */
        if (te->id > maxId) {
            te = te->next;
            continue;
        }
        aeGetTime(&now_sec, &now_ms);
        if (now_sec > te->when_sec || (now_sec == te->when_sec && now_ms >= te->when_ms)) {
            int retval;

            id = te->id;
            retval = te->timeProc(eventLoop, id, te->sessionData);
            processed++;
            if (retval != AE_NOMORE) {
                aeAddMillisecondsToNow(retval, &te->when_sec, &te->when_ms);
            } else {
                te->id = AE_DELETED_EVENT_ID;
            }
        }
        prev = te;
        te = te->next;
    }

    return processed;
}

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurs (if any).
 *
 * If flags is 0, the function does nothing and returns.
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 *
 * The function returns the number of events processed. */
int aeEventLoop::aeProcessEvents(int flags) {
    int processed = 0, numevents;

    /* Nothing to do? return ASAP */
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) {
        return 0;
    }

    /* Note that we want call select() even if there are no
     * file events to process as long as we want to process time
     * events, in order to sleep until the next time event is ready
     * to fire. */
    if (this->maxfd != -1 || ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
        aeTimeEvent *shortest = NULL;
        struct timeval tv, *tvp;

        if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT)) {
            shortest = aeSearchNearestTimer(this);
        }

        if (shortest) {
            long now_sec, now_ms;
            aeGetTime(&now_sec, &now_ms);
            tvp = &tv;

            /* How many milliseconds we need to wait for the next time event to fire? */
            int64_t ms = (shortest->when_sec - now_sec) * 1000 + shortest->when_ms - now_ms;

            if (ms > 0) {
                tvp->tv_sec = ms / 1000;
                tvp->tv_usec = (ms % 1000) * 1000;
            } else {
                tvp->tv_sec = 0;
                tvp->tv_usec = 0;
            }
        } else {
            /* If we have to check for events but need to return
             * ASAP because of AE_DONT_WAIT we need to set the timeout to zero */
            if (flags & AE_DONT_WAIT) {
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                /* Otherwise we can block */
                tvp = NULL; /* wait forever */
            }
        }

        numevents = aeApiPoll(this, tvp);
        for (int i = 0; i < numevents; i++) {
            aeFileEvent *fe = &this->events[this->fired[i].fd];
            int mask = this->fired[i].mask;
            int fd = this->fired[i].fd;

            int rfired = 0;
            /* note the fe->mask & mask & ... code: maybe an already processed
             * event removed an element that fired and we still didn't
             * processed, so we check if the event is still valid. */
            if (fe->mask & mask & AE_READABLE) {
                rfired = 1;
                fe->rfileProc(this, fd, fe->sessionData, mask);
            }
            if (fe->mask & mask & AE_WRITABLE) {
                if (!rfired || fe->wfileProc != fe->rfileProc) {
                    fe->wfileProc(this, fd, fe->sessionData, mask);
                }
            }
            processed++;
        }
    }
    /* Check time events */
    if (flags & AE_TIME_EVENTS) {
        processed += processTimeEvents(this);
    }

    return processed; /* return the number of processed file/time events */
}


void aeEventLoop::aeMain() {
    this->stop_ = 0;
    while (!this->stop_) {
        if (this->beforesleep) {
            this->beforesleep();
        }
        this->aeProcessEvents(AE_ALL_EVENTS);
    }
}


void aeEventLoop::aeSetBeforeSleepProc(aeBeforeSleepProc *beforesleep) {
    this->beforesleep = beforesleep;
}
