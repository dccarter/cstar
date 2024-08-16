#ifndef __AE_H__
#define __AE_H__

#include <time.h>

enum Status {
    AE_OK = 0,
    AE_ERR = -1,
    AE_NO_MORE = -1,
    AE_DELETED_EVENT_ID = -1
};

enum State {
    AE_NONE = 0,     /* No events registered. */
    AE_READABLE = 1, /* Fire when descriptor is readable. */
    AE_WRITABLE = 2, /* Fire when descriptor is writable. */
    AE_TIMEOUT = 4,  /* Fire when waiting for event times out */
    AE_BARRIER = 8,
    /*
     * With WRITABLE, never fire the event if the
     * READABLE event already fired in the same event
     *     loop iteration. Useful when you want to persist
     *     things to disk before sending replies, and want
     *     to do that in a group fashion.
     */
};

enum Flags {
    AE_FILE_EVENTS = 1,
    AE_TIME_EVENTS = 2,
    AE_ALL_EVENTS = (AE_FILE_EVENTS | AE_TIME_EVENTS),
    AE_DONT_WAIT = 4,
    AE_CALL_AFTER_SLEEP = 8,
};

/* Macros */
#define AE_NOTUSED(V) ((void)V)

struct aeEventLoop;

/* Types and data structures */
typedef void aeFileProc(struct aeEventLoop *eventLoop,
                        int fd,
                        void *clientData,
                        int mask);

typedef enum Status aeTimeProc(struct aeEventLoop *eventLoop,
                               long long id,
                               void *clientData);

typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop,
                                  void *clientData);

typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);

#define AE_TIMER_BASE(T)                                                       \
    u_int64_t id;  /* time event identifier. */                                \
    long when_sec; /* seconds */                                               \
    long when_ms;  /* milliseconds */                                          \
    T *prev;                                                                   \
    T *next;                                                                   \
    int fd;

/* File event structure */
typedef struct aeFileEvent {
    struct {
        AE_TIMER_BASE(struct aeFileEvent)
    } timer;
    int mask; /* one of AE_(READABLE|WRITABLE|BARRIER) */
    aeFileProc *rfileProc;
    aeFileProc *wfileProc;
    void *clientData;
} aeFileEvent;

/* Time event structure */
typedef struct aeTimeEvent {
    AE_TIMER_BASE(struct aeTimeEvent);
    aeTimeProc *timeProc;
    aeEventFinalizerProc *finalizerProc;
    void *clientData;
} aeTimeEvent;

#undef AE_TIMER_BASE

/* A fired event */
typedef struct aeFiredEvent {
    int fd;
    int mask;
} aeFiredEvent;

/* State of an event based program */
typedef struct aeEventLoop {
    int maxfd;   /* highest file descriptor currently registered */
    int setsize; /* max number of file descriptors tracked */
    long long timeEventNextId;
    time_t lastTime;     /* Used to detect system clock skew */
    aeFileEvent *events; /* Registered events */
    aeFiredEvent *fired; /* Fired events */
    aeTimeEvent *timeEventHead;
    int stop;
    void *context;
    void *apidata; /* This is used for polling API specific data */
    aeBeforeSleepProc *beforesleep;
    aeBeforeSleepProc *aftersleep;
} aeEventLoop;

/* Prototypes */
aeEventLoop *aeCreateEventLoop(void *context, int setsize);

void aeDeleteEventLoop(aeEventLoop *eventLoop);

void aeStop(aeEventLoop *eventLoop);

enum Status aeCreateFileEvent(aeEventLoop *eventLoop,
                              int fd,
                              enum State mask,
                              aeFileProc *proc,
                              void *clientData,
                              u_int64_t timeout);

void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, enum State mask);

int aeGetFileEvents(aeEventLoop *eventLoop, int fd);

enum Status aeCreateTimeEvent(aeEventLoop *eventLoop,
                              long long milliseconds,
                              aeTimeProc *proc,
                              void *clientData,
                              aeEventFinalizerProc *finalizerProc);

int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id);

int aeProcessEvents(aeEventLoop *eventLoop, enum Flags flags, int64_t timeout);

int aeWait(int fd, enum Flags mask, long long milliseconds);

void aeMain(aeEventLoop *eventLoop);

char *aeGetApiName(void);

void aeSetBeforeSleepProc(aeEventLoop *eventLoop,
                          aeBeforeSleepProc *beforesleep);

void aeSetAfterSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *aftersleep);

int aeGetSetSize(aeEventLoop *eventLoop);

int aeResizeSetSize(aeEventLoop *eventLoop, int setsize);

void aeGetTime(long *seconds, long *milliseconds);

int64_t aeOsTime();

#endif
