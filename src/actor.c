#ifndef _GNU_SOURCE
#define _GNU_SOURCE  /* MSG_NOSIGNAL, MSG_TRUNC, SOCK_SEQPACKET */
#endif

#include "actor.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

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
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) != 0) {
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
    if (len > channel_max_message(write_fd)) {
        return ACTOR_CHANNEL_TOOBIG;
    }
    for (;;) {
        /* SOCK_SEQPACKET send is all-or-nothing: one call is one whole frame. */
        ssize_t n = send(write_fd, bytes, len, MSG_NOSIGNAL);
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
    for (;;) {
        /* Peek the exact next-frame size without consuming it (MSG_TRUNC on a
         * datagram socket reports the true length). Frames always carry the
         * serializer's 4-byte header, so a length of 0 unambiguously means the
         * peer closed -- never an empty frame. */
        ssize_t need = recv(read_fd, NULL, 0, MSG_PEEK | MSG_TRUNC);
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
        ssize_t n = recv(read_fd, buf, (size_t)need, 0);
        if (n < 0) {
            free(buf);
            if (errno == EINTR) {
                continue;
            }
            return ACTOR_RECV_ERROR;
        }
        *out = buf;
        *out_len = (size_t)n;
        return ACTOR_RECV_OK;
    }
}
