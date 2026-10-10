#ifndef _GNU_SOURCE
#define _GNU_SOURCE  /* MSG_NOSIGNAL, MSG_TRUNC, SOCK_SEQPACKET */
#endif

#include "actor.h"
#include "platform.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
/* =========================================================================
 * WINDOWS: AF_UNIX STREAM SOCKETS WITH AN EXPLICIT LENGTH PREFIX.
 *
 * POSIX gets three properties from one SOCK_SEQPACKET socketpair: one send is
 * one whole frame, any number of senders share one write end without
 * interleaving, and a descriptor can ride along (SCM_RIGHTS). Windows' AF_UNIX
 * is STREAM ONLY and passes no descriptors, so each property is rebuilt:
 *
 *   FRAMES. Every frame carries [u32 payload length][u32 handle count], then
 *   the payload, then each handle as [u32 length][path]. The reader reassembles
 *   per connection. A stream cannot lose a boundary that is written down.
 *
 *   NO INTERLEAVING. An inbox is a LISTENER at a path, and every sending
 *   PROCESS opens its own connection to it -- so each connection has exactly
 *   one writer and nothing ever interleaves. All handles a process holds to one
 *   inbox share that one connection (peer_get caches it by path), which keeps
 *   what one process sends to one actor in the order it was sent, as on POSIX.
 *
 *   HANDLES TRAVEL AS PATHS. A handle is a capability to write to an inbox,
 *   and on Windows that capability is the inbox's name. The receiver connects
 *   to it itself, so there is nothing to duplicate between processes and no
 *   DuplicateHandle. The inbox lives in the user's own temporary directory,
 *   which other users cannot open.
 *
 * MEASURED on Windows 11 26200 before this was written (the probe is recorded
 * in docs/windows_port_status.md §22): a client may connect before the
 * listener accepts; a non-blocking send was accepted WHOLE or refused with
 * WSAEWOULDBLOCK and nothing written (no partial send seen up to 8 MiB) --
 * a partial send is still handled, by finishing the frame, since nothing
 * documents the property; an actor that exits OR IS KILLED shows POLLHUP on
 * every connection to it; and a send to a closed peer REPORTS SUCCESS once,
 * which is why a send checks for the hang-up first. The socket FILE survives
 * its process, so the inbox deletes it on close.
 * ========================================================================= */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

/* No SO_SNDBUF to derive this from: the frame is ours. A fixed, documented
 * size, above the 64 KiB floor every platform promises. */
#define ACTOR_WIN_MAX_MESSAGE ((size_t)4 * 1024 * 1024)
/* A frame header claiming more than this is not a frame (corrupt stream). */
#define ACTOR_WIN_MAX_PATH 4096

#define WIN_INBOX_ID 0      /* Mailbox.read_fd for the process's one inbox */

/* ---- outbound: connections to other inboxes, and the handles that use them */

typedef struct WinPeer {
    char *path;
    SOCKET s;               /* INVALID_SOCKET: the inbox was not there */
    int refs;               /* handles using this connection */
    struct WinPeer *next;
} WinPeer;

static WinPeer *peers = NULL;
static WinPeer **handle_slots = NULL;   /* handle id -> peer (NULL: free) */
static size_t handle_slot_count = 0;

static int net_ready(void) {
    return gb_net_init();
}

static void sockaddr_for(struct sockaddr_un *a, const char *path) {
    memset(a, 0, sizeof *a);
    a->sun_family = AF_UNIX;
    strncpy(a->sun_path, path, sizeof a->sun_path - 1);
}

/* A PROCESS THAT ENDS WITH A CONNECTION OPEN CAN LOSE WHAT IT SENT. Measured
 * with a C probe (Windows 11 26200): a sender that wrote 100,000 bytes and
 * exited with its socket open delivered NONE of them -- the receiver got
 * WSAECONNRESET -- while one that called shutdown(SD_SEND) and closesocket
 * first delivered all 100,000.
 *
 * gBASIC DOES NOT HIT THIS TODAY, also measured: an actor that returns, dies
 * of a runtime error, or calls exit(n) runs the interpreter's teardown, which
 * frees every handle and so closes every connection gracefully
 * (peer_release). With this hook removed, nothing was lost on any of those
 * paths, whether or not the receiver was reading at the time. The hook is
 * DEFENCE IN DEPTH for any exit that skips teardown: it costs one atexit and
 * makes the loss impossible rather than merely not reached. A process KILLED
 * or crashing can still lose what is unread on its side; POSIX's datagrams
 * would survive that, and the difference is in docs/windows_port_status.md. */
static void peers_close_gracefully(void) {
    for (WinPeer *p = peers; p; p = p->next) {
        if (p->s != INVALID_SOCKET) {
            shutdown(p->s, SD_SEND);
            closesocket(p->s);
            p->s = INVALID_SOCKET;
        }
    }
}

static WinPeer *peer_get(const char *path) {
    static int exit_hook = 0;
    if (!exit_hook) {
        atexit(peers_close_gracefully);
        exit_hook = 1;
    }
    for (WinPeer *p = peers; p; p = p->next) {
        if (strcmp(p->path, path) == 0) {
            return p;
        }
    }
    WinPeer *p = calloc(1, sizeof *p);
    char *copy = p ? malloc(strlen(path) + 1) : NULL;
    if (!copy) {
        free(p);
        return NULL;
    }
    strcpy(copy, path);
    p->path = copy;
    p->s = INVALID_SOCKET;
    if (net_ready() && strlen(path) < sizeof(((struct sockaddr_un *)0)->sun_path)) {
        SOCKET s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s != INVALID_SOCKET) {
            struct sockaddr_un a;
            sockaddr_for(&a, path);
            SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0);
            if (connect(s, (struct sockaddr *)&a, sizeof a) == 0) {
                u_long nb = 1;
                ioctlsocket(s, FIONBIO, &nb);
                p->s = s;
            } else {
                closesocket(s);
            }
        }
    }
    p->next = peers;
    peers = p;
    return p;
}

static void peer_release(WinPeer *p) {
    if (--p->refs > 0) {
        return;
    }
    for (WinPeer **pp = &peers; *pp; pp = &(*pp)->next) {
        if (*pp == p) {
            *pp = p->next;
            break;
        }
    }
    if (p->s != INVALID_SOCKET) {
        shutdown(p->s, SD_SEND);    /* graceful: what was sent still arrives */
        closesocket(p->s);
    }
    free(p->path);
    free(p);
}

static int slot_new(WinPeer *p) {
    for (size_t i = 0; i < handle_slot_count; i++) {
        if (!handle_slots[i]) {
            handle_slots[i] = p;
            p->refs++;
            return (int)i + 1;
        }
    }
    WinPeer **grown = realloc(handle_slots, sizeof(WinPeer *) * (handle_slot_count + 1));
    if (!grown) {
        return -1;
    }
    handle_slots = grown;
    handle_slots[handle_slot_count++] = p;
    p->refs++;
    return (int)handle_slot_count;   /* ids start at 1: 0 is the inbox */
}

static WinPeer *slot_peer(int handle) {
    if (handle < 1 || (size_t)handle > handle_slot_count) {
        return NULL;
    }
    return handle_slots[handle - 1];
}

int channel_handle_adopt_path(const char *path) {
    WinPeer *p = peer_get(path);
    if (!p) {
        return -1;
    }
    int id = slot_new(p);
    if (id < 0 && p->refs == 0) {
        p->refs = 1;
        peer_release(p);
    }
    return id;
}

int channel_handle_dup(int handle) {
    WinPeer *p = slot_peer(handle);
    return p ? slot_new(p) : -1;
}

void channel_handle_close(int handle) {
    WinPeer *p = slot_peer(handle);
    if (!p) {
        return;
    }
    handle_slots[handle - 1] = NULL;
    peer_release(p);
}

static int socket_hung_up(SOCKET s) {
    if (s == INVALID_SOCKET) {
        return 1;
    }
    WSAPOLLFD p = { s, 0, 0 };
    int r = WSAPoll(&p, 1, 0);
    return r > 0 && (p.revents & (POLLHUP | POLLERR | POLLNVAL));
}

int channel_handle_hung_up(int handle) {
    WinPeer *p = slot_peer(handle);
    return !p || socket_hung_up(p->s);
}

int channel_handle_pollfd(int handle) {
    WinPeer *p = slot_peer(handle);
    return (p && p->s != INVALID_SOCKET) ? (int)p->s : -1;
}

size_t channel_max_message(int write_fd) {
    (void)write_fd;
    return ACTOR_WIN_MAX_MESSAGE;
}

static void put_u32(unsigned char *at, uint32_t v) {
    memcpy(at, &v, 4);
}

static uint32_t get_u32(const unsigned char *at) {
    uint32_t v;
    memcpy(&v, at, 4);
    return v;
}

/* Send all of `len` bytes, the first call non-blocking. Returns OK, FULL (the
 * first send was refused and NOTHING was written, so the stream is intact), or
 * ERROR. Once any byte of a frame is out, the rest must follow or the stream
 * is corrupt, so a partial send is finished by waiting for room. */
static int send_frame_bytes(SOCKET s, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        size_t want = len - sent;
        int n = send(s, data + sent, want > 0x40000000 ? 0x40000000 : (int)want, 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        int e = WSAGetLastError();
        if (e != WSAEWOULDBLOCK) {
            return ACTOR_CHANNEL_ERROR;
        }
        if (sent == 0) {
            return ACTOR_CHANNEL_FULL;
        }
        WSAPOLLFD p = { s, POLLWRNORM, 0 };
        if (WSAPoll(&p, 1, -1) < 0 || (p.revents & (POLLHUP | POLLERR | POLLNVAL))) {
            return ACTOR_CHANNEL_ERROR;
        }
    }
    return ACTOR_CHANNEL_OK;
}

int channel_send(int write_fd, const void *bytes, size_t len) {
    return channel_send_fds(write_fd, bytes, len, NULL, 0);
}

int channel_send_fds(int write_fd, const void *bytes, size_t len,
                     const int *fds, size_t nfds) {
    if (len > ACTOR_WIN_MAX_MESSAGE || nfds > ACTOR_MAX_MESSAGE_FDS) {
        return ACTOR_CHANNEL_TOOBIG;
    }
    WinPeer *target = slot_peer(write_fd);
    /* A send to a closed peer is reported as success once (measured), so the
     * hang-up is asked first: the POSIX answer is "no longer reachable". */
    if (!target || socket_hung_up(target->s)) {
        return ACTOR_CHANNEL_ERROR;
    }
    size_t total = 8 + len;
    for (size_t i = 0; i < nfds; i++) {
        WinPeer *h = slot_peer(fds[i]);
        if (!h) {
            return ACTOR_CHANNEL_ERROR;
        }
        total += 4 + strlen(h->path);
    }
    unsigned char *frame = malloc(total);
    if (!frame) {
        return ACTOR_CHANNEL_ERROR;
    }
    put_u32(frame, (uint32_t)len);
    put_u32(frame + 4, (uint32_t)nfds);
    memcpy(frame + 8, bytes, len);
    size_t at = 8 + len;
    for (size_t i = 0; i < nfds; i++) {
        const char *path = slot_peer(fds[i])->path;
        size_t pl = strlen(path);
        put_u32(frame + at, (uint32_t)pl);
        memcpy(frame + at + 4, path, pl);
        at += 4 + pl;
    }
    int rc = send_frame_bytes(target->s, (const char *)frame, total);
    free(frame);
    return rc;
}

/* ---- inbound: this process's ONE inbox ------------------------------------ */

typedef struct WinFrame {
    void *bytes;            /* NULL: a corrupt frame, reported as an error */
    size_t len;
    char **paths;
    size_t npaths;
    struct WinFrame *next;
} WinFrame;

typedef struct {
    SOCKET s;
    unsigned char *buf;
    size_t len, cap;
} WinConn;

static struct {
    int open;
    SOCKET listener;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    WinConn *conns;
    size_t nconns;
    size_t next_read;       /* round-robin start, so no sender starves another */
    int startup_only;       /* read only conns[startup_index] until its first frame */
    size_t startup_index;   /* 1: conns[0] is this process's own self() connection */
    WinFrame *head, *tail;
} inbox;

static void frame_free(WinFrame *f) {
    for (size_t i = 0; i < f->npaths; i++) {
        free(f->paths[i]);
    }
    free(f->paths);
    free(f->bytes);
    free(f);
}

static void frame_push(WinFrame *f) {
    f->next = NULL;
    if (inbox.tail) {
        inbox.tail->next = f;
    } else {
        inbox.head = f;
    }
    inbox.tail = f;
}

static void conn_drop(size_t i) {
    closesocket(inbox.conns[i].s);
    free(inbox.conns[i].buf);
    memmove(&inbox.conns[i], &inbox.conns[i + 1],
            sizeof(WinConn) * (inbox.nconns - i - 1));
    inbox.nconns--;
    if (inbox.next_read > i && inbox.next_read > 0) {
        inbox.next_read--;
    }
}

/* Take one whole frame off the front of a connection's buffer. 1 taken, 0 not
 * complete yet, -1 the stream is not frames (the connection is dropped and a
 * corrupt frame queued, so the receiver hears about it rather than waiting). */
static int conn_take_frame(WinConn *c) {
    if (c->len < 8) {
        return 0;
    }
    uint32_t plen = get_u32(c->buf);
    uint32_t nh = get_u32(c->buf + 4);
    if (plen > ACTOR_WIN_MAX_MESSAGE || nh > ACTOR_MAX_MESSAGE_FDS) {
        return -1;
    }
    size_t at = 8 + (size_t)plen;
    for (uint32_t i = 0; i < nh; i++) {
        if (c->len < at + 4) {
            return 0;
        }
        uint32_t pl = get_u32(c->buf + at);
        if (pl == 0 || pl > ACTOR_WIN_MAX_PATH) {
            return -1;
        }
        at += 4 + pl;
    }
    if (c->len < at) {
        return 0;
    }
    WinFrame *f = calloc(1, sizeof *f);
    void *payload = malloc(plen ? plen : 1);
    char **paths = nh ? calloc(nh, sizeof(char *)) : NULL;
    if (!f || !payload || (nh && !paths)) {
        free(f);
        free(payload);
        free(paths);
        return -1;
    }
    memcpy(payload, c->buf + 8, plen);
    f->bytes = payload;
    f->len = plen;
    f->paths = paths;
    size_t pos = 8 + (size_t)plen;
    for (uint32_t i = 0; i < nh; i++) {
        uint32_t pl = get_u32(c->buf + pos);
        char *p = malloc(pl + 1);
        if (!p) {
            frame_free(f);
            return -1;
        }
        memcpy(p, c->buf + pos + 4, pl);
        p[pl] = '\0';
        f->paths[f->npaths++] = p;
        pos += 4 + pl;
    }
    memmove(c->buf, c->buf + at, c->len - at);
    c->len -= at;
    frame_push(f);
    return 1;
}

/* Read what one connection has, until a frame completes or it would block.
 * Returns 1 when a frame was queued, 0 otherwise; *gone when the connection
 * ended (its sender released every handle, or exited). */
static int conn_service(WinConn *c, int *gone) {
    *gone = 0;
    for (;;) {
        int t = conn_take_frame(c);
        if (t > 0) {
            return 1;
        }
        if (t < 0) {
            WinFrame *bad = calloc(1, sizeof *bad);
            if (bad) {
                frame_push(bad);
            }
            *gone = 1;
            return 1;
        }
        if (c->cap - c->len < 65536) {
            size_t cap = c->cap ? c->cap * 2 : 65536;
            while (cap - c->len < 65536) {
                cap *= 2;
            }
            unsigned char *grown = realloc(c->buf, cap);
            if (!grown) {
                *gone = 1;
                return 0;
            }
            c->buf = grown;
            c->cap = cap;
        }
        int n = recv(c->s, (char *)c->buf + c->len, (int)(c->cap - c->len), 0);
        if (n > 0) {
            c->len += (size_t)n;
            continue;
        }
        if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
            return 0;
        }
        *gone = 1;          /* 0: orderly end; error: the sender died */
        return 0;
    }
}

/* Accept every waiting connection, then -- only while nothing is queued, so a
 * flooding sender meets FULL as on POSIX instead of filling our memory -- read
 * until one whole frame is queued. Never blocks. */
static void inbox_accept_all(void) {
    for (;;) {
        SOCKET a = accept(inbox.listener, NULL, NULL);
        if (a == INVALID_SOCKET) {
            break;
        }
        SetHandleInformation((HANDLE)a, HANDLE_FLAG_INHERIT, 0);
        u_long nb = 1;
        ioctlsocket(a, FIONBIO, &nb);
        WinConn *grown = realloc(inbox.conns, sizeof(WinConn) * (inbox.nconns + 1));
        if (!grown) {
            closesocket(a);
            break;
        }
        inbox.conns = grown;
        inbox.conns[inbox.nconns].s = a;
        inbox.conns[inbox.nconns].buf = NULL;
        inbox.conns[inbox.nconns].len = 0;
        inbox.conns[inbox.nconns].cap = 0;
        inbox.nconns++;
    }
}

static void inbox_service(void) {
    if (!inbox.open) {
        return;
    }
    inbox_accept_all();
    if (inbox.head || inbox.nconns == 0) {
        return;
    }
    if (inbox.startup_only && inbox.nconns <= inbox.startup_index) {
        return;             /* the parent has not connected yet */
    }
    size_t tries = inbox.startup_only ? 1 : inbox.nconns;
    size_t i = inbox.startup_only ? inbox.startup_index : inbox.next_read % inbox.nconns;
    while (tries-- > 0 && !inbox.head && inbox.nconns > 0) {
        if (i >= inbox.nconns) {
            i = 0;
        }
        int gone = 0;
        int got = conn_service(&inbox.conns[i], &gone);
        if (gone) {
            conn_drop(i);       /* slot i now holds the next connection */
        } else {
            i++;
        }
        if (got) {
            inbox.startup_only = 0;
            inbox.next_read = i;
        }
    }
}

int mailbox_new_path(char *buf, size_t size) {
    WCHAR wtmp[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, wtmp);
    if (n == 0 || n > MAX_PATH) {
        return -1;
    }
    char tmp[MAX_PATH * 4];
    if (!WideCharToMultiByte(CP_UTF8, 0, wtmp, -1, tmp, sizeof tmp, NULL, NULL)) {
        return -1;
    }
    static unsigned seq = 0;
    unsigned char rnd[6];
    if (gb_secure_random(rnd, sizeof rnd) != 0) {
        return -1;
    }
    int w = snprintf(buf, size, "%sgbasic-actor-%lu-%u-%02x%02x%02x%02x%02x%02x.sock",
                     tmp, (unsigned long)GetCurrentProcessId(), ++seq,
                     rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], rnd[5]);
    /* AF_UNIX's sun_path is 108 bytes on Windows too. A temporary directory
     * that leaves no room is refused here, by length, not at bind. */
    if (w < 0 || (size_t)w >= size || (size_t)w >= sizeof inbox.path) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int mailbox_open_at(Mailbox *box, const char *path) {
    box->read_fd = -1;
    box->write_fd = -1;
    if (inbox.open) {
        errno = EBUSY;          /* one inbox per process, which is all there is */
        return -1;
    }
    if (!net_ready() || strlen(path) >= sizeof inbox.path) {
        errno = EINVAL;
        return -1;
    }
    SOCKET l = socket(AF_UNIX, SOCK_STREAM, 0);
    if (l == INVALID_SOCKET) {
        errno = EIO;
        return -1;
    }
    SetHandleInformation((HANDLE)l, HANDLE_FLAG_INHERIT, 0);
    struct sockaddr_un a;
    sockaddr_for(&a, path);
    if (bind(l, (struct sockaddr *)&a, sizeof a) != 0 || listen(l, SOMAXCONN) != 0) {
        closesocket(l);
        errno = EIO;
        return -1;
    }
    u_long nb = 1;
    ioctlsocket(l, FIONBIO, &nb);
    memset(&inbox, 0, sizeof inbox);
    inbox.open = 1;
    inbox.listener = l;
    strcpy(inbox.path, path);
    int self = channel_handle_adopt_path(path);
    if (self < 0 || channel_handle_pollfd(self) < 0) {
        if (self >= 0) {
            channel_handle_close(self);
        }
        Mailbox tmp = { WIN_INBOX_ID, -1 };
        mailbox_close(&tmp);
        errno = EIO;
        return -1;
    }
    /* self() is a connection to our own inbox, and it is the FIRST one the
     * listener sees. Accept it now so it is conns[0] and the startup frame
     * is waited for on the connection after it -- the parent's. */
    WSAPOLLFD lp = { l, POLLIN, 0 };
    WSAPoll(&lp, 1, 5000);
    inbox_accept_all();
    inbox.startup_only = 1;
    inbox.startup_index = inbox.nconns;
    box->read_fd = WIN_INBOX_ID;
    box->write_fd = self;
    return 0;
}

int mailbox_open(Mailbox *box) {
    char path[sizeof inbox.path];
    if (mailbox_new_path(path, sizeof path) != 0) {
        box->read_fd = -1;
        box->write_fd = -1;
        return -1;
    }
    if (mailbox_open_at(box, path) != 0) {
        return -1;
    }
    inbox.startup_only = 0;     /* the root has no startup frame */
    return 0;
}

void mailbox_close(Mailbox *box) {
    if (box->write_fd >= 0) {
        channel_handle_close(box->write_fd);
        box->write_fd = -1;
    }
    if (box->read_fd == WIN_INBOX_ID && inbox.open) {
        while (inbox.nconns > 0) {
            conn_drop(inbox.nconns - 1);
        }
        free(inbox.conns);
        while (inbox.head) {
            WinFrame *f = inbox.head;
            inbox.head = f->next;
            frame_free(f);
        }
        closesocket(inbox.listener);
        /* The socket FILE outlives the socket (measured); a stale one would be
         * a name nothing answers at. */
        WCHAR wpath[sizeof inbox.path];
        if (MultiByteToWideChar(CP_UTF8, 0, inbox.path, -1, wpath, (int)(sizeof wpath / sizeof wpath[0]))) {
            DeleteFileW(wpath);
        }
        memset(&inbox, 0, sizeof inbox);
    }
    box->read_fd = -1;
}

size_t mailbox_poll_count(const Mailbox *box) {
    return (box->read_fd == WIN_INBOX_ID && inbox.open) ? 1 + inbox.nconns : 0;
}

void mailbox_poll_fill(const Mailbox *box, struct pollfd *out) {
    if (box->read_fd != WIN_INBOX_ID || !inbox.open) {
        return;
    }
    out[0].fd = inbox.listener;
    out[0].events = POLLIN;
    out[0].revents = 0;
    for (size_t i = 0; i < inbox.nconns; i++) {
        out[1 + i].fd = inbox.conns[i].s;
        out[1 + i].events = POLLIN;
        out[1 + i].revents = 0;
    }
}

int mailbox_poll_ready(Mailbox *box, const struct pollfd *polled, size_t n) {
    (void)box;
    for (size_t i = 0; i < n; i++) {
        if (polled[i].revents) {
            inbox_service();
            break;
        }
    }
    return inbox.head != NULL;
}

int mailbox_pending(const Mailbox *box) {
    (void)box;
    return inbox.head != NULL;
}

int channel_recv_fds(int read_fd, void **out, size_t *out_len,
                     int **out_fds, size_t *out_nfds) {
    *out_fds = NULL;
    *out_nfds = 0;
    if (read_fd != WIN_INBOX_ID || !inbox.open) {
        return ACTOR_RECV_ERROR;
    }
    Mailbox box = { WIN_INBOX_ID, -1 };
    for (;;) {
        inbox_service();
        if (inbox.head) {
            break;
        }
        size_t n = mailbox_poll_count(&box);
        WSAPOLLFD *p = malloc(sizeof(WSAPOLLFD) * n);
        if (!p) {
            return ACTOR_RECV_ERROR;
        }
        mailbox_poll_fill(&box, p);
        int r = WSAPoll(p, (ULONG)n, -1);
        free(p);
        if (r < 0) {
            return ACTOR_RECV_ERROR;
        }
    }
    WinFrame *f = inbox.head;
    inbox.head = f->next;
    if (!inbox.head) {
        inbox.tail = NULL;
    }
    /* A connection's buffer may already hold the NEXT whole frame, and bytes
     * already read never make a socket readable again -- so a wait would sleep
     * on a message that has arrived. Queue it now, where mailbox_pending sees
     * it. */
    inbox_service();
    if (!f->bytes) {
        frame_free(f);
        return ACTOR_RECV_ERROR;
    }
    int *fds = f->npaths ? malloc(sizeof(int) * f->npaths) : NULL;
    if (f->npaths && !fds) {
        frame_free(f);
        return ACTOR_RECV_ERROR;
    }
    size_t got = 0;
    for (size_t i = 0; i < f->npaths; i++) {
        int h = channel_handle_adopt_path(f->paths[i]);
        if (h < 0) {
            for (size_t k = 0; k < got; k++) {
                channel_handle_close(fds[k]);
            }
            free(fds);
            frame_free(f);
            return ACTOR_RECV_ERROR;
        }
        fds[got++] = h;
    }
    *out = f->bytes;
    *out_len = f->len;
    f->bytes = NULL;
    frame_free(f);
    *out_fds = fds;
    *out_nfds = got;
    return ACTOR_RECV_OK;
}

int channel_recv(int read_fd, void **out, size_t *out_len) {
    int *fds = NULL;
    size_t nfds = 0;
    int rc = channel_recv_fds(read_fd, out, out_len, &fds, &nfds);
    for (size_t i = 0; i < nfds; i++) {
        channel_handle_close(fds[i]);
    }
    free(fds);
    return rc;
}

#else /* POSIX: everything to the end of the file */

#include <poll.h>
#include <sys/socket.h>

/* The handle IS the write descriptor; each of these is the one call the
 * evaluator made itself before the seam existed. */
int channel_handle_dup(int handle) {
    return fcntl(handle, F_DUPFD_CLOEXEC, 3);
}

void channel_handle_close(int handle) {
    close(handle);
}

int channel_handle_hung_up(int handle) {
    struct pollfd p = { handle, 0, 0 };
    int r = poll(&p, 1, 0);
    return r > 0 && (p.revents & (POLLHUP | POLLERR | POLLNVAL));
}

int channel_handle_pollfd(int handle) {
    return handle;
}

size_t mailbox_poll_count(const Mailbox *box) {
    (void)box;
    return 1;
}

void mailbox_poll_fill(const Mailbox *box, struct pollfd *out) {
    out[0].fd = box->read_fd;
    out[0].events = POLLIN;
    out[0].revents = 0;
}

int mailbox_poll_ready(Mailbox *box, const struct pollfd *polled, size_t n) {
    (void)box;
    return n > 0 && polled[0].revents != 0;
}

int mailbox_pending(const Mailbox *box) {
    (void)box;
    return 0;
}

/* Documented portable floor for a single message: 64 KiB. channel_max_message
 * never returns less than this, so a program can rely on at least this much
 * regardless of the platform's default socket buffer. */
#define ACTOR_MIN_MAX_MESSAGE ((size_t)65536)

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int mailbox_open(Mailbox *box) {
    int sv[2];
    if (!gb_channel_socketpair(sv)) {
        return -1;
    }
    /* sv[0] is the owner's read end (kept blocking so receive() blocks); sv[1]
     * is the senders' write end (non-blocking so a full mailbox errors). */
    if (set_nonblocking(sv[1]) != 0) {
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    box->read_fd = sv[0];
    box->write_fd = sv[1];
    return 0;
}

void mailbox_close(Mailbox *box) {
    if (box->read_fd >= 0) {
        close(box->read_fd);
        box->read_fd = -1;
    }
    if (box->write_fd >= 0) {
        close(box->write_fd);
        box->write_fd = -1;
    }
}

size_t channel_max_message(int write_fd) {
    int sndbuf = 0;
    socklen_t len = sizeof sndbuf;
    if (getsockopt(write_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &len) == 0 && sndbuf > 0) {
        /* The kernel reports SO_SNDBUF as roughly twice the usable payload
         * (it reserves the other half for bookkeeping); halve to a safe frame
         * size and never drop below the floor. */
        size_t usable = (size_t)sndbuf / 2;
        if (usable > ACTOR_MIN_MAX_MESSAGE) {
            return usable;
        }
    }
    return ACTOR_MIN_MAX_MESSAGE;
}

int channel_send(int write_fd, const void *bytes, size_t len) {
    return channel_send_fds(write_fd, bytes, len, NULL, 0);
}

int channel_send_fds(int write_fd, const void *bytes, size_t len,
                     const int *fds, size_t nfds) {
    if (len > channel_max_message(write_fd)) {
        return ACTOR_CHANNEL_TOOBIG;
    }
    if (nfds > ACTOR_MAX_MESSAGE_FDS) {
        return ACTOR_CHANNEL_TOOBIG;
    }

    struct iovec iov;
    iov.iov_base = (void *)bytes;
    iov.iov_len = len;

    struct msghdr msg;
    memset(&msg, 0, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    /* Ancillary buffer sized for the largest allowed fd batch, so the same stack
     * buffer serves any nfds. */
    char cbuf[CMSG_SPACE(sizeof(int) * ACTOR_MAX_MESSAGE_FDS)];
    if (nfds > 0) {
        memset(cbuf, 0, sizeof cbuf);
        msg.msg_control = cbuf;
        msg.msg_controllen = CMSG_SPACE(sizeof(int) * nfds);
        struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN(sizeof(int) * nfds);
        memcpy(CMSG_DATA(cm), fds, sizeof(int) * nfds);
    }

    for (;;) {
        /* SOCK_SEQPACKET sendmsg is all-or-nothing: one call is one whole frame. */
        ssize_t n = sendmsg(write_fd, &msg, MSG_NOSIGNAL);
        if (n >= 0) {
            return ACTOR_CHANNEL_OK;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return ACTOR_CHANNEL_FULL;
        }
        return ACTOR_CHANNEL_ERROR; /* EPIPE/ECONNRESET (peer gone) or other */
    }
}

int channel_recv(int read_fd, void **out, size_t *out_len) {
    int *fds = NULL;
    size_t nfds = 0;
    int rc = channel_recv_fds(read_fd, out, out_len, &fds, &nfds);
    /* A caller using the plain interface does not expect descriptors; close any
     * that arrived so they are not leaked. */
    for (size_t i = 0; i < nfds; i++) {
        close(fds[i]);
    }
    free(fds);
    return rc;
}

/* CLOSE-ON-EXEC ON A RECEIVED DESCRIPTOR, which is a security property and not
 * bookkeeping: a descriptor that survives exec is handed to every child this
 * interpreter spawns, and `spawn` is fork+exec.
 *
 * Linux buys it atomically with MSG_CMSG_CLOEXEC, set as the descriptors are
 * installed, so there is no window. macOS and the BSDs have no such flag --
 * this was the ONE compile error standing between gBASIC and a macOS build --
 * so there the flag is 0 and each descriptor is marked immediately after
 * recvmsg returns.
 *
 * THE DIFFERENCE IS A RACE AND IT IS STATED RATHER THAN GLOSSED: between the
 * recvmsg and the fcntl, a concurrent fork+exec would inherit the descriptor.
 * It is NOT reachable in gBASIC today -- the actor path is the main thread, and
 * the only other threads this interpreter starts are frontend parsers, which
 * never exec -- but it is a real difference in the guarantee, and a future
 * thread that execs would reopen it on macOS and not on Linux. */
#ifdef MSG_CMSG_CLOEXEC
#define GB_MSG_CMSG_CLOEXEC MSG_CMSG_CLOEXEC
#define GB_CMSG_CLOEXEC_IS_ATOMIC 1
#else
#define GB_MSG_CMSG_CLOEXEC 0
#define GB_CMSG_CLOEXEC_IS_ATOMIC 0
#endif

int channel_recv_fds(int read_fd, void **out, size_t *out_len,
                     int **out_fds, size_t *out_nfds) {
    *out_fds = NULL;
    *out_nfds = 0;
    for (;;) {
        /* Peek the exact next-frame size without consuming it. Frames always
         * carry the serializer's 4-byte header, so a length of 0 unambiguously
         * means the peer closed -- never an empty frame.
         *
         * THROUGH THE PLATFORM LAYER, because the obvious one-liner --
         * `recv(fd, NULL, 0, MSG_PEEK | MSG_TRUNC)` -- is a LINUX EXTENSION
         * that compiles elsewhere and answers 0, which the check below would
         * read as a clean shutdown. See include/platform.h. */
        ssize_t need = gb_channel_peek_len(read_fd);
        if (need < 0) {
            if (errno == EINTR) {
                continue;
            }
            return ACTOR_RECV_ERROR;
        }
        if (need == 0) {
            return ACTOR_RECV_CLOSED;
        }
        char *buf = malloc((size_t)need);
        if (!buf) {
            return ACTOR_RECV_ERROR;
        }

        struct iovec iov;
        iov.iov_base = buf;
        iov.iov_len = (size_t)need;

        struct msghdr msg;
        memset(&msg, 0, sizeof msg);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        char cbuf[CMSG_SPACE(sizeof(int) * ACTOR_MAX_MESSAGE_FDS)];
        msg.msg_control = cbuf;
        msg.msg_controllen = sizeof cbuf;

        ssize_t n = recvmsg(read_fd, &msg, GB_MSG_CMSG_CLOEXEC);
        if (n < 0) {
            free(buf);
            if (errno == EINTR) {
                continue;
            }
            return ACTOR_RECV_ERROR;
        }

        /* Collect any transferred descriptors. */
        int *fds = NULL;
        size_t nfds = 0;
        for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
            if (cm->cmsg_level != SOL_SOCKET || cm->cmsg_type != SCM_RIGHTS) {
                continue;
            }
            size_t payload = cm->cmsg_len - CMSG_LEN(0);
            size_t count = payload / sizeof(int);
            int *src = (int *)(void *)CMSG_DATA(cm);
            int *grown = realloc(fds, sizeof(int) * (nfds + count));
            if (!grown) {
                for (size_t i = 0; i < nfds; i++) {
                    close(fds[i]);
                }
                for (size_t i = 0; i < count; i++) {
                    close(src[i]);
                }
                free(fds);
                free(buf);
                return ACTOR_RECV_ERROR;
            }
            fds = grown;
            memcpy(fds + nfds, src, sizeof(int) * count);
            nfds += count;
        }

        /* Where the kernel could not do it atomically, do it now. */
        if (!GB_CMSG_CLOEXEC_IS_ATOMIC) {
            for (size_t i = 0; i < nfds; i++) {
                gb_set_cloexec(fds[i], 1);
            }
        }

        *out = buf;
        *out_len = (size_t)n;
        *out_fds = fds;
        *out_nfds = nfds;
        return ACTOR_RECV_OK;
    }
}

#endif /* _WIN32 */
