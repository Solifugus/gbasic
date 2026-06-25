#ifndef GBASIC_ACTOR_H
#define GBASIC_ACTOR_H

#include <stddef.h>

/*
 * Mailbox transport for multiprocessing actors
 * (docs/multiprocessing_design.md §4.1).
 *
 * A mailbox is one inbound channel: the owner reads its inbox; any number of
 * senders write to it. It is built on an AF_UNIX SOCK_SEQPACKET socket pair, so
 * every send() is one atomic, boundary-preserved frame -- no explicit length
 * framing is needed and two concurrent senders never byte-interleave. The write
 * end is non-blocking, so a full mailbox fails fast (ACTOR_CHANNEL_FULL) rather
 * than blocking the sender (the §4.1 bounded-mailbox, erroring-send contract).
 *
 * This module is transport-only: it moves opaque byte frames and knows nothing
 * about gBASIC values. The evaluator serializes a value to bytes (Phase 0) and
 * hands those bytes here.
 */

/* channel_send result codes. */
enum {
    ACTOR_CHANNEL_OK = 0,
    ACTOR_CHANNEL_FULL = -1,    /* mailbox full (would block) -> structured error */
    ACTOR_CHANNEL_TOOBIG = -2,  /* frame exceeds the per-message maximum */
    ACTOR_CHANNEL_ERROR = -3    /* peer gone / unexpected error */
};

/* channel_recv result codes. */
enum {
    ACTOR_RECV_OK = 0,
    ACTOR_RECV_CLOSED = 1,      /* every write end is closed; no more frames */
    ACTOR_RECV_ERROR = -1
};

typedef struct {
    int read_fd;   /* the owner reads its inbox here (blocking) */
    int write_fd;  /* senders write here; this is the capability handed out */
} Mailbox;

/* Create a mailbox pair. Returns 0 on success, -1 on error (errno set). */
int mailbox_open(Mailbox *box);

/* Close both ends of a mailbox; safe on already-closed (-1) fds. */
void mailbox_close(Mailbox *box);

/* Largest frame, in bytes, that may be sent to write_fd: derived from the
 * socket's SO_SNDBUF, never below a documented floor so programs have a portable
 * lower bound to rely on. */
size_t channel_max_message(int write_fd);

/* Send one atomic frame. write_fd must be non-blocking (mailbox_open sets the
 * write end that way). Returns an ACTOR_CHANNEL_* code. */
int channel_send(int write_fd, const void *bytes, size_t len);

/* Receive one frame (blocking) into a freshly malloc'd buffer. On
 * ACTOR_RECV_OK the caller owns *out and must free() it. Returns an
 * ACTOR_RECV_* code. */
int channel_recv(int read_fd, void **out, size_t *out_len);

#endif /* GBASIC_ACTOR_H */
