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
 * That paragraph is POSIX. Windows has no SOCK_SEQPACKET and no SCM_RIGHTS, so
 * there the same contract is rebuilt from AF_UNIX streams: a length prefix per
 * frame, one connection per sending process, and handles sent as inbox paths
 * (the Windows half of src/actor.c; docs/windows_port_status.md §22).
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

/* Most file descriptors a single message may carry as SCM_RIGHTS ancillary data
 * (one per actor handle embedded in the value). */
#define ACTOR_MAX_MESSAGE_FDS 32

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

/* Like channel_send, but also transfers `nfds` file descriptors to the receiver
 * as SCM_RIGHTS ancillary data (nfds may be 0; capped at ACTOR_MAX_MESSAGE_FDS).
 * The receiver obtains its own duplicates; the sender's fds are unaffected. */
int channel_send_fds(int write_fd, const void *bytes, size_t len,
                     const int *fds, size_t nfds);

/* Receive one frame (blocking) into a freshly malloc'd buffer. On
 * ACTOR_RECV_OK the caller owns *out and must free() it. Returns an
 * ACTOR_RECV_* code. */
int channel_recv(int read_fd, void **out, size_t *out_len);

/* Like channel_recv, but also collects any SCM_RIGHTS descriptors the sender
 * attached. On ACTOR_RECV_OK *out_fds (NULL if none) is a freshly malloc'd array
 * of *out_nfds descriptors the caller owns: it must close each and free the
 * array. Descriptors beyond ACTOR_MAX_MESSAGE_FDS are closed and dropped. */
int channel_recv_fds(int read_fd, void **out, size_t *out_len,
                     int **out_fds, size_t *out_nfds);

/* --- a sender's capability, and waiting on an inbox ------------------------
 *
 * The evaluator holds a HANDLE (an int in Mailbox.write_fd / ActorHandle) and
 * never says what it is. On POSIX it IS the mailbox write descriptor and every
 * function below is the one system call the evaluator used to make itself, so
 * the Linux build is unchanged by construction. On Windows it is an index into
 * actor.c's table of connections (see the Windows half of src/actor.c). */

/* A second, independently closable handle to the same mailbox (-1 on failure). */
int channel_handle_dup(int handle);

/* Give the handle up; the mailbox is unaffected while other handles remain. */
void channel_handle_close(int handle);

/* Has the actor behind this handle gone (its inbox closed)? Never blocks. */
int channel_handle_hung_up(int handle);

/* A descriptor to poll (events 0) for that hang-up, or -1 when there is none
 * because it has already happened. */
int channel_handle_pollfd(int handle);

/* Waiting on an inbox. POSIX polls ONE descriptor; a Windows inbox is a
 * listener plus one connection per sending process, so the set is asked for
 * rather than assumed. `mailbox_poll_fill` writes mailbox_poll_count() entries.
 * `mailbox_poll_ready` is called with those entries after the poll and answers
 * whether a whole frame can now be received without blocking.
 * `mailbox_pending` answers that without a poll (Windows may already hold a
 * frame it read; POSIX never does). */
struct pollfd;
size_t mailbox_poll_count(const Mailbox *box);
void mailbox_poll_fill(const Mailbox *box, struct pollfd *out);
int mailbox_poll_ready(Mailbox *box, const struct pollfd *polled, size_t n);
int mailbox_pending(const Mailbox *box);

#ifdef _WIN32
/* WINDOWS ONLY. A mailbox is NAMED: an AF_UNIX listener at a path, and a handle
 * travels as that path, not as a descriptor.
 *
 * mailbox_new_path: a fresh, unused inbox path for a child the caller is about
 * to spawn (the parent chooses it; the child listens there).
 * mailbox_open_at: the spawned child's side -- listen at the path its parent
 * chose. Until its first frame arrives, only the FIRST connection is read, so
 * the parent's startup frame is always the child's first message.
 * channel_handle_adopt_path: a handle to the inbox at `path` (connected now; a
 * handle to an inbox nobody listens at is a handle to an actor that has gone). */
int mailbox_new_path(char *buf, size_t size);
int mailbox_open_at(Mailbox *box, const char *path);
int channel_handle_adopt_path(const char *path);
#endif

#endif /* GBASIC_ACTOR_H */
