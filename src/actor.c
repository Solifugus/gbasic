#ifndef _GNU_SOURCE
#define _GNU_SOURCE  /* MSG_NOSIGNAL, MSG_TRUNC, SOCK_SEQPACKET */
#endif

#include "actor.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
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

int channel_recv_fds(int read_fd, void **out, size_t *out_len,
                     int **out_fds, size_t *out_nfds) {
    *out_fds = NULL;
    *out_nfds = 0;
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

        ssize_t n = recvmsg(read_fd, &msg, MSG_CMSG_CLOEXEC);
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

        *out = buf;
        *out_len = (size_t)n;
        *out_fds = fds;
        *out_nfds = nfds;
        return ACTOR_RECV_OK;
    }
}
