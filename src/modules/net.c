/* ===================== net: TCP and Unix sockets (N1) ======================
 *
 * docs/networking_design.md, ruled by Matthew 2026-11-04. Included into
 * src/eval.c, like the other modules, so it shares the value system, the
 * error model and the event loop rather than reaching them through an API.
 *
 * WHAT IT IS: `load net` binds a global `net` record holding `events`, and the
 * verbs make CONNECTIONS and LISTENERS -- one value kind, VALUE_NET, whose
 * `kind` field says which. Core, not optional: sockets are POSIX and need no
 * library. TLS (N2) is the one optional part: it sits behind HAVE_LIBSSL, and
 * without it a `tls:` option is refused with a sentence.
 *
 * THE RULES IT KEEPS, each from the design's §2:
 *   P1  ONE LOOP. A watched socket's descriptor joins webserver_loop_iterate's
 *       poll, which is also what the Qt pump drives, so a socket is serviced
 *       with a window or a modal dialog up.
 *   P2  WAITED OR WATCHED, NEVER BOTH, decided when the socket is made: watched
 *       when a watcher on `net.events` is live then, as `http` decides at start.
 *   P3  BYTES, COUNTED. Every length here is bytes; NUL is content.
 *   P4  THE BUFFER IS THE MODULE'S, the framing the program's: take/read cut
 *       exactly n bytes, up to a delimiter, or a line.
 *   P5  BOUNDED BY DEFAULT: a receive ceiling (reading pauses at it), a send
 *       ceiling (a write past it is refused), idle and connect timeouts, and a
 *       connection cap on a listener.
 *   P6  LOOPBACK BY DEFAULT.
 *   P7  FAILURES ARE NAMED: code 1004, source `net`, and `details.code`.
 *
 * THE LOOP HOLDS A REFERENCE to every watched socket while it is open, so a
 * handler that rebinds `c` per event does not close the previous connection --
 * the trap `http`'s loop reference exists for. The reference is dropped when
 * the `closed` event has been delivered. A WAITED socket has no loop
 * reference, and dropping the last value that names it closes it.
 * ========================================================================= */

#ifdef _WIN32
/* ---- WINDOWS: Winsock behind the POSIX names this file uses -----------------
 *
 * A Winsock SOCKET is not a file descriptor: close() and write() on one fail or
 * leak, and failures are reported through WSAGetLastError, never errno. Every
 * socket call below goes through a wrapper that keeps the POSIX shape (an int
 * descriptor, -1 and errno on failure), so the logic of this file is one body on
 * both platforms. The names are #undef'd at the end of the file, so nothing
 * later in src/eval.c sees them.
 *
 * WHAT DIFFERS, AND IS SAID RATHER THAN HIDDEN:
 *   - A non-blocking connect answers WSAEWOULDBLOCK where POSIX says
 *     EINPROGRESS; the connect wrapper translates that one case.
 *   - A truncated datagram is a FAILURE (WSAEMSGSIZE) with the buffer filled,
 *     where POSIX returns the bytes and sets MSG_TRUNC; recvmsg here turns the
 *     first into the second. The size that was SENT is unknowable, as on macOS.
 *   - The resolver's wake is a loopback socket pair rather than a pipe, because
 *     WSAPoll takes sockets only.
 *   - AF_UNIX is stream-only on Windows (afunix.h, Windows 10 1803 and later),
 *     which is all `net` asks of it; peer credentials have no Windows form and
 *     answer unknown, as on a POSIX system without SO_PEERCRED.
 *   - Sockets are made non-inheritable, Windows' FD_CLOEXEC. */
#include <afunix.h>
#include <mswsock.h>    /* SIO_UDP_CONNRESET */
#include <pthread.h>

#ifndef MSG_TRUNC
#define MSG_TRUNC 0x0100
#endif
#define SHUT_WR SD_SEND
#define SOCK_CLOEXEC 0

struct iovec { void *iov_base; size_t iov_len; };
struct msghdr {
    void *msg_name;
    socklen_t msg_namelen;
    struct iovec *msg_iov;
    size_t msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    int msg_flags;
};

/* A WSA error code -> errno, for the codes this file's callers distinguish. */
static int net_w_map(int w) {
    int e;
    switch (w) {
    case WSAEWOULDBLOCK: e = EAGAIN; break;
    case WSAEINPROGRESS: e = EINPROGRESS; break;
    case WSAEINTR: e = EINTR; break;
    case WSAECONNREFUSED: e = ECONNREFUSED; break;
    case WSAETIMEDOUT: e = ETIMEDOUT; break;
    case WSAECONNRESET: e = ECONNRESET; break;
    case WSAECONNABORTED: e = ECONNABORTED; break;
    case WSAESHUTDOWN: e = EPIPE; break;
    case WSAENETUNREACH: e = ENETUNREACH; break;
    case WSAEHOSTUNREACH: e = EHOSTUNREACH; break;
    case WSAENETDOWN: e = ENETDOWN; break;
    case WSAEADDRINUSE: e = EADDRINUSE; break;
    case WSAEADDRNOTAVAIL: e = EADDRNOTAVAIL; break;
    case WSAEACCES: e = EACCES; break;
    case WSAEMSGSIZE: e = EMSGSIZE; break;
    case WSAENOBUFS: e = ENOBUFS; break;
    case WSAENOTCONN: e = ENOTCONN; break;
    case WSAEAFNOSUPPORT: e = EAFNOSUPPORT; break;
    case WSAEINVAL: e = EINVAL; break;
    case WSAENOTSOCK: e = EBADF; break;
    case WSAEMFILE: e = EMFILE; break;
    default: e = EIO; break;
    }
    return e;
}

static int net_w_errno(void) {
    errno = net_w_map(WSAGetLastError());
    return errno;
}

/* The UCRT's strerror has no text for the socket errnos ("Unknown error"), and
 * a message naming the reason is the whole of P7. */
static const char *net_w_strerror(int e) {
    switch (e) {
    case ECONNREFUSED: return "Connection refused";
    case ETIMEDOUT: return "Connection timed out";
    case ECONNRESET: return "Connection reset by peer";
    case ECONNABORTED: return "Software caused connection abort";
    case EPIPE: return "Broken pipe";
    case ENETUNREACH: return "Network is unreachable";
    case EHOSTUNREACH: return "No route to host";
    case ENETDOWN: return "Network is down";
    case EADDRINUSE: return "Address already in use";
    case EADDRNOTAVAIL: return "Cannot assign requested address";
    case EMSGSIZE: return "Message too long";
    case ENOBUFS: return "No buffer space available";
    case ENOTCONN: return "Transport endpoint is not connected";
    case EAFNOSUPPORT: return "Address family not supported by protocol";
    case EINPROGRESS: return "Operation now in progress";
    default: return strerror(e);
    }
}

static int net_w_socket(int family, int type, int protocol) {
    SOCKET s = WSASocketW(family, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET) {
        net_w_errno();
        return -1;
    }
    DWORD no = 0, ignored = 0;
    if (family == AF_INET6) {
        /* Windows defaults IPV6_V6ONLY to ON, Linux to off; `::` reaching IPv4
         * too, mapped, is the contract (tests/net/datagram_test.bas) */
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&no, sizeof no);
    }
    if (type == SOCK_DGRAM) {
        /* Without this an ICMP port-unreachable for an EARLIER send_to surfaces
         * as WSAECONNRESET on the next receive -- a reset on a connectionless
         * socket, which POSIX never reports. */
        BOOL off = FALSE;
        WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof off, NULL, 0, &ignored, NULL, NULL);
    }
    return (int)s;
}

/* Windows' AF_UNIX gives the peer's process id and nothing else: there is no
 * uid or gid to give. -1 when the ioctl is refused. */
static long net_w_peer_pid(int fd) {
    ULONG pid = 0;
    DWORD got = 0;
    if (WSAIoctl((SOCKET)fd, SIO_AF_UNIX_GETPEERPID, NULL, 0, &pid, sizeof pid, &got, NULL, NULL) != 0) {
        return -1;
    }
    return (long)pid;
}
static int net_w_connect(int fd, const struct sockaddr *a, socklen_t n) {
    if (connect((SOCKET)fd, a, n) == 0) {
        return 0;
    }
    int w = WSAGetLastError();
    net_w_errno();
    if (w == WSAEWOULDBLOCK) {
        errno = EINPROGRESS;      /* the one translation POSIX callers rely on */
    }
    return -1;
}
static int net_w_bind(int fd, const struct sockaddr *a, socklen_t n) {
    if (bind((SOCKET)fd, a, n) == 0) {
        return 0;
    }
    net_w_errno();
    return -1;
}
static int net_w_listen(int fd, int backlog) {
    if (listen((SOCKET)fd, backlog) == 0) {
        return 0;
    }
    net_w_errno();
    return -1;
}
static int net_w_accept(int fd, struct sockaddr *a, socklen_t *n) {
    int len = n ? (int)*n : 0;
    SOCKET c = accept((SOCKET)fd, a, n ? &len : NULL);
    if (c == INVALID_SOCKET) {
        net_w_errno();
        return -1;
    }
    if (n) {
        *n = (socklen_t)len;
    }
    SetHandleInformation((HANDLE)c, HANDLE_FLAG_INHERIT, 0);
    return (int)c;
}
static ssize_t net_w_recv(int fd, void *b, size_t n, int flags) {
    int r = recv((SOCKET)fd, (char *)b, n > INT_MAX ? INT_MAX : (int)n, flags);
    if (r == SOCKET_ERROR) {
        net_w_errno();
        return -1;
    }
    return r;
}
static ssize_t net_w_send(int fd, const void *b, size_t n, int flags) {
    int r = send((SOCKET)fd, (const char *)b, n > INT_MAX ? INT_MAX : (int)n, flags);
    if (r == SOCKET_ERROR) {
        net_w_errno();
        return -1;
    }
    return r;
}
static ssize_t net_w_sendto(int fd, const void *b, size_t n, int flags, const struct sockaddr *a, socklen_t al) {
    int r = sendto((SOCKET)fd, (const char *)b, n > INT_MAX ? INT_MAX : (int)n, flags, a, al);
    if (r == SOCKET_ERROR) {
        net_w_errno();
        return -1;
    }
    return r;
}
static ssize_t net_w_recvmsg(int fd, struct msghdr *mh, int flags) {
    int nl = (int)mh->msg_namelen;
    mh->msg_flags = 0;
    int r = recvfrom((SOCKET)fd, (char *)mh->msg_iov[0].iov_base, (int)mh->msg_iov[0].iov_len,
                     flags & ~MSG_TRUNC, (struct sockaddr *)mh->msg_name, &nl);
    mh->msg_namelen = (socklen_t)nl;
    if (r == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEMSGSIZE) {
            /* the buffer was filled and the rest discarded: POSIX's MSG_TRUNC */
            mh->msg_flags |= MSG_TRUNC;
            return (ssize_t)mh->msg_iov[0].iov_len;
        }
        net_w_errno();
        return -1;
    }
    return r;
}
static int net_w_setsockopt(int fd, int level, int name, const void *v, socklen_t n) {
    if (setsockopt((SOCKET)fd, level, name, (const char *)v, (int)n) == 0) {
        return 0;
    }
    net_w_errno();
    return -1;
}
static int net_w_getsockopt(int fd, int level, int name, void *v, socklen_t *n) {
    int len = (int)*n;
    if (getsockopt((SOCKET)fd, level, name, (char *)v, &len) != 0) {
        net_w_errno();
        return -1;
    }
    if (level == SOL_SOCKET && name == SO_ERROR && len == (int)sizeof(int) && *(int *)v != 0) {
        *(int *)v = net_w_map(*(int *)v);   /* SO_ERROR holds a WSA code */
    }
    *n = (socklen_t)len;
    return 0;
}
static int net_w_getsockname(int fd, struct sockaddr *a, socklen_t *n) {
    int len = (int)*n;
    if (getsockname((SOCKET)fd, a, &len) != 0) {
        net_w_errno();
        return -1;
    }
    *n = (socklen_t)len;
    return 0;
}
static int net_w_getpeername(int fd, struct sockaddr *a, socklen_t *n) {
    int len = (int)*n;
    if (getpeername((SOCKET)fd, a, &len) != 0) {
        net_w_errno();
        return -1;
    }
    *n = (socklen_t)len;
    return 0;
}
static int net_w_shutdown(int fd, int how) {
    if (shutdown((SOCKET)fd, how) == 0) {
        return 0;
    }
    net_w_errno();
    return -1;
}
static int net_w_close(int fd) {
    return closesocket((SOCKET)fd) == 0 ? 0 : -1;
}
/* write() is used on one descriptor here: the resolver's wake. */
static ssize_t net_w_write(int fd, const void *b, size_t n) {
    return net_w_send(fd, b, n, 0);
}
/* The resolver's wake: a connected loopback TCP pair, since WSAPoll cannot
 * poll a pipe. The listener lives only for the instant it takes to connect,
 * and the accepted peer is checked to be the socket this function connected,
 * so nothing else can slip in as the wake. */
static int net_w_pipe(int fds[2]) {
    int l = net_w_socket(AF_INET, SOCK_STREAM, 0);
    int c = -1, a = -1;
    struct sockaddr_in in, peer, mine;
    socklen_t n = sizeof in;
    memset(&in, 0, sizeof in);
    in.sin_family = AF_INET;
    in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (l < 0 || net_w_bind(l, (struct sockaddr *)&in, sizeof in) != 0 ||
        net_w_listen(l, 1) != 0 || net_w_getsockname(l, (struct sockaddr *)&in, &n) != 0) {
        goto fail;
    }
    c = net_w_socket(AF_INET, SOCK_STREAM, 0);
    if (c < 0 || net_w_connect(c, (struct sockaddr *)&in, sizeof in) != 0) {
        goto fail;
    }
    n = sizeof peer;
    a = net_w_accept(l, (struct sockaddr *)&peer, &n);
    socklen_t mn = sizeof mine;
    if (a < 0 || net_w_getsockname(c, (struct sockaddr *)&mine, &mn) != 0 ||
        peer.sin_port != mine.sin_port || peer.sin_addr.s_addr != mine.sin_addr.s_addr) {
        goto fail;
    }
    net_w_close(l);
    fds[0] = a;
    fds[1] = c;
    return 0;
fail:
    if (a >= 0) {
        net_w_close(a);
    }
    if (c >= 0) {
        net_w_close(c);
    }
    if (l >= 0) {
        net_w_close(l);
    }
    return -1;
}

#define socket net_w_socket
#define connect net_w_connect
#define bind net_w_bind
#define listen net_w_listen
#define accept net_w_accept
#define recv net_w_recv
#define send net_w_send
#define sendto net_w_sendto
#define recvmsg net_w_recvmsg
#define setsockopt net_w_setsockopt
#define getsockopt net_w_getsockopt
#define getsockname net_w_getsockname
#define getpeername net_w_getpeername
#define shutdown net_w_shutdown
#define close net_w_close
#define write net_w_write
#define pipe net_w_pipe
#define strerror net_w_strerror
#define lstat stat
#else
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <pthread.h>
#endif
#if HAVE_LIBSSL
#include <openssl/x509v3.h>
#endif

#define NET_ERROR_CODE 1004
#define NET_DEFAULT_BUFFER (16u * 1024u * 1024u)
#define NET_LOW_WATER (64u * 1024u)
#define NET_READ_CHUNK 65536
#define NET_DEFAULT_DATAGRAM 65536u   /* more than any UDP payload (65507 over IPv4) */
#define NET_DATAGRAMS_PER_PASS 64
/* What a receive reads into. Linux reports a truncated datagram's SENT size
 * through MSG_TRUNC; Windows cannot (WSAEMSGSIZE says only that it was cut), so
 * there the buffer holds the largest datagram UDP carries and the cut to
 * max_datagram is made here, which gives the same answer. */
#ifdef _WIN32
#define NET_DGRAM_RECV_SIZE(max) ((max) > 65536u ? (max) : 65536u)
#else
#define NET_DGRAM_RECV_SIZE(max) (max)
#endif

typedef enum { NET_CONNECTION = 0, NET_LISTENER = 1, NET_DATAGRAM = 2 } NetKind;

/* Why a handshake is under way, which decides what its end reports. */
enum {
    NET_TLS_NONE = 0,
    NET_TLS_CONNECT = 1,     /* net.connect with tls: -> `connected` */
    NET_TLS_UPGRADE = 2,     /* start_tls -> a `tls` event */
    NET_TLS_ACCEPT = 3       /* accepted on a TLS listener -> `accepted` */
};

#if HAVE_LIBSSL
/* A TLS listener's contexts: the default one and those chosen by SNI, built by
 * webserver_tls_build, which webserver uses for its own listeners. Counted,
 * because a connection still handshaking needs the SNI table after its
 * listener has closed. */
typedef struct NetTlsServer {
    size_t refs;
    SSL_CTX *dflt;
    char **hosts;
    SSL_CTX **ctxs;
    size_t count;
    int client_certificate;  /* 0 none, 1 requested, 2 required */
} NetTlsServer;
#endif

typedef struct {
    char *data;
    size_t off;      /* bytes before this have been taken */
    size_t len;      /* bytes after off */
    size_t cap;
} NetBuf;

/* A name being resolved off the loop: getaddrinfo blocks, so a WATCHED connect
 * to a name runs it on a helper thread that runs nothing else, and the loop
 * polls the pipe it writes when done. No gBASIC code ever runs on it. */
typedef struct {
    pthread_t thread;
    int pipe_fd[2];
    char *host;
    char port[16];
    int rc;
    struct addrinfo *result;
} NetResolve;

/* TCP keepalive (Transward §3): `on` with the three settings, each 0 for the
 * system's own value. */
typedef struct {
    int on;
    int idle;
    int interval;
    int count;
} NetKeepalive;

struct NetSocket {
    NetKind kind;
    long id;
    int fd;
    size_t ref_count;
    int watched;
    int loop_ref;            /* the loop holds one reference */
    int open;                /* the descriptor is live (or a connect is under way) */
    int connecting;          /* a watched connect has not finished */
    int resolving;           /* a watched connect is waiting for its name */
    int peer_closed;         /* end of stream seen */
    int closing;             /* close once the send queue drains */
    int write_shut;
    int data_notified;
    int full_notified;
    int paused;              /* the receive buffer is at its ceiling */
    int want_writable;       /* the send queue went above the low-water mark */
    int closed_pending;      /* a `closed` event is owed */
    int closed_delivered;
    char *close_code;        /* "peer", "local", "aborted", "idle", or an error's code */
    char *error_code;
    char *error_message;
    int error_pending;       /* an `error` event is owed */
    int connected_pending;   /* a `connected` event is owed */
    NetBuf in;
    NetBuf out;
    size_t in_ceiling;
    size_t out_ceiling;
    double idle_s;           /* 0 = none */
    double last_activity;
    double connect_deadline;
    double connect_timeout_s;
    int family;
    char *path;              /* a Unix socket's path */
    int unlink_on_close;     /* a listener that made its path */
    int port;
    /* a connect in progress walks these addresses in order */
    struct addrinfo *addrs;
    struct addrinfo *next_addr;
    NetResolve *resolve;
    /* listener */
    size_t max_connections;
    size_t children;
    double child_idle_s;
    size_t child_buffer;
    NetKeepalive keepalive;  /* a connect's, applied to each address tried; a listener's, applied
                              * to each connection it accepts */
    struct NetSocket *listener;   /* an accepted connection's listener (borrowed) */
    char *host;              /* the host net.connect was given: start_tls's default name */
    /* TLS (N2). The fields that are not libssl types stay outside the guard so
     * the code that only reads them needs none. */
    int tls_handshaking;     /* NET_TLS_*: why one is under way, 0 when none is */
    int tls_want_write;      /* the handshake is starved of POLLOUT rather than POLLIN */
    int tls_read_wants_write;
    int tls_write_wants_read;
    int tls_no_notify;       /* abort: close without TLS's close_notify */
    int tls_event_pending;   /* a `tls` event is owed: a watched start_tls finished */
    int announced;           /* the program has been handed this connection */
    Value tls_info;          /* what the handshake established; null before it finishes */
    /* datagram (N3) */
    size_t max_datagram;     /* a longer datagram is delivered cut, and says so */
    char *dgram_buf;
    Value dgram_pending;     /* records read by the loop, owed as `datagram` events */
    /* N4 */
    Value accept_request;    /* an upgraded connection's request, carried by its `accepted` event */
    Value state;             /* named values a library keeps per connection (net.set_state) */
#if HAVE_LIBSSL
    SSL *ssl;
    NetTlsServer *tls_server;     /* a TLS listener's contexts, and an accepted connection's */
#endif
};

static NetSocket **net_sockets = NULL;
static size_t net_socket_count = 0;
static long net_next_id = 1;
static int net_sigpipe_ignored = 0;

static int net_is_listener(NetSocket *s) {
    return s->kind == NET_LISTENER;
}

static long net_socket_id(NetSocket *s) {
    return s->id;
}

static void net_socket_retain(NetSocket *s) {
    s->ref_count++;
}

static double net_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    }
    return (double)time(NULL);
}

static Value value_net(NetSocket *s) {
    Value v = {0};
    v.kind = VALUE_NET;
    v.as.net = s;
    return v;
}

/* ---- raising, with a stable code ---------------------------------------- */

static void net_raise_details(const char *code, const char *message, Value extra_partial, int has_partial) {
    if (raise_in_flight) {
        if (has_partial) {
            value_free(extra_partial);
        }
        return;
    }
    Value details = value_record(NULL, 0);
    record_set(&details, "code", value_string(code));
    if (has_partial) {
        record_set(&details, "partial", extra_partial);
    }
    error_set_state_full(message, NET_ERROR_CODE, "net", details, value_null(), 0, 0);
    error_generation++;
    raise_in_flight = 1;
}

static Value net_raise(const char *code, const char *message) {
    net_raise_details(code, message, value_null(), 0);
    return value_null();
}

/* A misuse rather than a network failure: the ordinary 1003. */
static Value net_refuse(const char *message) {
    runtime_error_raise(message, 1003, "net");
    return value_null();
}

static const char *net_errno_code(int e) {
    switch (e) {
    case ECONNREFUSED: return "refused";
    case ETIMEDOUT: return "timeout";
    case ECONNRESET: case EPIPE:
#ifdef ECONNABORTED
    case ECONNABORTED:
#endif
        return "reset";
    case ENETUNREACH: case EHOSTUNREACH:
#ifdef ENETDOWN
    case ENETDOWN:
#endif
        return "unreachable";
    case EADDRINUSE: return "address_in_use";
    case EACCES: case EPERM: return "permission";
    case EMSGSIZE: return "too_large";     /* a datagram longer than the protocol carries */
    case ENOENT: return "refused";     /* a Unix path nobody is listening on */
    default: return "failed";
    }
}

/* ---- buffers ---------------------------------------------------------------- */

static void netbuf_compact(NetBuf *b) {
    if (b->off > 0) {
        memmove(b->data, b->data + b->off, b->len);
        b->off = 0;
    }
}

/* A REFILL COSTS WHAT WAS CONSUMED, NOT WHAT IS HELD. The held bytes are moved
 * down only when the consumed space before them is at least as large, so a
 * move is paid for by the bytes read since the last one; otherwise the buffer
 * grows to twice what it must hold. Compacting whenever the end was reached
 * moved the whole buffer for every refill of a full one: a waited reader
 * behind a faster peer, at the default 16 MiB, moved 16 MiB per 32 KiB packet
 * and read loopback at 24 MiB/s (Transward's throughput measurement, §3). The
 * price is an allocation of up to twice what the buffer holds; pages never
 * written stay unresident. THE SIZING CARRIES IT (measured): with the old
 * compact-whenever rule and this growth the suite stays fast, because a buffer
 * twice what it holds reaches its end only after consuming as much again.
 * run_net.sh tier 10 is red only with the old function whole. */
static void netbuf_append(NetBuf *b, const char *src, size_t n) {
    if (n == 0) {
        return;
    }
    if (b->off + b->len + n > b->cap && b->off >= b->len) {
        netbuf_compact(b);
    }
    if (b->off + b->len + n > b->cap) {
        /* off < len here (or it was compacted to 0), so this holds it all */
        size_t nc = 2 * (b->len + n);
        if (nc < 4096) {
            nc = 4096;
        }
        char *p = realloc(b->data, nc);
        if (!p) {
            abort();
        }
        b->data = p;
        b->cap = nc;
    }
    memcpy(b->data + b->off + b->len, src, n);
    b->len += n;
}

static void netbuf_consume(NetBuf *b, size_t n) {
    b->off += n;
    b->len -= n;
    if (b->len == 0) {
        b->off = 0;
    }
}

static void netbuf_free(NetBuf *b) {
    free(b->data);
    memset(b, 0, sizeof *b);
}

/* ---- the registry and the lifecycle ----------------------------------------- */

static void net_registry_add(NetSocket *s) {
    NetSocket **n = realloc(net_sockets, sizeof(NetSocket *) * (net_socket_count + 1));
    if (!n) {
        abort();
    }
    net_sockets = n;
    net_sockets[net_socket_count++] = s;
}

static void net_registry_remove(NetSocket *s) {
    for (size_t i = 0; i < net_socket_count; i++) {
        if (net_sockets[i] == s) {
            memmove(&net_sockets[i], &net_sockets[i + 1],
                    sizeof(NetSocket *) * (net_socket_count - i - 1));
            net_socket_count--;
            return;
        }
    }
}

static void net_resolve_finish(NetResolve *r) {
    if (!r) {
        return;
    }
    pthread_join(r->thread, NULL);
    close(r->pipe_fd[0]);
    close(r->pipe_fd[1]);
    if (r->result) {
        freeaddrinfo(r->result);
    }
    free(r->host);
    free(r);
}

/* Close the descriptor; the struct stays readable (its buffer included, so a
 * program can take what arrived before the close). */
#if HAVE_LIBSSL
static void net_tls_server_release(NetTlsServer *t);
#endif

static void net_close_fd(NetSocket *s) {
#if HAVE_LIBSSL
    if (s->ssl) {
        /* close_notify, once and without waiting: it tells the peer the end
         * was meant, which is how it tells a close from a truncation. */
        if (s->fd >= 0 && !s->tls_handshaking && !s->tls_no_notify) {
            ERR_clear_error();
            SSL_shutdown(s->ssl);
        }
        SSL_free(s->ssl);
        s->ssl = NULL;
        ERR_clear_error();
    }
    s->tls_handshaking = 0;
#endif
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
    if (s->kind == NET_LISTENER && s->unlink_on_close && s->path) {
        unlink(s->path);
        s->unlink_on_close = 0;
    }
    if (s->kind == NET_CONNECTION && s->listener && s->open) {
        if (s->listener->children > 0) {
            s->listener->children--;
        }
    }
    s->open = 0;
    s->connecting = 0;
    if (s->addrs) {
        freeaddrinfo(s->addrs);
        s->addrs = NULL;
        s->next_addr = NULL;
    }
}

/* The half-close: TLS's close_notify on a TLS connection (the peer may still
 * send), the socket's own on a plain one. */
static void net_shut_write(NetSocket *s) {
#if HAVE_LIBSSL
    if (s->ssl) {
        ERR_clear_error();
        SSL_shutdown(s->ssl);
        ERR_clear_error();
        return;
    }
#endif
    shutdown(s->fd, SHUT_WR);
}

static void net_socket_free(NetSocket *s) {
    net_close_fd(s);
    if (s->resolve) {
        net_resolve_finish(s->resolve);
        s->resolve = NULL;
    }
    net_registry_remove(s);
    netbuf_free(&s->in);
    netbuf_free(&s->out);
    free(s->close_code);
    free(s->error_code);
    free(s->error_message);
    free(s->path);
    free(s->host);
    value_free(s->tls_info);
    free(s->dgram_buf);
    value_free(s->dgram_pending);
    value_free(s->accept_request);
    value_free(s->state);
#if HAVE_LIBSSL
    if (s->tls_server) {
        net_tls_server_release(s->tls_server);
    }
#endif
    free(s);
}

static void net_socket_release(NetSocket *s) {
    if (s && --s->ref_count == 0) {
        net_socket_free(s);
    }
}

static void net_drop_loop_ref(NetSocket *s) {
    if (s->loop_ref) {
        s->loop_ref = 0;
        net_socket_release(s);
    }
}

static NetSocket *net_socket_new(NetKind kind) {
    NetSocket *s = calloc(1, sizeof *s);
    if (!s) {
        abort();
    }
    s->kind = kind;
    s->id = net_next_id++;
    s->fd = -1;
    s->ref_count = 1;
    s->in_ceiling = NET_DEFAULT_BUFFER;
    s->out_ceiling = NET_DEFAULT_BUFFER;
    s->last_activity = net_now();
    s->tls_info = value_null();
    s->dgram_pending = value_null();
    s->accept_request = value_null();
    s->state = value_record(NULL, 0);
    net_registry_add(s);
    if (!net_sigpipe_ignored) {
        /* A write to a peer that left must be an ERROR, never a process
         * death; MSG_NOSIGNAL covers send where it exists, this the rest. */
#ifdef SIGPIPE
        signal(SIGPIPE, SIG_IGN);
#endif
        net_sigpipe_ignored = 1;
    }
    return s;
}

/* A failure on a watched socket: an `error` event and then a `closed` one. */
static void net_fail(NetSocket *s, const char *code, const char *message) {
    if (!s->error_code) {
        s->error_code = copy_string(code);
        s->error_message = copy_string(message);
        s->error_pending = 1;
    }
    if (!s->close_code) {
        s->close_code = copy_string(code);
    }
    net_close_fd(s);
    s->closed_pending = 1;
}

static void net_mark_closed(NetSocket *s, const char *code) {
    if (!s->close_code) {
        s->close_code = copy_string(code);
    }
    net_close_fd(s);
    s->closed_pending = 1;
}

/* ---- the events global ------------------------------------------------------ */

static void net_bind_events_global(void) {
    if (env_find_in_frame(&global_env, "net")) {
        return;     /* a program that bound it first keeps what it wrote */
    }
    Value record = value_record(NULL, 0);
    record_set(&record, "events", value_array(NULL, 0));
    Symbol *items = realloc(global_env.items, sizeof(Symbol) * (global_env.count + 1));
    if (!items) {
        abort();
    }
    global_env.items = items;
    global_env.items[global_env.count].name = copy_string("net");
    memset(&global_env.items[global_env.count].lock, 0, sizeof(TypeLock));
    global_env.items[global_env.count].value = record;
    global_env.count++;
}

static int net_events_watched(void) {
    for (size_t i = 0; i < watcher_count; i++) {
        if (watchers[i].active && watcher_matches_change(watchers[i].stmt, "net.events")) {
            return 1;
        }
    }
    return 0;
}

/* Append one event and fire the watcher. Returns 0 when the watcher raised.
 * A datagram socket's events name it as `socket` (it is neither a connection
 * nor a listener), and carry `extra`'s fields: data, from, truncated, size. */
static int net_emit_full(const char *kind, NetSocket *conn, NetSocket *listener,
                         const char *code, const char *message, Value *extra) {
    Symbol *symbol = env_find_in_frame(&global_env, "net");
    if (!symbol || symbol->value.kind != VALUE_RECORD) {
        return 1;
    }
    RecordField *events = record_find(&symbol->value, "events");
    if (!events || events->value->kind != VALUE_ARRAY) {
        return 1;
    }
    NetSocket *subject = conn ? conn : listener;
    Value e = value_record(NULL, 0);
    record_set(&e, "kind", value_string(kind));
    record_set(&e, "id", value_number_exact(subject ? subject->id : 0));
    if (conn && conn->kind == NET_DATAGRAM) {
        conn->ref_count++;
        record_set(&e, "socket", value_net(conn));
        conn = NULL;
    }
    if (conn) {
        conn->ref_count++;
        record_set(&e, "connection", value_net(conn));
    } else {
        record_set(&e, "connection", value_null());
    }
    if (listener) {
        listener->ref_count++;
        record_set(&e, "listener", value_net(listener));
    } else {
        record_set(&e, "listener", value_null());
    }
    record_set(&e, "code", code ? value_string(code) : value_null());
    record_set(&e, "message", message ? value_string(message) : value_null());
    if (extra && extra->kind == VALUE_RECORD) {
        for (size_t i = 0; i < extra->as.record.count; i++) {
            record_set(&e, extra->as.record.fields[i].name, value_copy(*extra->as.record.fields[i].value));
        }
    }
    Value ignored = append_to_array_ref(events->value, e, 0);
    value_free(ignored);
    return watcher_trigger_delivery("net.events", NULL);
}

static int net_emit(const char *kind, NetSocket *conn, NetSocket *listener,
                    const char *code, const char *message) {
    return net_emit_full(kind, conn, listener, code, message, NULL);
}

/* ---- reading and writing the descriptor ------------------------------------- */

/* Read what the socket has, up to the ceiling. Returns the bytes read, or -1
 * on a failure (code and message set). End of stream sets peer_closed. */
#if HAVE_LIBSSL
static long net_tls_fill(NetSocket *s, const char **code, char *message, size_t msize);
static long net_tls_flush(NetSocket *s, const char **code, char *message, size_t msize);
#endif

static long net_fill(NetSocket *s, const char **code, char *message, size_t msize) {
    if (s->tls_handshaking) {
        return 0;            /* nothing is the program's until the handshake is done */
    }
#if HAVE_LIBSSL
    if (s->ssl) {
        return net_tls_fill(s, code, message, msize);
    }
#endif
    long total = 0;
    char chunk[NET_READ_CHUNK];
    while (s->fd >= 0 && !s->peer_closed && s->in.len < s->in_ceiling) {
        size_t room = s->in_ceiling - s->in.len;
        ssize_t n = recv(s->fd, chunk, room < sizeof chunk ? room : sizeof chunk, 0);
        if (n > 0) {
            netbuf_append(&s->in, chunk, (size_t)n);
            total += n;
            continue;
        }
        if (n == 0) {
            s->peer_closed = 1;
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        *code = net_errno_code(errno);
        snprintf(message, msize, "net: the connection failed: %s", strerror(errno));
        return -1;
    }
    if (total > 0) {
        s->last_activity = net_now();
    }
    return total;
}

static long net_flush_some(NetSocket *s, const char **code, char *message, size_t msize) {
    if (s->tls_handshaking) {
        return 0;            /* what was written waits for the handshake */
    }
#if HAVE_LIBSSL
    if (s->ssl) {
        return net_tls_flush(s, code, message, msize);
    }
#endif
    long total = 0;
    while (s->fd >= 0 && s->out.len > 0) {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        ssize_t n = send(s->fd, s->out.data + s->out.off, s->out.len, flags);
        if (n > 0) {
            netbuf_consume(&s->out, (size_t)n);
            total += n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        *code = net_errno_code(errno);
        snprintf(message, msize, "net: the connection failed while sending: %s", strerror(errno));
        return -1;
    }
    if (total > 0) {
        s->last_activity = net_now();
    }
    return total;
}

static int net_set_nonblocking(int fd) {
#ifdef _WIN32
    return gb_sock_set_blocking(fd, 0);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static void net_socket_options(int fd, int family) {
    int one = 1;
    if (family != AF_UNIX) {
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    }
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}

/* macOS names the idle time TCP_KEEPALIVE. */
#if !defined(TCP_KEEPIDLE) && defined(TCP_KEEPALIVE)
#define TCP_KEEPIDLE TCP_KEEPALIVE
#endif

/* Keepalive on a TCP socket. 0 with errno when the system refused a setting:
 * a keepalive asked for and not set is a connection that is not watched for
 * a dead peer, and nothing would say so, so the caller refuses the connect. */
static int net_apply_keepalive(int fd, const NetKeepalive *k) {
    if (!k->on) {
        return 1;
    }
    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one) != 0) {
        return 0;
    }
#if defined(TCP_KEEPIDLE) && defined(TCP_KEEPINTVL) && defined(TCP_KEEPCNT)
    if ((k->idle && setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &k->idle, sizeof k->idle) != 0) ||
        (k->interval && setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &k->interval, sizeof k->interval) != 0) ||
        (k->count && setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &k->count, sizeof k->count) != 0)) {
        return 0;
    }
#endif
    return 1;
}

/* What the SYSTEM says the socket's keepalive is, not what was asked: false,
 * or { idle, interval, count } in seconds and probes. */
static Value net_keepalive_now(int fd) {
    int on = 0;
    socklen_t len = sizeof on;
    if (getsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, &len) != 0) {
        return value_unknown();
    }
    if (!on) {
        return value_bool(0);
    }
    Value r = value_record(NULL, 0);
#if defined(TCP_KEEPIDLE) && defined(TCP_KEEPINTVL) && defined(TCP_KEEPCNT)
    static const int opts[] = { TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT };
    static const char *const names[] = { "idle", "interval", "count" };
    for (int i = 0; i < 3; i++) {
        int v = 0;
        len = sizeof v;
        record_set(&r, names[i], getsockopt(fd, IPPROTO_TCP, opts[i], &v, &len) == 0
                                     ? value_number_exact(v) : value_unknown());
    }
#else
    record_set(&r, "idle", value_unknown());
    record_set(&r, "interval", value_unknown());
    record_set(&r, "count", value_unknown());
#endif
    return r;
}

/* ---- options -------------------------------------------------------------------- */

/* A duration or a number of seconds; -1 having raised. */
static double net_seconds(Value v, const char *what) {
    char msg[200];
    if (v.kind == VALUE_NUMBER && v.as.num.value >= 0) {
        return v.as.num.value;
    }
    if (v.kind == VALUE_DURATION) {
        long long months = 0, seconds = 0;
        duration_totals(v.as.duration, &months, &seconds);
        if (months == 0 && seconds >= 0) {
            return (double)seconds;
        }
    }
    snprintf(msg, sizeof msg, "%s must be a duration (30 seconds) or a number of seconds", what);
    net_refuse(msg);
    return -1;
}

static int net_check_options(Value opts, const char *verb, const char *const *known) {
    char msg[400];
    for (size_t i = 0; i < opts.as.record.count; i++) {
        const char *name = opts.as.record.fields[i].name;
        int ok = 0;
        for (size_t k = 0; known[k]; k++) {
            if (strcmp(known[k], name) == 0) {
                ok = 1;
                break;
            }
        }
        if (!ok) {
            size_t at = (size_t)snprintf(msg, sizeof msg, "%s: unknown option '%s' (known:", verb, name);
            for (size_t k = 0; known[k] && at < sizeof msg; k++) {
                at += (size_t)snprintf(msg + at, sizeof msg - at, "%s %s", k ? "," : "", known[k]);
            }
            if (at < sizeof msg) {
                snprintf(msg + at, sizeof msg - at, ")");
            }
            net_refuse(msg);
            return 0;
        }
    }
    return 1;
}

static Value *net_opt(Value *opts, const char *name) {
    RecordField *f = record_find(opts, name);
    return f && f->value->kind != VALUE_NULL ? f->value : NULL;
}

static int net_opt_text(Value *opts, const char *name, const char *verb, const char **out) {
    Value *v = net_opt(opts, name);
    *out = NULL;
    if (!v) {
        return 1;
    }
    if (v->kind != VALUE_STRING) {
        char msg[160];
        snprintf(msg, sizeof msg, "%s: %s must be text", verb, name);
        net_refuse(msg);
        return 0;
    }
    *out = v->as.string;
    return 1;
}

static int net_opt_count(Value *opts, const char *name, const char *verb, double min, double max,
                         double *out) {
    Value *v = net_opt(opts, name);
    if (!v) {
        return 1;
    }
    if (v->kind != VALUE_NUMBER || v->as.num.value != floor(v->as.num.value) ||
        v->as.num.value < min || v->as.num.value > max) {
        char msg[200];
        snprintf(msg, sizeof msg, "%s: %s must be a whole number from %.0f to %.0f", verb, name, min, max);
        net_refuse(msg);
        return 0;
    }
    *out = v->as.num.value;
    return 1;
}

static int net_opt_seconds(Value *opts, const char *name, const char *verb, double *out) {
    Value *v = net_opt(opts, name);
    if (!v) {
        return 1;
    }
    char what[120];
    snprintf(what, sizeof what, "%s: %s", verb, name);
    double s = net_seconds(*v, what);
    if (s < 0) {
        return 0;
    }
    *out = s;
    return 1;
}

/* `keepalive:` -- true (the system's timings), false, or a record of any of
 * { idle, interval, count }. The limits are Linux's (idle and interval up to
 * 32767 seconds, count up to 127); whole seconds, since that is the unit the
 * system takes. */
static int net_opt_keepalive(Value *opts, const char *verb, NetKeepalive *out) {
    memset(out, 0, sizeof *out);
    Value *v = net_opt(opts, "keepalive");
    char msg[240];
    if (!v || (v->kind == VALUE_BOOL && !v->as.boolean)) {
        return 1;
    }
    if (v->kind == VALUE_BOOL) {
        out->on = 1;
        return 1;
    }
    if (v->kind != VALUE_RECORD) {
        snprintf(msg, sizeof msg, "%s: keepalive must be true, false or a record { idle, interval, count }",
                 verb);
        net_refuse(msg);
        return 0;
    }
    static const char *const known[] = { "idle", "interval", "count", NULL };
    char what[80];
    snprintf(what, sizeof what, "%s keepalive", verb);
    if (!net_check_options(*v, what, known)) {
        return 0;
    }
    double idle = 0, interval = 0, count = 0;
    if (!net_opt_seconds(v, "idle", what, &idle) || !net_opt_seconds(v, "interval", what, &interval) ||
        !net_opt_count(v, "count", what, 1, 127, &count)) {
        return 0;
    }
    if ((net_opt(v, "idle") && (idle < 1 || idle > 32767 || idle != floor(idle))) ||
        (net_opt(v, "interval") && (interval < 1 || interval > 32767 || interval != floor(interval)))) {
        snprintf(msg, sizeof msg, "%s: keepalive's idle and interval must be whole seconds from 1 to 32767",
                 verb);
        net_refuse(msg);
        return 0;
    }
#if !(defined(TCP_KEEPIDLE) && defined(TCP_KEEPINTVL) && defined(TCP_KEEPCNT))
    if (idle || interval || count) {
        snprintf(msg, sizeof msg, "%s: this system does not let a program set keepalive's timings; "
                 "keepalive: true uses its own", verb);
        net_refuse(msg);
        return 0;
    }
#endif
    out->on = 1;
    out->idle = (int)idle;
    out->interval = (int)interval;
    out->count = (int)count;
    return 1;
}

/* ---- addresses ------------------------------------------------------------------- */

static Value net_address_record(const struct sockaddr *sa) {
    Value r = value_record(NULL, 0);
    char host[INET6_ADDRSTRLEN] = "";
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
        inet_ntop(AF_INET, &in->sin_addr, host, sizeof host);
        record_set(&r, "address", value_string(host));
        record_set(&r, "port", value_number_exact(ntohs(in->sin_port)));
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;
        inet_ntop(AF_INET6, &in6->sin6_addr, host, sizeof host);
        record_set(&r, "address", value_string(host));
        record_set(&r, "port", value_number_exact(ntohs(in6->sin6_port)));
    } else if (sa->sa_family == AF_UNIX) {
        const struct sockaddr_un *un = (const struct sockaddr_un *)sa;
        record_set(&r, "path", value_string(un->sun_path));
    }
    return r;
}

static Value net_endpoint(NetSocket *s, int peer) {
    if (s->fd < 0) {
        return value_unknown();
    }
    struct sockaddr_storage ss;
    socklen_t len = sizeof ss;
    memset(&ss, 0, sizeof ss);
    int rc = peer ? getpeername(s->fd, (struct sockaddr *)&ss, &len)
                  : getsockname(s->fd, (struct sockaddr *)&ss, &len);
    if (rc != 0) {
        return value_unknown();
    }
    if (ss.ss_family == AF_UNIX && s->path) {
        /* the kernel answers an unnamed peer with an empty path */
        Value r = value_record(NULL, 0);
        record_set(&r, "path", value_string(s->path));
        return r;
    }
    return net_address_record((struct sockaddr *)&ss);
}

static Value net_peer_credentials(NetSocket *s) {
    if (s->fd < 0 || s->family != AF_UNIX || s->kind != NET_CONNECTION) {
        return value_unknown();
    }
    Value r = value_record(NULL, 0);
#if defined(SO_PEERCRED)
    struct ucred cred;
    socklen_t len = sizeof cred;
    if (getsockopt(s->fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) {
        value_free(r);
        return value_unknown();
    }
    record_set(&r, "pid", value_number_exact(cred.pid));
    record_set(&r, "uid", value_number_exact(cred.uid));
    record_set(&r, "gid", value_number_exact(cred.gid));
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    uid_t uid;
    gid_t gid;
    if (getpeereid(s->fd, &uid, &gid) != 0) {
        value_free(r);
        return value_unknown();
    }
    record_set(&r, "pid", value_unknown());
    record_set(&r, "uid", value_number_exact(uid));
    record_set(&r, "gid", value_number_exact(gid));
#elif defined(_WIN32)
    long pid = net_w_peer_pid(s->fd);
    if (pid < 0) {
        value_free(r);
        return value_unknown();
    }
    record_set(&r, "pid", value_number_exact(pid));
    record_set(&r, "uid", value_unknown());   /* Windows has no uid to give */
    record_set(&r, "gid", value_unknown());
#else
    value_free(r);
    return value_unknown();
#endif
    return r;
}

/* ---- connecting -------------------------------------------------------------------- */

static int net_unix_address(const char *path, struct sockaddr_un *a, const char *verb) {
    memset(a, 0, sizeof *a);
    a->sun_family = AF_UNIX;
    if (strlen(path) >= sizeof a->sun_path) {
        char msg[200];
        snprintf(msg, sizeof msg, "%s: the path is longer than a Unix socket path can be (%zu bytes)",
                 verb, sizeof a->sun_path - 1);
        net_refuse(msg);
        return 0;
    }
    memcpy(a->sun_path, path, strlen(path));
    return 1;
}

static const char *net_gai_code(int rc) {
    switch (rc) {
    case EAI_NONAME:
#ifdef EAI_NODATA
    case EAI_NODATA:
#endif
    case EAI_FAIL:
        return "name_not_found";
    case EAI_AGAIN:
        return "timeout";
    default:
        return "name_not_found";
    }
}

/* ---- TLS (N2) ---------------------------------------------------------------------- *
 *
 * docs/networking_design.md §5. TLS rides UNDER the buffers: net_fill and
 * net_flush_some hand a TLS socket to the two functions here, so the take and
 * read families, backpressure, idle timeouts and the events are the same code
 * for a TLS connection as for a plain one. What TLS adds is a HANDSHAKE, which
 * runs in the loop (watched) or within a deadline (waited), and two
 * directions the loop must now poll for: a read that needs to write (a key
 * update) and a write that needs to read.
 *
 * VERIFICATION IS ON for a client: the system trust store (or `ca:`), and the
 * name checked against the certificate. `verify: false` is spelled out by
 * whoever wants it. */

#if HAVE_LIBSSL
static SSL_CTX *net_tls_default_client = NULL;   /* `tls: true`: built once, shared */

static void net_raise_reason(const char *code, const char *message, const char *reason) {
    if (raise_in_flight) {
        return;
    }
    Value details = value_record(NULL, 0);
    record_set(&details, "code", value_string(code));
    if (reason) {
        record_set(&details, "reason", value_string(reason));
    }
    error_set_state_full(message, NET_ERROR_CODE, "net", details, value_null(), 0, 0);
    error_generation++;
    raise_in_flight = 1;
}

/* libssl's own words for the newest queued error, or a fallback. */
static const char *net_tls_error_text(const char *fallback) {
    unsigned long e = ERR_peek_last_error();
    const char *r = e ? ERR_reason_error_string(e) : NULL;
    return r ? r : fallback;
}

/* The errno behind an SSL_ERROR_SYSCALL. OpenSSL 3 also queues it as a
 * system error, so "the queue is empty" is not the test for a system failure
 * (measured: a refused client certificate under valgrind's timing reported "a
 * protocol error" for what was EPIPE). */
static int net_tls_errno(void) {
    if (errno) {
        return errno;
    }
#ifdef ERR_SYSTEM_ERROR
    unsigned long e = ERR_peek_last_error();
    if (e && ERR_SYSTEM_ERROR(e)) {
        return ERR_GET_REASON(e);
    }
#endif
    return 0;
}

static SSL_CTX *net_tls_client_ctx(const char *ca, const char *cert, const char *key, int verify,
                                   const char *verb) {
    char msg[600];
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        net_raise_reason("tls_failed", "net: could not create a TLS context", net_tls_error_text(NULL));
        return NULL;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
    SSL_CTX_set_options(ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);
#endif
    if (verify) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        struct stat st;
        int loaded = ca ? (stat(ca, &st) == 0 && S_ISDIR(st.st_mode)
                               ? SSL_CTX_load_verify_locations(ctx, NULL, ca)
                               : SSL_CTX_load_verify_locations(ctx, ca, NULL))
                        : SSL_CTX_set_default_verify_paths(ctx);
        if (loaded != 1) {
            if (ca) {
                snprintf(msg, sizeof msg, "%s: could not load the certificate authorities in '%s'", verb, ca);
            } else {
                snprintf(msg, sizeof msg, "%s: could not load the system's trusted certificates", verb);
            }
            SSL_CTX_free(ctx);
            net_raise_reason("tls_failed", msg, net_tls_error_text(NULL));
            return NULL;
        }
    }
    if (cert || key) {
        if (!cert || !key) {
            SSL_CTX_free(ctx);
            snprintf(msg, sizeof msg, "%s: a client certificate needs both cert and key", verb);
            net_refuse(msg);
            return NULL;
        }
        if (SSL_CTX_use_certificate_chain_file(ctx, cert) != 1) {
            snprintf(msg, sizeof msg, "%s: could not load certificate '%s'", verb, cert);
            SSL_CTX_free(ctx);
            net_raise_reason("tls_failed", msg, net_tls_error_text(NULL));
            return NULL;
        }
        if (SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) != 1 || SSL_CTX_check_private_key(ctx) != 1) {
            snprintf(msg, sizeof msg,
                     "%s: could not use private key '%s' (unreadable, or it does not match the certificate)",
                     verb, key);
            SSL_CTX_free(ctx);
            net_raise_reason("tls_failed", msg, net_tls_error_text(NULL));
            return NULL;
        }
    }
    ERR_clear_error();
    return ctx;
}

static SSL *net_tls_new_ssl(SSL_CTX *ctx) {
    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        abort();
    }
    /* The send queue is a buffer that moves (compaction) and grows (more
     * writes) between a write libssl could not finish and its retry; these
     * two modes are what make that legal. */
    SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    return ssl;
}

/* A client's SSL for `tls:` (true, or a record), ready for its descriptor.
 * `host` is the name connected to, the default server_name. NULL having raised. */
static SSL *net_tls_client_ssl(Value *opt, const char *verb, const char *host) {
    static const char *const known[] = { "server_name", "ca", "verify", "cert", "key", NULL };
    char msg[400];
    const char *server_name = host, *ca = NULL, *cert = NULL, *key = NULL;
    int verify = 1;
    char where[96];
    snprintf(where, sizeof where, "%s tls", verb);
    if (opt->kind == VALUE_RECORD) {
        if (!net_check_options(*opt, where, known)) {
            return NULL;
        }
        const char *name = NULL;
        if (!net_opt_text(opt, "server_name", where, &name) || !net_opt_text(opt, "ca", where, &ca) ||
            !net_opt_text(opt, "cert", where, &cert) || !net_opt_text(opt, "key", where, &key)) {
            return NULL;
        }
        if (name) {
            server_name = name;
        }
        Value *v = net_opt(opt, "verify");
        if (v) {
            if (v->kind != VALUE_BOOL) {
                snprintf(msg, sizeof msg, "%s: verify must be true or false", where);
                net_refuse(msg);
                return NULL;
            }
            verify = v->as.boolean;
        }
    } else if (opt->kind != VALUE_BOOL || !opt->as.boolean) {
        snprintf(msg, sizeof msg, "%s: tls must be true or a record such as { server_name: \"example.com\" }", verb);
        net_refuse(msg);
        return NULL;
    }
    if (verify && !server_name) {
        snprintf(msg, sizeof msg,
                 "%s: a Unix socket has no host name to check the certificate against; give tls a "
                 "server_name, or verify: false", verb);
        net_refuse(msg);
        return NULL;
    }
    SSL_CTX *ctx = NULL;
    if (verify && !ca && !cert && !key) {
        if (!net_tls_default_client) {
            net_tls_default_client = net_tls_client_ctx(NULL, NULL, NULL, 1, verb);
            if (!net_tls_default_client) {
                return NULL;
            }
        }
        ctx = net_tls_default_client;
        SSL_CTX_up_ref(ctx);
    } else {
        ctx = net_tls_client_ctx(ca, cert, key, verify, verb);
        if (!ctx) {
            return NULL;
        }
    }
    SSL *ssl = net_tls_new_ssl(ctx);
    SSL_CTX_free(ctx);                  /* the SSL holds its own reference */
    SSL_set_connect_state(ssl);
    if (server_name) {
        unsigned char probe[sizeof(struct in6_addr)];
        int literal = inet_pton(AF_INET, server_name, probe) == 1 || inet_pton(AF_INET6, server_name, probe) == 1;
        if (!literal) {
            SSL_set_tlsext_host_name(ssl, server_name);    /* SNI is for names, never addresses */
        }
        if (verify) {
            X509_VERIFY_PARAM *param = SSL_get0_param(ssl);
            X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            int ok = literal ? X509_VERIFY_PARAM_set1_ip_asc(param, server_name)
                             : X509_VERIFY_PARAM_set1_host(param, server_name, 0);
            if (ok != 1) {
                SSL_free(ssl);
                snprintf(msg, sizeof msg, "%s: '%s' cannot be checked against a certificate", verb, server_name);
                net_refuse(msg);
                return NULL;
            }
        }
    }
    return ssl;
}

static void net_tls_server_release(NetTlsServer *t) {
    if (!t || --t->refs > 0) {
        return;
    }
    for (size_t i = 0; i < t->count; i++) {
        SSL_CTX_free(t->ctxs[i]);
        free(t->hosts[i]);
    }
    free(t->ctxs);
    free(t->hosts);
    SSL_CTX_free(t->dflt);
    free(t);
}

/* SNI, as webserver_sni_select does it: an exact, case-insensitive name, and
 * the default context for a name it does not know. */
static int net_tls_sni_select(SSL *ssl, int *alert, void *arg) {
    (void)alert;
    NetTlsServer *t = (NetTlsServer *)arg;
    const char *name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (t && name) {
        for (size_t i = 0; i < t->count; i++) {
            if (strcasecmp(t->hosts[i], name) == 0) {
                SSL_set_SSL_CTX(ssl, t->ctxs[i]);
                break;
            }
        }
    }
    return SSL_TLSEXT_ERR_OK;
}

/* A listener's (or a server-side start_tls's) contexts from its `tls:` record.
 * NULL having raised. */
static NetTlsServer *net_tls_server_new(Value *opt, const char *verb) {
    static const char *const known[] = { "cert", "key", "certs", "client_ca", "require_client_cert", NULL };
    char msg[600];
    char where[96];
    snprintf(where, sizeof where, "%s tls", verb);
    if (opt->kind != VALUE_RECORD) {
        snprintf(msg, sizeof msg, "%s: a server's tls must be a record with cert and key (or certs:)", verb);
        net_refuse(msg);
        return NULL;
    }
    const char *client_ca = NULL;
    if (!net_check_options(*opt, where, known) || !net_opt_text(opt, "client_ca", where, &client_ca)) {
        return NULL;
    }
    int require = 0;
    Value *rv = net_opt(opt, "require_client_cert");
    if (rv) {
        if (rv->kind != VALUE_BOOL) {
            snprintf(msg, sizeof msg, "%s: require_client_cert must be true or false", where);
            net_refuse(msg);
            return NULL;
        }
        require = rv->as.boolean;
    }
    if (require && !client_ca) {
        snprintf(msg, sizeof msg,
                 "%s: require_client_cert needs client_ca, the authorities a client's certificate must "
                 "come from", where);
        net_refuse(msg);
        return NULL;
    }
    NetTlsServer *t = calloc(1, sizeof *t);
    if (!t) {
        abort();
    }
    t->refs = 1;
    if (!webserver_tls_build(opt, verb, msg, sizeof msg, &t->dflt, &t->hosts, &t->ctxs, &t->count)) {
        free(t);
        net_raise_reason("tls_failed", msg, net_tls_error_text(NULL));
        ERR_clear_error();
        return NULL;
    }
    t->client_certificate = client_ca ? (require ? 2 : 1) : 0;
    /* Every context gets the same settings, because SNI swaps the context in
     * mid-handshake and the one it swaps in is the one that verifies. */
    for (size_t i = 0; i <= t->count; i++) {
        SSL_CTX *ctx = i < t->count ? t->ctxs[i] : t->dflt;
        if (i == t->count && t->count > 0 && t->dflt == t->ctxs[0]) {
            break;                       /* the default IS the first named context */
        }
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
        SSL_CTX_set_options(ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);
#endif
        SSL_CTX_set_session_id_context(ctx, (const unsigned char *)"gbasic-net", 10);
        /* client_ca and require_client_cert were applied by webserver_tls_build */
    }
    SSL_CTX_set_tlsext_servername_callback(t->dflt, net_tls_sni_select);
    SSL_CTX_set_tlsext_servername_arg(t->dflt, t);
    ERR_clear_error();
    return t;
}

/* Begin a handshake on a connected descriptor. The first step is taken by the
 * caller or by the loop, which polls for writing first: a client's opening
 * flight is a write. */
static void net_tls_attach(NetSocket *s, SSL *ssl, int why) {
    s->ssl = ssl;
    SSL_set_fd(ssl, s->fd);
    s->tls_handshaking = why;
    s->tls_want_write = SSL_is_server(ssl) ? 0 : 1;
}

static Value net_tls_name_text(X509_NAME *name) {
    BIO *b = BIO_new(BIO_s_mem());
    if (!b) {
        abort();
    }
    X509_NAME_print_ex(b, name, 0, XN_FLAG_RFC2253 & ~ASN1_STRFLGS_ESC_MSB);
    char *data = NULL;
    long n = BIO_get_mem_data(b, &data);
    Value v = value_string_n(data ? data : "", n > 0 ? (size_t)n : 0);
    BIO_free(b);
    return v;
}

static Value net_tls_time(const ASN1_TIME *t) {
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    if (!t || ASN1_TIME_to_tm(t, &tm) != 1) {
        return value_unknown();
    }
    DateTime dt = {0};
    dt.year = tm.tm_year + 1900;
    dt.month = tm.tm_mon + 1;
    dt.day = tm.tm_mday;
    dt.hour = tm.tm_hour;
    dt.minute = tm.tm_min;
    dt.second = tm.tm_sec;
    dt.precision = PREC_SECOND;
    return value_datetime(dt);
}

/* The facts of a finished handshake, taken once: they outlive the SSL, which
 * is freed at close, so a program reads c.tls in its `closed` handler too. */
static Value net_tls_snapshot(SSL *ssl) {
    Value r = value_record(NULL, 0);
    record_set(&r, "version", value_string(SSL_get_version(ssl)));
    const char *cipher = SSL_get_cipher_name(ssl);
    record_set(&r, "cipher", cipher ? value_string(cipher) : value_unknown());
    const char *name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    record_set(&r, "server_name", name ? value_string(name) : value_unknown());
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    X509 *peer = SSL_get1_peer_certificate(ssl);
#else
    X509 *peer = SSL_get_peer_certificate(ssl);
#endif
    if (!peer) {
        record_set(&r, "peer_certificate", value_null());
        return r;
    }
    Value c = value_record(NULL, 0);
    record_set(&c, "subject", net_tls_name_text(X509_get_subject_name(peer)));
    record_set(&c, "issuer", net_tls_name_text(X509_get_issuer_name(peer)));
    record_set(&c, "not_before", net_tls_time(X509_get0_notBefore(peer)));
    record_set(&c, "not_after", net_tls_time(X509_get0_notAfter(peer)));
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdlen = 0;
    char hex[EVP_MAX_MD_SIZE * 2 + 1];
    if (X509_digest(peer, EVP_sha256(), md, &mdlen) == 1) {
        for (unsigned int i = 0; i < mdlen; i++) {
            snprintf(hex + i * 2, 3, "%02x", md[i]);
        }
        record_set(&c, "sha256", value_string_n(hex, (size_t)mdlen * 2));
    } else {
        record_set(&c, "sha256", value_unknown());
    }
    X509_free(peer);
    record_set(&r, "peer_certificate", c);
    return r;
}

/* One step of the handshake: 1 finished, 0 still going, -1 failed with the
 * code, message and reason set. */
static int net_tls_step(NetSocket *s, const char **code, char *msg, size_t msize, char *reason, size_t rsize) {
    ERR_clear_error();
    errno = 0;
    int r = SSL_do_handshake(s->ssl);
    if (r == 1) {
        int why = s->tls_handshaking;
        s->tls_handshaking = 0;
        value_free(s->tls_info);
        s->tls_info = net_tls_snapshot(s->ssl);
        if (s->watched) {
            if (why == NET_TLS_CONNECT) {
                s->connected_pending = 1;
            } else if (why == NET_TLS_UPGRADE) {
                s->tls_event_pending = 1;
            } else if (why == NET_TLS_ACCEPT) {
                s->connected_pending = 2;
            }
        }
        s->last_activity = net_now();
        return 1;
    }
    int e = SSL_get_error(s->ssl, r);
    if (e == SSL_ERROR_WANT_READ) {
        s->tls_want_write = 0;
        return 0;
    }
    if (e == SSL_ERROR_WANT_WRITE) {
        s->tls_want_write = 1;
        return 0;
    }
    long vr = SSL_get_verify_result(s->ssl);
    if (vr != X509_V_OK) {
        *code = "tls_verify";
        snprintf(reason, rsize, "%s", X509_verify_cert_error_string(vr));
        snprintf(msg, msize, "net: the %s certificate did not verify: %s",
                 SSL_is_server(s->ssl) ? "client's" : "server's", reason);
    } else {
        *code = "tls_failed";
        if (e == SSL_ERROR_SYSCALL) {
            int err = net_tls_errno();
            snprintf(reason, rsize, "%s", err ? strerror(err) : "the peer closed the connection during the handshake");
        } else {
            snprintf(reason, rsize, "%s", net_tls_error_text("the handshake failed"));
        }
        snprintf(msg, msize, "net: the TLS handshake failed: %s", reason);
    }
    ERR_clear_error();
    return -1;
}

/* A waited handshake, within a deadline (<= 0: none). 1 done, 0 raised. */
static int net_tls_wait(NetSocket *s, double deadline, const char *verb) {
    char msg[400], reason[256];
    for (;;) {
        const char *code = NULL;
        int r = net_tls_step(s, &code, msg, sizeof msg, reason, sizeof reason);
        if (r == 1) {
            return 1;
        }
        if (r < 0) {
            net_raise_reason(code, msg, reason);
            return 0;
        }
        double left = deadline > 0 ? deadline - net_now() : -1;
        if (deadline > 0 && left <= 0) {
            snprintf(msg, sizeof msg, "%s: the TLS handshake did not finish within the timeout", verb);
            net_raise("timeout", msg);
            return 0;
        }
        struct pollfd p = { s->fd, (short)(s->tls_want_write ? POLLOUT : POLLIN), 0 };
        int pr = poll(&p, 1, left < 0 ? -1 : (int)(left * 1000) + 1);
        if (pr < 0 && errno != EINTR) {
            net_raise("failed", "net: poll failed during the TLS handshake");
            return 0;
        }
    }
}

/* After a send failed because the peer had gone: did it say why first? A TLS
 * 1.3 server refuses a client certificate AFTER the client's handshake has
 * finished, so its alert can be waiting, unread, when the client's next write
 * finds the socket closed. Reading it names the refusal ("certificate
 * required") instead of the broken pipe it caused, whichever of the two the
 * timing happened to produce. Bytes that come before the alert are the
 * peer's, and are kept. */
static int net_tls_read_alert(NetSocket *s, char *reason, size_t rsize) {
    char chunk[NET_READ_CHUNK];
    for (int guard = 0; guard < 64; guard++) {
        ERR_clear_error();
        errno = 0;
        int n = SSL_read(s->ssl, chunk, (int)sizeof chunk);
        if (n > 0) {
            netbuf_append(&s->in, chunk, (size_t)n);
            continue;
        }
        if (SSL_get_error(s->ssl, n) != SSL_ERROR_SSL) {
            return 0;
        }
        const char *text = net_tls_error_text(NULL);
        if (!text) {
            return 0;
        }
        snprintf(reason, rsize, "%s", text);
        return 1;
    }
    return 0;
}

static long net_tls_fill(NetSocket *s, const char **code, char *message, size_t msize) {
    long total = 0;
    char chunk[NET_READ_CHUNK];
    s->tls_read_wants_write = 0;
    while (s->fd >= 0 && !s->peer_closed && s->in.len < s->in_ceiling) {
        size_t room = s->in_ceiling - s->in.len;
        ERR_clear_error();
        errno = 0;
        int n = SSL_read(s->ssl, chunk, (int)(room < sizeof chunk ? room : sizeof chunk));
        if (n > 0) {
            netbuf_append(&s->in, chunk, (size_t)n);
            total += n;
            continue;
        }
        int e = SSL_get_error(s->ssl, n);
        if (e == SSL_ERROR_WANT_READ) {
            break;
        }
        if (e == SSL_ERROR_WANT_WRITE) {
            s->tls_read_wants_write = 1;
            break;
        }
        if (e == SSL_ERROR_ZERO_RETURN) {
            s->peer_closed = 1;
            break;
        }
        if (e == SSL_ERROR_SYSCALL) {
            int err = net_tls_errno();
            if (err == EINTR) {
                continue;
            }
            if (err == 0) {
                s->peer_closed = 1;      /* end of stream without close_notify (older libssl) */
                break;
            }
            *code = net_errno_code(err);
            snprintf(message, msize, "net: the connection failed: %s", strerror(err));
            ERR_clear_error();
            return -1;
        }
        *code = "tls_failed";
        snprintf(message, msize, "net: the TLS connection failed: %s", net_tls_error_text("a protocol error"));
        ERR_clear_error();
        return -1;
    }
    if (total > 0) {
        s->last_activity = net_now();
    }
    return total;
}

static long net_tls_flush(NetSocket *s, const char **code, char *message, size_t msize) {
    long total = 0;
    s->tls_write_wants_read = 0;
    while (s->fd >= 0 && s->out.len > 0) {
        size_t len = s->out.len < (1u << 30) ? s->out.len : (1u << 30);
        ERR_clear_error();
        errno = 0;
        int n = SSL_write(s->ssl, s->out.data + s->out.off, (int)len);
        if (n > 0) {
            netbuf_consume(&s->out, (size_t)n);
            total += n;
            continue;
        }
        int e = SSL_get_error(s->ssl, n);
        if (e == SSL_ERROR_WANT_WRITE) {
            break;
        }
        if (e == SSL_ERROR_WANT_READ) {
            s->tls_write_wants_read = 1;
            break;
        }
        if (e == SSL_ERROR_SYSCALL) {
            int err = net_tls_errno();
            if (err == EINTR) {
                continue;
            }
            err = err ? err : EPIPE;
            char reason[256];
            if ((err == EPIPE || err == ECONNRESET) && net_tls_read_alert(s, reason, sizeof reason)) {
                *code = "tls_failed";
                snprintf(message, msize, "net: the peer ended the TLS connection: %s", reason);
            } else {
                *code = net_errno_code(err);
                snprintf(message, msize, "net: the connection failed while sending: %s", strerror(err));
            }
            ERR_clear_error();
            return -1;
        }
        *code = "tls_failed";
        snprintf(message, msize, "net: the TLS connection failed while sending: %s",
                 net_tls_error_text("a protocol error"));
        ERR_clear_error();
        return -1;
    }
    if (total > 0) {
        s->last_activity = net_now();
    }
    return total;
}

/* Decrypted bytes libssl holds that poll cannot see: a record larger than the
 * room left, kept when the buffer reached its ceiling. */
static int net_tls_has_pending(NetSocket *s) {
    return s->ssl && !s->tls_handshaking && !s->paused && !s->peer_closed && SSL_has_pending(s->ssl);
}
#endif

#if !HAVE_LIBSSL
/* A `tls:` option in a build without libssl. */
static int net_tls_unavailable(const char *verb) {
    char msg[200];
    snprintf(msg, sizeof msg,
             "%s: TLS is not available in this build (install the libssl development files and rebuild)", verb);
    net_refuse(msg);
    return 0;
}
#endif

/* A waited handshake where one is under way; always 1 in a build without TLS,
 * where none ever is. */
static int net_tls_wait_or_refuse(NetSocket *s, double deadline, const char *verb) {
#if HAVE_LIBSSL
    return net_tls_wait(s, deadline, verb);
#else
    (void)s;
    (void)deadline;
    (void)verb;
    return 1;
#endif
}

/* Begin a non-blocking connect to the next address. Returns 1 when one is in
 * progress or done, 0 when the addresses ran out (code set). */
static int net_try_next(NetSocket *s, const char **code, int *last_errno) {
    while (s->next_addr) {
        struct addrinfo *a = s->next_addr;
        s->next_addr = a->ai_next;
        int fd = socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
        if (fd < 0) {
            *last_errno = errno;
            continue;
        }
        net_set_nonblocking(fd);
        net_socket_options(fd, a->ai_family);
        if (!net_apply_keepalive(fd, &s->keepalive)) {
            *last_errno = errno;
            close(fd);
            continue;
        }
        int rc = connect(fd, a->ai_addr, a->ai_addrlen);
        if (rc == 0 || errno == EINPROGRESS) {
            s->fd = fd;
            s->family = a->ai_family;
            s->connecting = rc != 0;
            return 1;
        }
        *last_errno = errno;
        close(fd);
    }
    *code = net_errno_code(*last_errno ? *last_errno : ECONNREFUSED);
    return 0;
}

/* After POLLOUT on a connecting socket: finished, or on to the next address. */
static int net_connect_step(NetSocket *s, const char **code, int *last_errno) {
    int err = 0;
    socklen_t len = sizeof err;
    if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
        err = errno;
    }
    if (err == 0) {
        s->connecting = 0;
        return 1;
    }
    *last_errno = err;
    close(s->fd);
    s->fd = -1;
    return net_try_next(s, code, last_errno) ? 2 : 0;
}

static void *net_resolve_thread(void *arg) {
    NetResolve *r = (NetResolve *)arg;
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    r->rc = getaddrinfo(r->host, r->port, &hints, &r->result);
    char one = 1;
    ssize_t ignored = write(r->pipe_fd[1], &one, 1);
    (void)ignored;
    return NULL;
}

/* The descriptor is connected: report it, or begin the handshake first. A
 * WAITED handshake finishes here within what is left of the connect timeout;
 * 0 means it raised, and the socket and `opts` have been released. */
static int net_connected(NetSocket *s, Value opts) {
#if HAVE_LIBSSL
    if (s->ssl) {
        net_tls_attach(s, s->ssl, NET_TLS_CONNECT);
        if (s->connect_deadline <= 0) {
            s->connect_deadline = net_now() + s->connect_timeout_s;   /* the handshake is within it */
        }
        if (!s->watched) {
            if (!net_tls_wait(s, s->connect_deadline > 0 ? s->connect_deadline : net_now() + s->connect_timeout_s,
                              "net.connect")) {
                value_free(opts);
                net_socket_release(s);
                return 0;
            }
        }
        return 1;
    }
#endif
    (void)opts;
    if (s->watched) {
        s->connected_pending = 1;
    }
    return 1;
}

static Value net_do_connect(AstExpr *expr) {
    static const char *const known[] = { "host", "port", "path", "timeout", "idle", "buffer", "tls",
                                         "keepalive", NULL };
    if (expr->as.call.args.count != 1) {
        return net_refuse("net.connect expects one options record, such as { host: \"example.com\", port: 7000 }");
    }
    Value opts = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(opts);
        return value_null();
    }
    if (opts.kind != VALUE_RECORD) {
        value_free(opts);
        return net_refuse("net.connect expects an options record");
    }
    if (!net_check_options(opts, "net.connect", known)) {
        value_free(opts);
        return value_null();
    }
    const char *host = NULL, *path = NULL;
    double port = -1, timeout = 30, idle = 0, buffer = NET_DEFAULT_BUFFER;
    NetKeepalive keepalive;
    if (!net_opt_text(&opts, "host", "net.connect", &host) ||
        !net_opt_text(&opts, "path", "net.connect", &path) ||
        !net_opt_count(&opts, "port", "net.connect", 1, 65535, &port) ||
        !net_opt_seconds(&opts, "timeout", "net.connect", &timeout) ||
        !net_opt_seconds(&opts, "idle", "net.connect", &idle) ||
        !net_opt_count(&opts, "buffer", "net.connect", 1024, 1073741824, &buffer) ||
        !net_opt_keepalive(&opts, "net.connect", &keepalive)) {
        value_free(opts);
        return value_null();
    }
    if (path && (host || port >= 0)) {
        value_free(opts);
        return net_refuse("net.connect: give a path for a Unix socket, or a host and a port, not both");
    }
    if (path && keepalive.on) {
        value_free(opts);
        return net_refuse("net.connect: keepalive is TCP's, and a Unix socket has no network to probe");
    }
    if (!path && (!host || port < 0)) {
        value_free(opts);
        return net_refuse("net.connect needs a host and a port, or a path for a Unix socket");
    }
    Value *tls_opt = net_opt(&opts, "tls");
    if (tls_opt && tls_opt->kind == VALUE_BOOL && !tls_opt->as.boolean) {
        tls_opt = NULL;                  /* tls: false is plain */
    }
#if HAVE_LIBSSL
    SSL *ssl = NULL;
    if (tls_opt) {
        /* Built before connecting, so a bad option or an unreadable ca: file
         * is refused without a connection made and dropped. */
        ssl = net_tls_client_ssl(tls_opt, "net.connect", host);
        if (!ssl) {
            value_free(opts);
            return value_null();
        }
    }
#else
    if (tls_opt) {
        value_free(opts);
        net_tls_unavailable("net.connect");
        return value_null();
    }
#endif

    NetSocket *s = net_socket_new(NET_CONNECTION);
    s->host = host ? copy_string(host) : NULL;
    s->announced = 1;                    /* the program holds what connect returns */
#if HAVE_LIBSSL
    s->ssl = ssl;      /* its descriptor is set when the connection is made */
#endif
    s->in_ceiling = (size_t)buffer;
    s->idle_s = idle;
    s->keepalive = keepalive;
    s->connect_timeout_s = timeout;
    s->watched = net_events_watched();
    char msg[400];

    if (path) {
        struct sockaddr_un a;
        if (!net_unix_address(path, &a, "net.connect")) {
            value_free(opts);
            net_socket_release(s);
            return value_null();
        }
        s->path = copy_string(path);
        s->family = AF_UNIX;
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
            int e = errno;
            if (fd >= 0) {
                close(fd);
            }
            snprintf(msg, sizeof msg, "net.connect: nothing is listening at %s (%s)", path, strerror(e));
            value_free(opts);
            net_socket_release(s);
            return net_raise(net_errno_code(e), msg);
        }
        net_set_nonblocking(fd);
        net_socket_options(fd, AF_UNIX);
        s->fd = fd;
        s->open = 1;
        if (!net_connected(s, opts)) {
            return value_null();
        }
    } else {
        char port_text[16];
        snprintf(port_text, sizeof port_text, "%d", (int)port);
        s->port = (int)port;
        unsigned char probe[sizeof(struct in6_addr)];
        int numeric = inet_pton(AF_INET, host, probe) == 1 || inet_pton(AF_INET6, host, probe) == 1;
        s->connect_deadline = net_now() + timeout;
        s->open = 1;
        if (s->watched && !numeric) {
            /* OFF THE LOOP: a watched connect never stalls on a name. */
            NetResolve *r = calloc(1, sizeof *r);
            if (!r || pipe(r->pipe_fd) != 0) {
                abort();
            }
            r->host = copy_string(host);
            snprintf(r->port, sizeof r->port, "%s", port_text);
            if (pthread_create(&r->thread, NULL, net_resolve_thread, r) != 0) {
                close(r->pipe_fd[0]);
                close(r->pipe_fd[1]);
                free(r->host);
                free(r);
                value_free(opts);
                net_socket_release(s);
                return net_raise("failed", "net.connect: could not start the name lookup");
            }
            s->resolve = r;
            s->resolving = 1;
            s->connecting = 1;
        } else {
            struct addrinfo hints;
            memset(&hints, 0, sizeof hints);
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            if (numeric) {
                hints.ai_flags = AI_NUMERICHOST;
            }
            int rc = getaddrinfo(host, port_text, &hints, &s->addrs);
            if (rc != 0) {
                snprintf(msg, sizeof msg, "net.connect: %s does not resolve (%s)", host, gai_strerror(rc));
                value_free(opts);
                net_socket_release(s);
                return net_raise(net_gai_code(rc), msg);
            }
            s->next_addr = s->addrs;
            const char *code = NULL;
            int last_errno = 0;
            if (!net_try_next(s, &code, &last_errno)) {
                snprintf(msg, sizeof msg, "net.connect: could not connect to %s port %d (%s)", host,
                         (int)port, strerror(last_errno ? last_errno : ECONNREFUSED));
                value_free(opts);
                net_socket_release(s);
                return net_raise(code, msg);
            }
            if (!s->watched) {
                /* WAITED: finish here, within the one timeout, trying each
                 * address in turn. */
                while (s->connecting) {
                    double left = s->connect_deadline - net_now();
                    if (left <= 0) {
                        snprintf(msg, sizeof msg, "net.connect: %s port %d did not answer within %.0f seconds",
                                 host, (int)port, timeout);
                        value_free(opts);
                        net_socket_release(s);
                        return net_raise("timeout", msg);
                    }
                    struct pollfd p = { s->fd, POLLOUT, 0 };
                    int pr = poll(&p, 1, (int)(left * 1000) + 1);
                    if (pr < 0 && errno == EINTR) {
                        continue;
                    }
                    if (pr == 0) {
                        continue;
                    }
                    int step = net_connect_step(s, &code, &last_errno);
                    if (step == 0) {
                        snprintf(msg, sizeof msg, "net.connect: could not connect to %s port %d (%s)", host,
                                 (int)port, strerror(last_errno));
                        value_free(opts);
                        net_socket_release(s);
                        return net_raise(code, msg);
                    }
                }
                freeaddrinfo(s->addrs);
                s->addrs = NULL;
                s->next_addr = NULL;
                if (!net_connected(s, opts)) {
                    return value_null();
                }
            } else if (!s->connecting) {
                net_connected(s, opts);
            }
        }
    }
    value_free(opts);
    if (s->watched) {
        s->ref_count++;
        s->loop_ref = 1;
    }
    return value_net(s);
}

/* ---- listening ---------------------------------------------------------------------- */

static Value net_do_listen(AstExpr *expr) {
    static const char *const known[] = { "port", "address", "path", "mode", "backlog",
                                         "max_connections", "idle", "buffer", "tls", "keepalive", NULL };
    if (expr->as.call.args.count != 1) {
        return net_refuse("net.listen expects one options record, such as { port: 7000 }");
    }
    Value opts = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(opts);
        return value_null();
    }
    if (opts.kind != VALUE_RECORD) {
        value_free(opts);
        return net_refuse("net.listen expects an options record");
    }
    if (!net_check_options(opts, "net.listen", known)) {
        value_free(opts);
        return value_null();
    }
    const char *address = NULL, *path = NULL, *mode = NULL;
    double port = -1, backlog = 128, max_conn = 1024, idle = 300, buffer = NET_DEFAULT_BUFFER;
    NetKeepalive keepalive;
    if (!net_opt_text(&opts, "address", "net.listen", &address) ||
        !net_opt_text(&opts, "path", "net.listen", &path) ||
        !net_opt_text(&opts, "mode", "net.listen", &mode) ||
        !net_opt_count(&opts, "port", "net.listen", 0, 65535, &port) ||
        !net_opt_count(&opts, "backlog", "net.listen", 1, 65535, &backlog) ||
        !net_opt_count(&opts, "max_connections", "net.listen", 1, 1000000, &max_conn) ||
        !net_opt_seconds(&opts, "idle", "net.listen", &idle) ||
        !net_opt_count(&opts, "buffer", "net.listen", 1024, 1073741824, &buffer) ||
        !net_opt_keepalive(&opts, "net.listen", &keepalive)) {
        value_free(opts);
        return value_null();
    }
    if (path && keepalive.on) {
        value_free(opts);
        return net_refuse("net.listen: keepalive is TCP's, and a Unix socket has no network to probe");
    }
    if (path && (address || port >= 0)) {
        value_free(opts);
        return net_refuse("net.listen: give a path for a Unix socket, or a port, not both");
    }
    if (!path && port < 0) {
        value_free(opts);
        return net_refuse("net.listen needs a port (0 asks the system for one), or a path for a Unix socket");
    }
    if (mode && !path) {
        value_free(opts);
        return net_refuse("net.listen: mode is the permissions of a Unix socket's path, so it needs path");
    }
    char msg[400];
    Value *tls_opt = net_opt(&opts, "tls");
#if HAVE_LIBSSL
    NetTlsServer *tls_server = NULL;
    if (tls_opt) {
        /* Certificates are loaded now, so a listener that cannot serve TLS is
         * refused at listen rather than at its first client. */
        tls_server = net_tls_server_new(tls_opt, "net.listen");
        if (!tls_server) {
            value_free(opts);
            return value_null();
        }
    }
#else
    if (tls_opt) {
        value_free(opts);
        net_tls_unavailable("net.listen");
        return value_null();
    }
#endif
    NetSocket *s = net_socket_new(NET_LISTENER);
#if HAVE_LIBSSL
    s->tls_server = tls_server;
#endif
    s->max_connections = (size_t)max_conn;
    s->child_idle_s = idle;
    s->child_buffer = (size_t)buffer;
    s->keepalive = keepalive;
    s->watched = net_events_watched();
    int fd = -1;
    if (path) {
        struct sockaddr_un a;
        if (!net_unix_address(path, &a, "net.listen")) {
            value_free(opts);
            net_socket_release(s);
            return value_null();
        }
        struct stat st;
        if (lstat(path, &st) == 0) {
            snprintf(msg, sizeof msg,
                     "net.listen: %s already exists; remove it first if it is a socket left by a "
                     "server that has stopped", path);
            value_free(opts);
            net_socket_release(s);
            return net_raise("address_in_use", msg);
        }
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) != 0) {
            int e = errno;
            if (fd >= 0) {
                close(fd);
            }
            snprintf(msg, sizeof msg, "net.listen: could not make %s (%s)", path, strerror(e));
            value_free(opts);
            net_socket_release(s);
            return net_raise(net_errno_code(e), msg);
        }
        s->path = copy_string(path);
        s->unlink_on_close = 1;
        s->family = AF_UNIX;
        if (mode) {
            char *end = NULL;
            long m = strtol(mode, &end, 8);
            if (!end || *end || m < 0 || m > 0777 || chmod(path, (mode_t)m) != 0) {
                close(fd);
                value_free(opts);
                s->fd = -1;
                net_socket_release(s);
                return net_refuse("net.listen: mode must be octal permissions, such as \"0600\"");
            }
        }
    } else {
        const char *bind_to = address ? address : "127.0.0.1";
        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;
        char port_text[16];
        snprintf(port_text, sizeof port_text, "%d", (int)port);
        if (getaddrinfo(bind_to, port_text, &hints, &res) != 0 || !res) {
            snprintf(msg, sizeof msg,
                     "net.listen: address must be a numeric address such as \"127.0.0.1\", \"0.0.0.0\" "
                     "or \"::\", not \"%s\"", bind_to);
            value_free(opts);
            net_socket_release(s);
            return net_refuse(msg);
        }
        fd = socket(res->ai_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
        int one = 1;
        if (fd >= 0) {
#ifdef _WIN32
            /* NOT SO_REUSEADDR, which on Windows lets a second socket BIND A
             * PORT ANOTHER IS LISTENING ON. Windows already permits the
             * rebind-after-close that POSIX's flag is for; this flag is what
             * makes a held port refuse, as it does on Linux. */
            setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, &one, sizeof one);
#else
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
        }
        if (fd < 0 || bind(fd, res->ai_addr, res->ai_addrlen) != 0) {
            int e = errno;
            if (fd >= 0) {
                close(fd);
            }
            freeaddrinfo(res);
            snprintf(msg, sizeof msg, "net.listen: could not listen on %s port %d (%s)", bind_to, (int)port,
                     strerror(e));
            value_free(opts);
            net_socket_release(s);
            return net_raise(net_errno_code(e), msg);
        }
        s->family = res->ai_family;
        freeaddrinfo(res);
    }
    if (listen(fd, (int)backlog) != 0) {
        int e = errno;
        close(fd);
        snprintf(msg, sizeof msg, "net.listen: listen failed (%s)", strerror(e));
        value_free(opts);
        net_socket_release(s);
        return net_raise(net_errno_code(e), msg);
    }
    net_set_nonblocking(fd);
    s->fd = fd;
    s->open = 1;
    if (!path) {
        struct sockaddr_storage ss;
        socklen_t len = sizeof ss;
        if (getsockname(fd, (struct sockaddr *)&ss, &len) == 0) {
            s->port = ss.ss_family == AF_INET6 ? ntohs(((struct sockaddr_in6 *)&ss)->sin6_port)
                                               : ntohs(((struct sockaddr_in *)&ss)->sin_port);
        }
    }
    value_free(opts);
    if (s->watched) {
        s->ref_count++;
        s->loop_ref = 1;
    }
    return value_net(s);
}

/* Accept one pending connection. NULL when none is waiting. */
static NetSocket *net_accept_one(NetSocket *l, int *refused_for_limit) {
    *refused_for_limit = 0;
    int fd = accept(l->fd, NULL, NULL);
    if (fd < 0) {
        return NULL;
    }
#ifndef _WIN32
    fcntl(fd, F_SETFD, FD_CLOEXEC);   /* the accept wrapper does this on Windows */
#endif
    if (l->children >= l->max_connections) {
        close(fd);
        *refused_for_limit = 1;
        return NULL;
    }
    net_set_nonblocking(fd);
    net_socket_options(fd, l->family);
    /* a refused setting is not survivable here as it is at connect (there
     * is no caller to raise to), so the connection is dropped */
    if (!net_apply_keepalive(fd, &l->keepalive)) {
        close(fd);
        return NULL;
    }
    NetSocket *c = net_socket_new(NET_CONNECTION);
    c->fd = fd;
    c->open = 1;
    c->family = l->family;
    c->watched = l->watched;
    c->idle_s = l->child_idle_s;
    c->in_ceiling = l->child_buffer;
    c->listener = l;
    if (l->path) {
        c->path = copy_string(l->path);
    }
    l->children++;
#if HAVE_LIBSSL
    if (l->tls_server) {
        c->tls_server = l->tls_server;
        c->tls_server->refs++;
        SSL *ssl = net_tls_new_ssl(l->tls_server->dflt);
        SSL_set_accept_state(ssl);
        net_tls_attach(c, ssl, NET_TLS_ACCEPT);
    }
#endif
    return c;
}

/* ---- argument helpers ------------------------------------------------------------- */

static NetSocket *net_arg_socket(AstExpr *expr, const char *verb, int want_kind, Value *owner) {
    char msg[200];
    if (expr->as.call.args.count < 1) {
        snprintf(msg, sizeof msg, "%s expects a %s", verb, want_kind == NET_LISTENER ? "listener" : "connection");
        net_refuse(msg);
        return NULL;
    }
    Value v = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(v);
        return NULL;
    }
    if (v.kind == VALUE_NET && v.as.net->kind == NET_DATAGRAM && want_kind >= 0 && want_kind != NET_DATAGRAM) {
        snprintf(msg, sizeof msg, "%s: a datagram socket has no stream; each datagram comes whole from "
                 "receive or a datagram event, and goes with send_to", verb);
        value_free(v);
        net_refuse(msg);
        return NULL;
    }
    if (v.kind != VALUE_NET || (want_kind >= 0 && (int)v.as.net->kind != want_kind)) {
        snprintf(msg, sizeof msg, "%s expects a %s from net.%s", verb,
                 want_kind == NET_LISTENER ? "listener" : "connection",
                 want_kind == NET_LISTENER ? "listen" : "connect or a listener");
        value_free(v);
        net_refuse(msg);
        return NULL;
    }
    *owner = v;
    return v.as.net;
}

static int net_blocking_warning(AstExpr *expr, const char *verb) {
    if (webserver_event_loop_running && warn_site_first_time(expr->line, expr->column)) {
        warn_fmt(2105, "net",
                 "%s blocks the event loop, so no other connection, request or transfer makes "
                 "progress until it returns; use the take family on a watched connection instead, "
                 "or accept the stall deliberately", verb);
        return !error_action_pending();
    }
    return 1;
}

/* Where a cut ends in the buffer, or -1 when it is not complete. */
static long net_cut(NetSocket *s, int how, size_t n, const char *delim, size_t dlen, size_t *skip) {
    const char *p = s->in.data ? s->in.data + s->in.off : "";
    *skip = 0;
    if (how == 0) {                     /* exactly n */
        return s->in.len >= n ? (long)n : -1;
    }
    if (how == 3) {                     /* everything (up to n when n > 0) */
        return (long)(n && n < s->in.len ? n : s->in.len);
    }
    if (dlen == 0 || s->in.len < dlen) {
        return -1;
    }
    for (size_t i = 0; i + dlen <= s->in.len; i++) {
        if (memcmp(p + i, delim, dlen) == 0) {
            *skip = dlen;
            return (long)i;
        }
    }
    return -1;
}

/* Take a cut out of the buffer as a value. */
static Value net_take_cut(NetSocket *s, long end, size_t skip, int include, int strip_cr, int consume) {
    const char *p = s->in.data + s->in.off;
    size_t keep = (size_t)end + (include ? skip : 0);
    if (strip_cr && keep > 0 && p[keep - 1] == '\r') {
        keep--;
    }
    Value v = value_string_n(p, keep);
    if (consume) {
        netbuf_consume(&s->in, (size_t)end + skip);
        if (s->paused && s->in.len < s->in_ceiling) {
            s->paused = 0;
            s->full_notified = 0;
        }
    }
    return v;
}

/* Flush the send queue a waited socket owes, within a deadline. */
static int net_wait_flush(NetSocket *s, double deadline, const char *verb) {
    char msg[300];
    while (s->fd >= 0 && s->out.len > 0) {
        const char *code = NULL;
        if (net_flush_some(s, &code, msg, sizeof msg) < 0) {
            net_raise(code, msg);
            return 0;
        }
        if (s->out.len == 0) {
            break;
        }
        double left = deadline > 0 ? deadline - net_now() : 1e9;
        if (left <= 0) {
            snprintf(msg, sizeof msg, "%s: the peer did not accept the data in time", verb);
            net_raise("timeout", msg);
            return 0;
        }
        struct pollfd p = { s->fd, (short)(s->tls_write_wants_read ? POLLIN : POLLOUT), 0 };
        poll(&p, 1, left > 1e6 ? -1 : (int)(left * 1000) + 1);
    }
    return 1;
}

/* ---- the verbs -------------------------------------------------------------------- */

/* take / take_exactly / take_until / take_line / peek_exactly: never block. */
static Value net_do_take(AstExpr *expr, const char *verb) {
    Value owner;
    NetSocket *s = net_arg_socket(expr, verb, NET_CONNECTION, &owner);
    if (!s) {
        return value_null();
    }
    size_t argc = expr->as.call.args.count;
    int how = 3;     /* 0 exactly, 1 until, 2 line, 3 any */
    int consume = 1, include = 0;
    size_t n = 0;
    Value delim = value_null();
    char msg[200];
    if (strcmp(verb, "net.take_exactly") == 0 || strcmp(verb, "net.peek_exactly") == 0) {
        how = 0;
        consume = strcmp(verb, "net.take_exactly") == 0;
    } else if (strcmp(verb, "net.take_until") == 0) {
        how = 1;
    } else if (strcmp(verb, "net.take_line") == 0) {
        how = 2;
    }
    if ((how == 0 && argc != 2) || (how == 1 && (argc < 2 || argc > 3)) || (how == 2 && argc != 1) ||
        (how == 3 && argc > 2)) {
        value_free(owner);
        snprintf(msg, sizeof msg, "%s: wrong number of arguments", verb);
        return net_refuse(msg);
    }
    if (how == 0 || (how == 3 && argc == 2)) {
        Value nv = eval_expr(expr->as.call.args.items[1]);
        long long k = 0;
        if (error_action_pending() || !pack_integer(verb, 1, nv, &k) || k < 0) {
            value_free(nv);
            value_free(owner);
            if (!error_action_pending()) {
                snprintf(msg, sizeof msg, "%s: the count must be a whole number of bytes", verb);
                net_refuse(msg);
            }
            return value_null();
        }
        value_free(nv);
        n = (size_t)k;
        if (how == 0 && n > s->in_ceiling) {
            value_free(owner);
            snprintf(msg, sizeof msg, "%s: %zu bytes is more than this connection's buffer holds (%zu)", verb, n,
                     s->in_ceiling);
            return net_raise("too_long", msg);
        }
    }
    if (how == 1) {
        delim = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending() || delim.kind != VALUE_STRING || string_length(delim.as.string) == 0) {
            value_free(delim);
            value_free(owner);
            if (!error_action_pending()) {
                net_refuse("net.take_until: the delimiter must be text of at least one byte");
            }
            return value_null();
        }
        if (argc == 3) {
            Value o = eval_expr(expr->as.call.args.items[2]);
            static const char *const known[] = { "include", NULL };
            if (error_action_pending() || o.kind != VALUE_RECORD || !net_check_options(o, verb, known)) {
                if (!error_action_pending()) {
                    net_refuse("net.take_until: the options are a record, such as { include: true }");
                }
                value_free(o);
                value_free(delim);
                value_free(owner);
                return value_null();
            }
            Value *inc = net_opt(&o, "include");
            include = inc && inc->kind == VALUE_BOOL && inc->as.boolean;
            value_free(o);
        }
    }
    /* A watched connection's buffer is filled by the loop; a waited one is
     * filled here, without blocking, so take means "what has arrived". */
    if (!s->watched && s->fd >= 0) {
        const char *code = NULL;
        char m2[300];
        if (net_fill(s, &code, m2, sizeof m2) < 0) {
            value_free(delim);
            value_free(owner);
            return net_raise(code, m2);
        }
    }
    size_t skip = 0;
    long end = net_cut(s, how, n, how == 2 ? "\n" : (how == 1 ? delim.as.string : NULL),
                       how == 2 ? 1 : (how == 1 ? string_length(delim.as.string) : 0), &skip);
    Value result;
    if (end < 0) {
        result = value_unknown();
    } else {
        result = net_take_cut(s, end, skip, include, how == 2, consume);
    }
    value_free(delim);
    value_free(owner);
    return result;
}

/* read / read_exactly / read_until / read_line: block, with a timeout. */
static Value net_do_read(AstExpr *expr, const char *verb) {
    Value owner;
    NetSocket *s = net_arg_socket(expr, verb, NET_CONNECTION, &owner);
    if (!s) {
        return value_null();
    }
    char msg[300];
    if (s->watched) {
        value_free(owner);
        snprintf(msg, sizeof msg,
                 "%s: this connection is watched, so the event loop reads it; use the take family "
                 "(take, take_exactly, take_until, take_line) in the watcher", verb);
        return net_refuse(msg);
    }
    int how = 3;
    if (strcmp(verb, "net.read_exactly") == 0) how = 0;
    else if (strcmp(verb, "net.read_until") == 0) how = 1;
    else if (strcmp(verb, "net.read_line") == 0) how = 2;
    size_t argc = expr->as.call.args.count;
    size_t n = 0;
    double timeout = -1;
    Value delim = value_null();
    size_t next = 1;
    if (how == 0 || how == 1) {
        if (argc < 2) {
            value_free(owner);
            snprintf(msg, sizeof msg, "%s expects a connection and %s", verb, how == 0 ? "a byte count" : "a delimiter");
            return net_refuse(msg);
        }
        Value a = eval_expr(expr->as.call.args.items[1]);
        if (error_action_pending()) {
            value_free(a);
            value_free(owner);
            return value_null();
        }
        if (how == 0) {
            long long k = 0;
            if (!pack_integer(verb, 1, a, &k) || k < 0) {
                value_free(a);
                value_free(owner);
                if (!error_action_pending()) {
                    snprintf(msg, sizeof msg, "%s: the count must be a whole number of bytes", verb);
                    net_refuse(msg);
                }
                return value_null();
            }
            n = (size_t)k;
            value_free(a);
            if (n > s->in_ceiling) {
                value_free(owner);
                snprintf(msg, sizeof msg, "%s: %zu bytes is more than this connection's buffer holds (%zu)", verb,
                         n, s->in_ceiling);
                return net_raise("too_long", msg);
            }
        } else {
            if (a.kind != VALUE_STRING || string_length(a.as.string) == 0) {
                value_free(a);
                value_free(owner);
                return net_refuse("net.read_until: the delimiter must be text of at least one byte");
            }
            delim = a;
        }
        next = 2;
    }
    for (size_t i = next; i < argc; i++) {
        Value a = eval_expr(expr->as.call.args.items[i]);
        if (error_action_pending()) {
            value_free(a);
            value_free(delim);
            value_free(owner);
            return value_null();
        }
        if (how == 3 && a.kind == VALUE_NUMBER && i == 1) {
            long long k = 0;
            if (!pack_integer(verb, 1, a, &k) || k < 0) {
                value_free(a);
                value_free(delim);
                value_free(owner);
                return value_null();
            }
            n = (size_t)k;
        } else {
            timeout = net_seconds(a, verb);
            if (timeout < 0) {
                value_free(a);
                value_free(delim);
                value_free(owner);
                return value_null();
            }
        }
        value_free(a);
    }
    if (!net_blocking_warning(expr, verb)) {
        value_free(delim);
        value_free(owner);
        return value_null();
    }
    double deadline = timeout >= 0 ? net_now() + timeout : -1;
    const char *dp = how == 2 ? "\n" : (how == 1 ? delim.as.string : NULL);
    size_t dl = how == 2 ? 1 : (how == 1 ? string_length(delim.as.string) : 0);
    for (;;) {
        /* what is owed goes out first, so a request-then-reply exchange
         * cannot deadlock on its own unsent request */
        const char *code = NULL;
        if (s->out.len > 0 && net_flush_some(s, &code, msg, sizeof msg) < 0) {
            value_free(delim);
            value_free(owner);
            return net_raise(code, msg);
        }
        if (s->fd >= 0 && net_fill(s, &code, msg, sizeof msg) < 0) {
            value_free(delim);
            value_free(owner);
            return net_raise(code, msg);
        }
        size_t skip = 0;
        long end = how == 3 ? (s->in.len > 0 ? (long)(n && n < s->in.len ? n : s->in.len) : -1)
                            : net_cut(s, how, n, dp, dl, &skip);
        if (end >= 0) {
            Value r = net_take_cut(s, end, skip, 0, how == 2, 1);
            value_free(delim);
            value_free(owner);
            return r;
        }
        if (s->peer_closed || s->fd < 0) {
            if (how == 3) {
                value_free(delim);
                value_free(owner);
                return value_string("");     /* end of stream: nothing more will come */
            }
            Value partial = value_string_n(s->in.data ? s->in.data + s->in.off : "", s->in.len);
            netbuf_consume(&s->in, s->in.len);
            snprintf(msg, sizeof msg, "%s: the peer closed the connection before the %s arrived", verb,
                     how == 0 ? "bytes asked for" : how == 1 ? "delimiter" : "end of the line");
            value_free(delim);
            value_free(owner);
            net_raise_details("closed", msg, partial, 1);
            return value_null();
        }
        if (how != 3 && s->in.len >= s->in_ceiling) {
            snprintf(msg, sizeof msg, "%s: the buffer filled (%zu bytes) before the %s arrived", verb,
                     s->in_ceiling, how == 1 ? "delimiter" : "end of the line");
            value_free(delim);
            value_free(owner);
            return net_raise("too_long", msg);
        }
        double left = deadline >= 0 ? deadline - net_now() : -1;
        if (deadline >= 0 && left <= 0) {
            snprintf(msg, sizeof msg, "%s: nothing complete arrived within the timeout; what did arrive is "
                     "still buffered", verb);
            value_free(delim);
            value_free(owner);
            return net_raise("timeout", msg);
        }
        struct pollfd p = { s->fd, (short)(POLLIN | (s->out.len > 0 || s->tls_read_wants_write ? POLLOUT : 0)), 0 };
        int pr = poll(&p, 1, left < 0 ? -1 : (int)(left * 1000) + 1);
        if (pr < 0 && errno != EINTR) {
            value_free(delim);
            value_free(owner);
            return net_raise("failed", "net: poll failed while reading");
        }
    }
}

static Value net_do_write(AstExpr *expr) {
    Value owner;
    NetSocket *s = net_arg_socket(expr, "net.write", NET_CONNECTION, &owner);
    if (!s) {
        return value_null();
    }
    if (expr->as.call.args.count != 2) {
        value_free(owner);
        return net_refuse("net.write expects a connection and the bytes to send");
    }
    Value data = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(data);
        value_free(owner);
        return value_null();
    }
    if (data.kind != VALUE_STRING) {
        value_free(data);
        value_free(owner);
        return net_refuse("net.write: the data must be text (bytes); convert a number with string() or pack()");
    }
    char msg[300];
    if (!s->open || s->closing || s->write_shut) {
        value_free(data);
        value_free(owner);
        return net_raise("closed", "net.write: this connection is closed for writing");
    }
    size_t n = string_length(data.as.string);
    if (s->out.len + n > s->out_ceiling) {
        value_free(data);
        value_free(owner);
        snprintf(msg, sizeof msg,
                 "net.write: %zu bytes would put the send queue past its ceiling (%zu queued); wait for "
                 "the writable event", n, s->out.len);
        return net_raise("backpressure", msg);
    }
    netbuf_append(&s->out, data.as.string, n);
    value_free(data);
    if (!s->connecting) {
        const char *code = NULL;
        if (net_flush_some(s, &code, msg, sizeof msg) < 0) {
            if (s->watched) {
                net_fail(s, code, msg);
            } else {
                value_free(owner);
                return net_raise(code, msg);
            }
        }
    }
    /* AT the mark arms, BELOW it fires: the two meet exactly. Arming only
     * above it stalled a program that wrote "while queued() < 64 KiB" -- it
     * stops at exactly 65536, which TLS's 16 KiB records make likely, and no
     * writable ever came (run_net.sh's drip, under the gate's load). */
    if (s->out.len >= NET_LOW_WATER) {
        s->want_writable = 1;
    }
    value_free(owner);
    return value_number_exact((long long)n);
}

static Value net_do_simple(AstExpr *expr, const char *verb) {
    Value owner;
    NetSocket *s = net_arg_socket(expr, verb, -1, &owner);
    if (!s) {
        return value_null();
    }
    char msg[300];
    Value result = value_bool(1);
    if (s->kind == NET_DATAGRAM && strcmp(verb, "net.close") != 0 && strcmp(verb, "net.abort") != 0) {
        value_free(owner);
        snprintf(msg, sizeof msg, "%s: a datagram socket has no stream; each datagram comes whole from "
                 "receive or a datagram event, and send_to hands one to the system before it returns", verb);
        return net_refuse(msg);
    }
    if (s->kind == NET_DATAGRAM) {
        /* nothing is queued, so a close is at once, watched or waited */
        if (s->open) {
            if (s->watched) {
                net_mark_closed(s, strcmp(verb, "net.abort") == 0 ? "aborted" : "local");
            } else {
                net_close_fd(s);
            }
        }
        value_free(owner);
        return result;
    }
    if (strcmp(verb, "net.buffered") == 0) {
        result = value_number_exact((long long)s->in.len);
    } else if (strcmp(verb, "net.queued") == 0) {
        result = value_number_exact((long long)s->out.len);
    } else if (strcmp(verb, "net.flush") == 0) {
        double timeout = -1;
        if (expr->as.call.args.count == 2) {
            Value t = eval_expr(expr->as.call.args.items[1]);
            timeout = error_action_pending() ? -2 : net_seconds(t, "net.flush");
            value_free(t);
            if (timeout < 0 && timeout != -1) {
                value_free(owner);
                return value_null();
            }
        }
        if (s->watched) {
            value_free(owner);
            return net_refuse("net.flush: a watched connection is flushed by the loop; wait for the writable event");
        }
        if (!net_blocking_warning(expr, verb) ||
            !net_wait_flush(s, timeout >= 0 ? net_now() + timeout : -1, verb)) {
            value_free(owner);
            return value_null();
        }
    } else if (strcmp(verb, "net.shutdown_write") == 0) {
        if (s->fd >= 0 && !s->write_shut) {
            if (!s->watched && !net_wait_flush(s, net_now() + 30, verb)) {
                value_free(owner);
                return value_null();
            }
            if (s->out.len == 0) {
                net_shut_write(s);
            }
            s->write_shut = 1;
        }
    } else if (strcmp(verb, "net.close") == 0) {
        if (s->kind == NET_LISTENER) {
            if (s->open) {
                net_mark_closed(s, "local");
            }
        } else if (s->open) {
            if (s->watched) {
                s->closing = 1;        /* the loop closes it when the queue drains */
                if (s->out.len == 0) {
                    net_mark_closed(s, "local");
                }
            } else {
                net_wait_flush(s, net_now() + 30, verb);
                if (error_action_pending()) {
                    net_close_fd(s);
                    value_free(owner);
                    return value_null();
                }
                net_close_fd(s);
            }
        }
    } else if (strcmp(verb, "net.abort") == 0) {
        if (s->open) {
            if (s->fd >= 0 && s->kind == NET_CONNECTION) {
                struct linger lg = { 1, 0 };
                setsockopt(s->fd, SOL_SOCKET, SO_LINGER, &lg, sizeof lg);
            }
            s->tls_no_notify = 1;
            netbuf_consume(&s->out, s->out.len);
            if (s->watched) {
                net_mark_closed(s, "aborted");
            } else {
                net_close_fd(s);
            }
        }
    } else if (strcmp(verb, "net.accept") == 0) {
        if (s->kind != NET_LISTENER) {
            value_free(owner);
            return net_refuse("net.accept expects a listener from net.listen");
        }
        if (s->watched) {
            value_free(owner);
            return net_refuse("net.accept: this listener is watched, so connections arrive as accepted events");
        }
        double timeout = -1;
        if (expr->as.call.args.count == 2) {
            Value t = eval_expr(expr->as.call.args.items[1]);
            timeout = error_action_pending() ? -2 : net_seconds(t, "net.accept");
            value_free(t);
            if (timeout < 0 && timeout != -1) {
                value_free(owner);
                return value_null();
            }
        }
        if (!net_blocking_warning(expr, verb)) {
            value_free(owner);
            return value_null();
        }
        double deadline = timeout >= 0 ? net_now() + timeout : -1;
        for (;;) {
            if (!s->open) {
                value_free(owner);
                return net_raise("closed", "net.accept: the listener is closed");
            }
            int limited = 0;
            NetSocket *c = net_accept_one(s, &limited);
            if (c) {
                if (c->tls_handshaking) {
                    double until = deadline >= 0 ? deadline : net_now() + (c->idle_s > 0 ? c->idle_s : 30);
                    if (!net_tls_wait_or_refuse(c, until, verb)) {
                        net_socket_release(c);
                        value_free(owner);
                        return value_null();
                    }
                }
                c->announced = 1;
                value_free(owner);
                return value_net(c);
            }
            double left = deadline >= 0 ? deadline - net_now() : -1;
            if (deadline >= 0 && left <= 0) {
                value_free(owner);
                return net_raise("timeout", "net.accept: no connection arrived within the timeout");
            }
            struct pollfd p = { s->fd, POLLIN, 0 };
            poll(&p, 1, left < 0 ? -1 : (int)(left * 1000) + 1);
        }
    } else {
        snprintf(msg, sizeof msg, "%s: not a verb", verb);
        result = net_refuse(msg);
    }
    value_free(owner);
    return result;
}

/* start_tls(connection, tls [, timeout]): the handshake on a connection that
 * is already open, as PostgreSQL's SSLRequest, SMTP's STARTTLS and LDAP's and
 * IMAP's upgrades need. A connection that came from a listener takes the
 * server's side (cert and key); one from net.connect the client's. */
static Value net_do_start_tls(AstExpr *expr) {
    const char *verb = "net.start_tls";
    Value owner;
    NetSocket *s = net_arg_socket(expr, verb, NET_CONNECTION, &owner);
    if (!s) {
        return value_null();
    }
    size_t argc = expr->as.call.args.count;
    if (argc < 2 || argc > 3) {
        value_free(owner);
        return net_refuse("net.start_tls expects a connection, its tls options (true, or a record) and an "
                          "optional timeout");
    }
    Value opt = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(opt);
        value_free(owner);
        return value_null();
    }
    double timeout = 30;
    if (argc == 3) {
        Value t = eval_expr(expr->as.call.args.items[2]);
        timeout = error_action_pending() ? -1 : net_seconds(t, verb);
        value_free(t);
        if (timeout < 0) {
            value_free(opt);
            value_free(owner);
            return value_null();
        }
    }
    Value result = value_null();
#if !HAVE_LIBSSL
    (void)s;
    net_tls_unavailable(verb);
#else
    char msg[400];
    if (!s->open || s->closing || s->write_shut || s->peer_closed) {
        net_raise("closed", "net.start_tls: the connection is closed");
    } else if (s->ssl) {
        net_refuse("net.start_tls: this connection is already TLS");
    } else if (s->connecting) {
        net_refuse("net.start_tls: the connection has not finished connecting; wait for the connected event");
    } else if (s->in.len > 0) {
        /* THE STARTTLS INJECTION (CVE-2011-0411 and its family): bytes that
         * arrived in plaintext after the upgrade was asked for would be read
         * as if they came over TLS. They are the peer's, sent too early, and
         * the upgrade is refused rather than letting them through. */
        snprintf(msg, sizeof msg,
                 "net.start_tls: %zu bytes arrived before the handshake and are still buffered; they came "
                 "in plaintext, so they cannot be read as part of the TLS stream (take them first if the "
                 "protocol allows them)", s->in.len);
        net_raise("unexpected_data", msg);
    } else {
        const char *code = NULL;
        if (s->out.len > 0) {
            /* what the program wrote before the upgrade goes in plaintext */
            if (!s->watched) {
                net_wait_flush(s, net_now() + timeout, verb);
            } else if (net_flush_some(s, &code, msg, sizeof msg) < 0) {
                net_raise(code, msg);
            } else if (s->out.len > 0) {
                net_refuse("net.start_tls: what was written before the upgrade has not all been sent; call "
                           "it once queued() is 0 (the writable event says when)");
            }
        }
        if (!error_action_pending()) {
            SSL *ssl = NULL;
            if (s->listener) {
                NetTlsServer *t = net_tls_server_new(&opt, verb);
                if (t) {
                    if (s->tls_server) {
                        net_tls_server_release(s->tls_server);
                    }
                    s->tls_server = t;
                    ssl = net_tls_new_ssl(t->dflt);
                    SSL_set_accept_state(ssl);
                }
            } else {
                ssl = net_tls_client_ssl(&opt, verb, s->host);
            }
            if (ssl) {
                net_tls_attach(s, ssl, NET_TLS_UPGRADE);
                s->connect_deadline = net_now() + timeout;
                if (s->watched) {
                    result = value_bool(1);       /* the tls event says it finished */
                } else if (net_blocking_warning(expr, verb) &&
                           net_tls_wait(s, s->connect_deadline, verb)) {
                    s->connect_deadline = 0;
                    result = value_bool(1);
                } else {
                    net_close_fd(s);              /* a stream a handshake failed on is unusable */
                }
            }
        }
    }
#endif
    value_free(opt);
    value_free(owner);
    return result;
}

static Value net_do_resolve(AstExpr *expr) {
    if (expr->as.call.args.count != 1) {
        return net_refuse("net.resolve expects a host name");
    }
    Value h = eval_expr(expr->as.call.args.items[0]);
    if (error_action_pending()) {
        value_free(h);
        return value_null();
    }
    if (h.kind != VALUE_STRING) {
        value_free(h);
        return net_refuse("net.resolve expects a host name as text");
    }
    if (!net_blocking_warning(expr, "net.resolve")) {
        value_free(h);
        return value_null();
    }
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(h.as.string, NULL, &hints, &res);
    if (rc != 0) {
        char msg[300];
        snprintf(msg, sizeof msg, "net.resolve: %s does not resolve (%s)", h.as.string, gai_strerror(rc));
        value_free(h);
        return net_raise(net_gai_code(rc), msg);
    }
    value_free(h);
    Value out = value_array(NULL, 0);
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        char text[INET6_ADDRSTRLEN] = "";
        if (a->ai_family == AF_INET) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)a->ai_addr)->sin_addr, text, sizeof text);
        } else if (a->ai_family == AF_INET6) {
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)a->ai_addr)->sin6_addr, text, sizeof text);
        } else {
            continue;
        }
        Value r = value_record(NULL, 0);
        record_set(&r, "address", value_string(text));
        record_set(&r, "family", value_string(a->ai_family == AF_INET6 ? "ipv6" : "ipv4"));
        int seen = 0;
        for (size_t i = 0; i < out.as.array.store->count; i++) {
            if (value_storage_equal(&out.as.array.store->items[i], &r)) {
                seen = 1;
                break;
            }
        }
        if (seen) {
            value_free(r);
            continue;
        }
        Value ignored = append_to_array_ref(&out, r, 0);
        value_free(ignored);
    }
    freeaddrinfo(res);
    return out;
}

/* ---- datagrams (N3) --------------------------------------------------------------- *
 *
 * docs/networking_design.md §4. A datagram socket is UDP, and A DATAGRAM IS
 * NEVER SPLIT OR JOINED: one receive, or one `datagram` event, is one datagram,
 * whatever arrived around it. One longer than the socket's `max_datagram` is
 * delivered with the bytes that fit and `truncated: true` (and, where the
 * system tells it, `size`, the length that was sent), never silently cut.
 *
 * There is NO SEND QUEUE. A datagram is either taken by the kernel or not, so
 * send_to hands it over before returning; a kernel that has no room waits for
 * room within the timeout, which on a datagram socket is the kernel's own
 * buffer draining to the network, not a peer reading. */

/* Where `to` ({ address: or host:, port: }) names, in the socket's family. 0
 * having raised. An IPv4 address is mapped for a socket bound to IPv6. */
static int net_dgram_target(NetSocket *s, Value to, AstExpr *expr, struct sockaddr_storage *out,
                            socklen_t *out_len) {
    const char *verb = "net.send_to";
    static const char *const known[] = { "address", "host", "port", NULL };
    char msg[400];
    if (to.kind != VALUE_RECORD) {
        net_refuse("net.send_to: say where to send it, as { address: \"127.0.0.1\", port: 514 } "
                   "(a received datagram's `from` is one)");
        return 0;
    }
    if (!net_check_options(to, verb, known)) {
        return 0;
    }
    const char *address = NULL, *host = NULL;
    double port = -1;
    if (!net_opt_text(&to, "address", verb, &address) || !net_opt_text(&to, "host", verb, &host) ||
        !net_opt_count(&to, "port", verb, 1, 65535, &port)) {
        return 0;
    }
    if (address && host) {
        net_refuse("net.send_to: give an address or a host, not both");
        return 0;
    }
    if (!address && !host) {
        net_refuse("net.send_to: say where to send it: an address (or host) and a port");
        return 0;
    }
    if (port < 0) {
        net_refuse("net.send_to: say which port to send it to");
        return 0;
    }
    const char *name = address ? address : host;
    char port_text[16];
    snprintf(port_text, sizeof port_text, "%d", (int)port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;
    int rc = getaddrinfo(name, port_text, &hints, &res);
    if (rc != 0 && address) {
        snprintf(msg, sizeof msg,
                 "net.send_to: address must be a numeric address such as \"127.0.0.1\" or \"::1\", not \"%s\" "
                 "(a name goes in host:)", name);
        net_refuse(msg);
        return 0;
    }
    if (rc != 0) {
        /* a name: resolving it blocks, as net.resolve does */
        if (!net_blocking_warning(expr, "net.send_to to a host name")) {
            return 0;
        }
        hints.ai_flags = 0;
        hints.ai_family = s->family == AF_INET ? AF_INET : AF_UNSPEC;
        rc = getaddrinfo(name, port_text, &hints, &res);
        if (rc != 0 || !res) {
            snprintf(msg, sizeof msg, "net.send_to: %s does not resolve (%s)", name, gai_strerror(rc));
            net_raise(net_gai_code(rc), msg);
            return 0;
        }
    }
    /* the first address the socket can reach */
    struct addrinfo *pick = NULL;
    for (struct addrinfo *a = res; a && !pick; a = a->ai_next) {
        if (a->ai_family == s->family || (s->family == AF_INET6 && a->ai_family == AF_INET)) {
            pick = a;
        }
    }
    if (!pick) {
        snprintf(msg, sizeof msg,
                 "net.send_to: %s is an IPv%s address and this socket is bound to IPv%s; make the socket "
                 "with address: \"%s\" to send to it", name, s->family == AF_INET ? "6" : "4",
                 s->family == AF_INET ? "4" : "6", s->family == AF_INET ? "::" : "0.0.0.0");
        freeaddrinfo(res);
        net_refuse(msg);
        return 0;
    }
    memset(out, 0, sizeof *out);
    /* Linux takes a plain IPv4 address on an IPv6 socket too (measured: this
     * block removed, run_net.sh stays green), but the portable contract is an
     * address of the socket's own family, so it is mapped here. */
    if (s->family == AF_INET6 && pick->ai_family == AF_INET) {
        struct sockaddr_in6 *m = (struct sockaddr_in6 *)out;
        const struct sockaddr_in *v4 = (const struct sockaddr_in *)pick->ai_addr;
        m->sin6_family = AF_INET6;
        m->sin6_port = v4->sin_port;
        m->sin6_addr.s6_addr[10] = 0xff;
        m->sin6_addr.s6_addr[11] = 0xff;
        memcpy(&m->sin6_addr.s6_addr[12], &v4->sin_addr, 4);
        *out_len = sizeof *m;
    } else {
        memcpy(out, pick->ai_addr, pick->ai_addrlen);
        *out_len = pick->ai_addrlen;
    }
    freeaddrinfo(res);
    return 1;
}

/* Read one datagram. 1 with *out a { data, from, truncated, size } record, 0
 * when none is waiting, -1 on a failure (code and message set). */
static int net_dgram_recv_one(NetSocket *s, Value *out, const char **code, char *message, size_t msize) {
    struct sockaddr_storage from;
    struct iovec iov = { s->dgram_buf, NET_DGRAM_RECV_SIZE(s->max_datagram) };
    struct msghdr mh;
    memset(&mh, 0, sizeof mh);
    memset(&from, 0, sizeof from);
    mh.msg_name = &from;
    mh.msg_namelen = sizeof from;
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    int flags = 0;
#ifdef __linux__
    flags |= MSG_TRUNC;      /* Linux answers the length that was SENT, so `size` is known */
#endif
    ssize_t got = recvmsg(s->fd, &mh, flags);
    if (got < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;
        }
        int e = errno;
        *code = net_errno_code(e);
        snprintf(message, msize, "net: receiving a datagram failed (%s)", strerror(e));
        return -1;
    }
    int truncated = (mh.msg_flags & MSG_TRUNC) || (size_t)got > s->max_datagram;
    size_t kept = (size_t)got > s->max_datagram ? s->max_datagram : (size_t)got;
    Value r = value_record(NULL, 0);
    record_set(&r, "data", value_string_n(s->dgram_buf, kept));
    record_set(&r, "from", net_address_record((struct sockaddr *)&from));
    record_set(&r, "truncated", value_bool(truncated));
#if defined(__linux__) || defined(_WIN32)
    record_set(&r, "size", value_number_exact((long long)got));
#else
    record_set(&r, "size", truncated ? value_unknown() : value_number_exact((long long)got));
#endif
    s->last_activity = net_now();
    *out = r;
    return 1;
}

static Value net_do_datagram(AstExpr *expr) {
    static const char *const known[] = { "port", "address", "max_datagram", NULL };
    const char *verb = "net.datagram";
    Value opts = value_record(NULL, 0);
    if (expr->as.call.args.count > 1) {
        value_free(opts);
        return net_refuse("net.datagram expects one options record, such as { port: 5140 }");
    }
    if (expr->as.call.args.count == 1) {
        value_free(opts);
        opts = eval_expr(expr->as.call.args.items[0]);
        if (error_action_pending()) {
            value_free(opts);
            return value_null();
        }
        if (opts.kind != VALUE_RECORD) {
            value_free(opts);
            return net_refuse("net.datagram expects an options record, such as { port: 5140 }");
        }
    }
    if (!net_check_options(opts, verb, known)) {
        value_free(opts);
        return value_null();
    }
    const char *address = NULL;
    double port = -1, max_datagram = NET_DEFAULT_DATAGRAM;
    if (!net_opt_text(&opts, "address", verb, &address) ||
        !net_opt_count(&opts, "port", verb, 0, 65535, &port) ||
        !net_opt_count(&opts, "max_datagram", verb, 1, NET_DEFAULT_DATAGRAM, &max_datagram)) {
        value_free(opts);
        return value_null();
    }
    /* A socket that names a port is a server's, and binds to loopback unless
     * told otherwise (P6). One that names neither is a client's: it gets what
     * an unbound UDP socket gets at its first send, any address and a port the
     * system picks, because a client bound to loopback could reach nothing
     * else. */
    const char *bind_to = address ? address : (port >= 0 ? "127.0.0.1" : "0.0.0.0");
    if (port < 0) {
        port = 0;
    }
    char msg[400];
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;
    char port_text[16];
    snprintf(port_text, sizeof port_text, "%d", (int)port);
    if (getaddrinfo(bind_to, port_text, &hints, &res) != 0 || !res) {
        snprintf(msg, sizeof msg,
                 "net.datagram: address must be a numeric address such as \"127.0.0.1\", \"0.0.0.0\" "
                 "or \"::\", not \"%s\"", bind_to);
        value_free(opts);
        return net_refuse(msg);
    }
    int fd = socket(res->ai_family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || bind(fd, res->ai_addr, res->ai_addrlen) != 0) {
        int e = errno;
        if (fd >= 0) {
            close(fd);
        }
        freeaddrinfo(res);
        snprintf(msg, sizeof msg, "net.datagram: could not bind %s port %d (%s)", bind_to, (int)port, strerror(e));
        value_free(opts);
        return net_raise(net_errno_code(e), msg);
    }
    NetSocket *s = net_socket_new(NET_DATAGRAM);
    s->family = res->ai_family;
    freeaddrinfo(res);
    net_set_nonblocking(fd);
    s->fd = fd;
    s->open = 1;
    s->max_datagram = (size_t)max_datagram;
    s->dgram_buf = malloc(NET_DGRAM_RECV_SIZE(s->max_datagram));
    if (!s->dgram_buf) {
        abort();
    }
    s->dgram_pending = value_array(NULL, 0);
    struct sockaddr_storage ss;
    socklen_t len = sizeof ss;
    if (getsockname(fd, (struct sockaddr *)&ss, &len) == 0) {
        s->port = ss.ss_family == AF_INET6 ? ntohs(((struct sockaddr_in6 *)&ss)->sin6_port)
                                           : ntohs(((struct sockaddr_in *)&ss)->sin_port);
    }
    value_free(opts);
    s->watched = net_events_watched();
    if (s->watched) {
        s->ref_count++;
        s->loop_ref = 1;
    }
    return value_net(s);
}

static NetSocket *net_arg_datagram(AstExpr *expr, const char *verb, Value *owner) {
    char msg[200];
    NetSocket *s = net_arg_socket(expr, verb, -1, owner);
    if (s && s->kind != NET_DATAGRAM) {
        snprintf(msg, sizeof msg, "%s expects a datagram socket from net.datagram; a %s has a stream, "
                 "so it uses write and the take and read families", verb,
                 s->kind == NET_LISTENER ? "listener's connection" : "connection");
        value_free(*owner);
        net_refuse(msg);
        return NULL;
    }
    return s;
}

/* send_to(socket, bytes, to [, timeout]) -> the bytes sent */
static Value net_do_send_to(AstExpr *expr) {
    Value owner;
    NetSocket *s = net_arg_datagram(expr, "net.send_to", &owner);
    if (!s) {
        return value_null();
    }
    size_t argc = expr->as.call.args.count;
    if (argc < 3 || argc > 4) {
        value_free(owner);
        return net_refuse("net.send_to expects a datagram socket, the bytes, and where to send them "
                          "({ address:, port: }), with an optional timeout");
    }
    Value data = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(data);
        value_free(owner);
        return value_null();
    }
    if (data.kind != VALUE_STRING) {
        value_free(data);
        value_free(owner);
        return net_refuse("net.send_to: the data must be text (bytes); convert a number with string() or pack()");
    }
    Value to = eval_expr(expr->as.call.args.items[2]);
    if (error_action_pending()) {
        value_free(to);
        value_free(data);
        value_free(owner);
        return value_null();
    }
    double timeout = 10;
    if (argc == 4) {
        Value t = eval_expr(expr->as.call.args.items[3]);
        timeout = error_action_pending() ? -2 : net_seconds(t, "net.send_to");
        value_free(t);
        if (timeout < 0) {
            value_free(to);
            value_free(data);
            value_free(owner);
            return value_null();
        }
    }
    if (!s->open) {
        value_free(to);
        value_free(data);
        value_free(owner);
        return net_raise("closed", "net.send_to: this datagram socket is closed");
    }
    struct sockaddr_storage dest;
    socklen_t dest_len = 0;
    if (!net_dgram_target(s, to, expr, &dest, &dest_len)) {
        value_free(to);
        value_free(data);
        value_free(owner);
        return value_null();
    }
    value_free(to);
    size_t n = string_length(data.as.string);
    char msg[300];
    double deadline = net_now() + timeout;
    for (;;) {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        ssize_t sent = sendto(s->fd, data.as.string, n, flags, (struct sockaddr *)&dest, dest_len);
        if (sent >= 0) {
            break;
        }
        int e = errno;
        if (e == EINTR) {
            continue;
        }
        if (e == EAGAIN || e == EWOULDBLOCK || e == ENOBUFS) {
            double left = deadline - net_now();
            if (left <= 0) {
                value_free(data);
                value_free(owner);
                return net_raise("timeout", "net.send_to: the system had no room for the datagram within the timeout");
            }
            struct pollfd p = { s->fd, POLLOUT, 0 };
            poll(&p, 1, (int)(left * 1000) + 1);
            continue;
        }
        value_free(data);
        value_free(owner);
        if (e == EMSGSIZE) {
            snprintf(msg, sizeof msg,
                     "net.send_to: %zu bytes is longer than one datagram can carry (65507 over IPv4)", n);
        } else {
            snprintf(msg, sizeof msg, "net.send_to: sending failed (%s)", strerror(e));
        }
        return net_raise(net_errno_code(e), msg);
    }
    s->last_activity = net_now();
    value_free(data);
    value_free(owner);
    return value_number_exact((long long)n);
}

/* receive(socket [, timeout]) -> { data, from, truncated, size }, waited only */
static Value net_do_receive(AstExpr *expr) {
    const char *verb = "net.receive";
    Value owner;
    NetSocket *s = net_arg_datagram(expr, verb, &owner);
    if (!s) {
        return value_null();
    }
    if (expr->as.call.args.count > 2) {
        value_free(owner);
        return net_refuse("net.receive expects a datagram socket and an optional timeout");
    }
    if (s->watched) {
        value_free(owner);
        return net_refuse("net.receive: this socket is watched, so each datagram arrives as a datagram event");
    }
    double timeout = -1;
    if (expr->as.call.args.count == 2) {
        Value t = eval_expr(expr->as.call.args.items[1]);
        timeout = error_action_pending() ? -2 : net_seconds(t, verb);
        value_free(t);
        if (timeout < 0) {
            value_free(owner);
            return value_null();
        }
    }
    if (!net_blocking_warning(expr, verb)) {
        value_free(owner);
        return value_null();
    }
    double deadline = timeout >= 0 ? net_now() + timeout : -1;
    char msg[300];
    for (;;) {
        if (!s->open) {
            value_free(owner);
            return net_raise("closed", "net.receive: this datagram socket is closed");
        }
        Value r;
        const char *code = NULL;
        int got = net_dgram_recv_one(s, &r, &code, msg, sizeof msg);
        if (got > 0) {
            value_free(owner);
            return r;
        }
        if (got < 0) {
            value_free(owner);
            return net_raise(code, msg);
        }
        double left = deadline >= 0 ? deadline - net_now() : -1;
        if (deadline >= 0 && left <= 0) {
            value_free(owner);
            return net_raise("timeout", "net.receive: no datagram arrived within the timeout");
        }
        struct pollfd p = { s->fd, POLLIN, 0 };
        poll(&p, 1, left < 0 ? -1 : (int)(left * 1000) + 1);
    }
}

/* ---- upgrades and per-connection state (N4) ------------------------------------ *
 *
 * docs/networking_design.md §6. A WebSocket server is an HTTP request that
 * asks to switch protocols, so the webserver answers it with a response
 * carrying `upgrade: true`, and the socket leaves the webserver for `net`
 * here: the 101 goes out through this connection's send queue, bytes the
 * client sent past its request are this connection's first input, and a TLS
 * session moves with its descriptor. The connection is announced as an
 * `accepted` event with the request the response carried and no listener. */
static int net_adopt_upgrade(int fd, void *ssl, const char *head, size_t head_len, const char *rest,
                             size_t rest_len, Value *request, double idle, const char **err) {
    (void)err;
    NetSocket *s = net_socket_new(NET_CONNECTION);
    s->fd = fd;
    s->open = 1;
    s->watched = 1;
    s->loop_ref = 1;          /* the reference it was made with, as an accepted one */
    s->idle_s = idle;
    struct sockaddr_storage ss;
    socklen_t len = sizeof ss;
    s->family = getsockname(fd, (struct sockaddr *)&ss, &len) == 0 ? ss.ss_family : AF_INET;
    if (s->family == AF_INET || s->family == AF_INET6) {
        s->port = s->family == AF_INET6 ? ntohs(((struct sockaddr_in6 *)&ss)->sin6_port)
                                        : ntohs(((struct sockaddr_in *)&ss)->sin_port);
    }
    net_set_nonblocking(fd);
    net_socket_options(fd, s->family);
#if HAVE_LIBSSL
    if (ssl) {
        s->ssl = (SSL *)ssl;
        /* net's own write discipline: the queue moves between a refused
         * write and its retry (see net_tls_new_ssl) */
        SSL_set_mode(s->ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
        value_free(s->tls_info);
        s->tls_info = net_tls_snapshot(s->ssl);
    }
#else
    (void)ssl;
#endif
    if (rest_len > 0) {
        netbuf_append(&s->in, rest, rest_len);
    }
    netbuf_append(&s->out, head, head_len);
    s->accept_request = request ? value_copy(*request) : value_record(NULL, 0);
    s->connected_pending = 2;     /* owed as `accepted` */
    s->last_activity = net_now();
    const char *code = NULL;
    char msg[300];
    if (net_flush_some(s, &code, msg, sizeof msg) < 0) {
        net_fail(s, code, msg);
    }
    return 1;
}

/* set_state(connection, name, value): one named value a library keeps with a
 * connection (a WebSocket's handshake and fragments), read back as
 * `c.state.name`. Named, so two libraries over one connection do not collide.
 * A value that names the connection itself would keep it alive: do not. */
static Value net_do_set_state(AstExpr *expr) {
    Value owner;
    NetSocket *s = net_arg_socket(expr, "net.set_state", -1, &owner);
    if (!s) {
        return value_null();
    }
    if (expr->as.call.args.count != 3) {
        value_free(owner);
        return net_refuse("net.set_state expects a socket, a name and a value");
    }
    Value name = eval_expr(expr->as.call.args.items[1]);
    if (error_action_pending()) {
        value_free(name);
        value_free(owner);
        return value_null();
    }
    if (name.kind != VALUE_STRING || string_length(name.as.string) == 0) {
        value_free(name);
        value_free(owner);
        return net_refuse("net.set_state: the name must be text");
    }
    Value v = eval_expr(expr->as.call.args.items[2]);
    if (error_action_pending()) {
        value_free(v);
        value_free(name);
        value_free(owner);
        return value_null();
    }
    record_set(&s->state, name.as.string, v);
    value_free(name);
    value_free(owner);
    return value_null();
}

static Value net_eval_call(AstExpr *expr) {
    const char *name = expr->as.call.name;
    char verb[64];
    snprintf(verb, sizeof verb, "net.%s", name);
    if (!gb_net_init()) {      /* WSAStartup on Windows: before it every socket call fails */
        return net_raise("failed", "net: the system's socket layer could not be started");
    }
    if (strcmp(name, "connect") == 0) return net_do_connect(expr);
    if (strcmp(name, "listen") == 0) return net_do_listen(expr);
    if (strcmp(name, "resolve") == 0) return net_do_resolve(expr);
    if (strcmp(name, "start_tls") == 0) return net_do_start_tls(expr);
    if (strcmp(name, "datagram") == 0) return net_do_datagram(expr);
    if (strcmp(name, "send_to") == 0) return net_do_send_to(expr);
    if (strcmp(name, "receive") == 0) return net_do_receive(expr);
    if (strcmp(name, "set_state") == 0) return net_do_set_state(expr);
    if (strcmp(name, "write") == 0) return net_do_write(expr);
    if (strcmp(name, "take") == 0 || strcmp(name, "take_exactly") == 0 || strcmp(name, "take_until") == 0 ||
        strcmp(name, "take_line") == 0 || strcmp(name, "peek_exactly") == 0) {
        return net_do_take(expr, verb);
    }
    if (strcmp(name, "read") == 0 || strcmp(name, "read_exactly") == 0 || strcmp(name, "read_until") == 0 ||
        strcmp(name, "read_line") == 0) {
        return net_do_read(expr, verb);
    }
    if (strcmp(name, "buffered") == 0 || strcmp(name, "queued") == 0 || strcmp(name, "flush") == 0 ||
        strcmp(name, "shutdown_write") == 0 || strcmp(name, "close") == 0 || strcmp(name, "abort") == 0 ||
        strcmp(name, "accept") == 0) {
        return net_do_simple(expr, verb);
    }
    char message[200];
    module_unknown_verb(message, sizeof message, "net", name);
    return net_refuse(message);
}

/* A connection's or listener's fields, read with `.` (`c.peer`). Everything
 * here is a fact about the socket NOW, so it is answered fresh each time. */
static int net_field(NetSocket *s, const char *field, Value *out) {
    if (strcmp(field, "id") == 0) { *out = value_number_exact(s->id); return 1; }
    if (strcmp(field, "kind") == 0) {
        *out = value_string(s->kind == NET_LISTENER ? "listener" :
                            s->kind == NET_DATAGRAM ? "datagram" : "connection");
        return 1;
    }
    if (strcmp(field, "open") == 0) { *out = value_bool(s->open); return 1; }
    if (strcmp(field, "watched") == 0) { *out = value_bool(s->watched); return 1; }
    if (strcmp(field, "tls") == 0) {
#if HAVE_LIBSSL
        if (s->kind == NET_LISTENER && s->tls_server) {
            Value r = value_record(NULL, 0);
            Value hosts = value_array(NULL, 0);
            for (size_t i = 0; i < s->tls_server->count; i++) {
                Value ignored = append_to_array_ref(&hosts, value_string(s->tls_server->hosts[i]), 0);
                value_free(ignored);
            }
            record_set(&r, "hosts", hosts);
            static const char *const asks[] = { "none", "requested", "required" };
            record_set(&r, "client_certificate", value_string(asks[s->tls_server->client_certificate]));
            *out = r;
            return 1;
        }
#endif
        *out = value_copy(s->tls_info);
        return 1;
    }
    if (strcmp(field, "peer") == 0) {
        *out = s->kind != NET_CONNECTION ? value_unknown() : net_endpoint(s, 1);
        return 1;
    }
    if (strcmp(field, "local") == 0) { *out = net_endpoint(s, 0); return 1; }
    if (strcmp(field, "port") == 0) {
        *out = s->path ? value_unknown() : value_number_exact(s->port);
        return 1;
    }
    if (strcmp(field, "path") == 0) {
        *out = s->path ? value_string(s->path) : value_unknown();
        return 1;
    }
    if (strcmp(field, "peer_credentials") == 0) { *out = net_peer_credentials(s); return 1; }
    /* read from the socket, so the answer is the system's, not the option's */
    if (strcmp(field, "keepalive") == 0 || strcmp(field, "nodelay") == 0) {
        if (s->kind != NET_CONNECTION || s->fd < 0 || s->family == AF_UNIX || s->connecting) {
            *out = value_unknown();
        } else if (field[0] == 'k') {
            *out = net_keepalive_now(s->fd);
        } else {
            int on = 0;
            socklen_t len = sizeof on;
            *out = getsockopt(s->fd, IPPROTO_TCP, TCP_NODELAY, &on, &len) == 0 ? value_bool(on != 0)
                                                                              : value_unknown();
        }
        return 1;
    }
    if (strcmp(field, "state") == 0) { *out = value_copy(s->state); return 1; }
    if (strcmp(field, "max_datagram") == 0 && s->kind == NET_DATAGRAM) {
        *out = value_number_exact((long long)s->max_datagram);
        return 1;
    }
    if (strcmp(field, "connections") == 0 && s->kind == NET_LISTENER) {
        *out = value_number_exact((long long)s->children);
        return 1;
    }
    return 0;
}

/* ---- the event loop ------------------------------------------------------------------ */

static int net_socket_live(NetSocket *s) {
    return s->watched && (s->open || (s->closed_pending && !s->closed_delivered) || s->error_pending ||
                          s->connected_pending);
}

static int net_loop_active(void) {
    if (!net_events_watched()) {
        return 0;
    }
    for (size_t i = 0; i < net_socket_count; i++) {
        if (net_socket_live(net_sockets[i])) {
            return 1;
        }
    }
    return 0;
}

static size_t net_poll_count(void) {
    return net_socket_count;
}

/* Fill the loop's pollfds for the watched sockets. Answers how many were
 * written; `who[i]` records which socket slot i belongs to. */
static size_t net_collect_fds(struct pollfd *p, NetSocket **who, size_t max) {
    size_t n = 0;
    for (size_t i = 0; i < net_socket_count && n < max; i++) {
        NetSocket *s = net_sockets[i];
        if (!s->watched || !s->open) {
            continue;
        }
        short ev = 0;
        int fd = s->fd;
        if (s->resolving && s->resolve) {
            fd = s->resolve->pipe_fd[0];
            ev = POLLIN;
        } else if (s->kind == NET_LISTENER || s->kind == NET_DATAGRAM) {
            ev = POLLIN;
        } else if (s->connecting) {
            ev = POLLOUT;
        } else if (s->tls_handshaking) {
            ev = s->tls_want_write ? POLLOUT : POLLIN;
        } else {
            if ((!s->paused && !s->peer_closed) || s->tls_write_wants_read) {
                ev |= POLLIN;
            }
            if (s->out.len > 0 || s->tls_read_wants_write) {
                ev |= POLLOUT;
            }
        }
        if (fd < 0 || !ev) {
            continue;
        }
        p[n].fd = fd;
        p[n].events = ev;
        p[n].revents = 0;
        who[n] = s;
        n++;
    }
    return n;
}

/* How long the loop may wait before a timeout here comes due. */
static long net_poll_timeout_ms(long tick_ms) {
    double now = net_now();
    double best = (double)tick_ms / 1000.0;
    for (size_t i = 0; i < net_socket_count; i++) {
        NetSocket *s = net_sockets[i];
        if (!s->watched || !s->open) {
            continue;
        }
        if ((s->connecting || s->tls_handshaking) && s->connect_deadline > 0 && s->connect_deadline - now < best) {
            best = s->connect_deadline - now;
        }
#if HAVE_LIBSSL
        if (net_tls_has_pending(s)) {
            best = 0;
        }
#endif
        if (s->idle_s > 0 && s->kind == NET_CONNECTION && s->last_activity + s->idle_s - now < best) {
            best = s->last_activity + s->idle_s - now;
        }
        if (s->connected_pending || s->closed_pending || s->error_pending || s->tls_event_pending ||
            (s->dgram_pending.kind == VALUE_ARRAY && s->dgram_pending.as.array.store->count > 0)) {
            best = 0;
        }
    }
    if (best < 0) {
        best = 0;
    }
    return (long)(best * 1000.0);
}

/* The I/O half: act on what the poll reported. No event is emitted here; the
 * state changes are recorded and net_deliver reports them, so a watcher that
 * closes or makes sockets cannot disturb the walk. */
static void net_service_io(struct pollfd *p, NetSocket **who, size_t n) {
    char msg[300];
    for (size_t i = 0; i < n; i++) {
        NetSocket *s = who[i];
        short re = p[i].revents;
#if HAVE_LIBSSL
        if (net_tls_has_pending(s)) {
            re |= POLLIN;            /* bytes libssl holds are readable whatever poll says */
        }
#endif
        if (!re || !s->open) {
            continue;
        }
        const char *code = NULL;
        int last_errno = 0;
        if (s->resolving && s->resolve) {
            NetResolve *r = s->resolve;
            s->resolve = NULL;
            s->resolving = 0;
            int rc = r->rc;
            struct addrinfo *res = r->result;
            r->result = NULL;
            char host[256];
            snprintf(host, sizeof host, "%s", r->host);
            net_resolve_finish(r);
            if (rc != 0) {
                snprintf(msg, sizeof msg, "net.connect: %s does not resolve (%s)", host, gai_strerror(rc));
                net_fail(s, net_gai_code(rc), msg);
                continue;
            }
            s->addrs = res;
            s->next_addr = res;
            if (!net_try_next(s, &code, &last_errno)) {
                snprintf(msg, sizeof msg, "net.connect: could not connect to %s (%s)", host,
                         strerror(last_errno ? last_errno : ECONNREFUSED));
                net_fail(s, code, msg);
            } else if (!s->connecting) {
                net_connected(s, value_null());
            }
            continue;
        }
        if (s->kind == NET_DATAGRAM) {
            /* A bounded number a pass, so one busy socket cannot hold the
             * loop; the rest wait in the kernel, which drops what overflows
             * its buffer, as UDP does everywhere. */
            for (int k = 0; k < NET_DATAGRAMS_PER_PASS; k++) {
                Value r;
                int got = net_dgram_recv_one(s, &r, &code, msg, sizeof msg);
                if (got < 0) {
                    net_fail(s, code, msg);
                    break;
                }
                if (got == 0) {
                    break;
                }
                Value ignored = append_to_array_ref(&s->dgram_pending, r, 0);
                value_free(ignored);
            }
            continue;
        }
        if (s->kind == NET_LISTENER) {
            for (int k = 0; k < 64; k++) {
                int limited = 0;
                NetSocket *c = net_accept_one(s, &limited);
                if (limited) {
                    /* recorded as a pending error on the listener itself */
                    if (!s->error_pending) {
                        free(s->error_code);
                        free(s->error_message);
                        s->error_code = copy_string("limit");
                        s->error_message = copy_string(
                            "net.listen: a connection was refused because max_connections is reached");
                        s->error_pending = 1;
                    }
                    continue;
                }
                if (!c) {
                    break;
                }
                /* THE LOOP'S REFERENCE IS THE ONE IT WAS MADE WITH: no
                 * program value names an accepted connection until an event
                 * hands it over, and each event holds its own. Taking a second
                 * one here leaked every accepted connection (valgrind,
                 * run_net.sh's watched-server tier). */
                c->loop_ref = 1;
                if (!c->tls_handshaking) {
                    c->connected_pending = 2;    /* 2 = owed as an `accepted` event */
                }                                /* (a TLS one owes it when the handshake is done) */
            }
            continue;
        }
        if (s->connecting) {
            int step = net_connect_step(s, &code, &last_errno);
            if (step == 1) {
                net_connected(s, value_null());
                if (s->addrs) {
                    freeaddrinfo(s->addrs);
                    s->addrs = NULL;
                    s->next_addr = NULL;
                }
                net_flush_some(s, &code, msg, sizeof msg);
            } else if (step == 0) {
                snprintf(msg, sizeof msg, "net.connect: could not connect (%s)", strerror(last_errno));
                net_fail(s, code, msg);
            }
            continue;
        }
#if HAVE_LIBSSL
        if (s->tls_handshaking) {
            char reason[256];
            int step = net_tls_step(s, &code, msg, sizeof msg, reason, sizeof reason);
            if (step < 0) {
                net_fail(s, code, msg);
                continue;
            }
            /* Finished or not, nothing more this pass. Bytes still in the
             * kernel are the next poll's, bytes libssl holds are
             * net_tls_has_pending's, and what the program wrote is polled for
             * because it is queued (forcing a read and a write here was
             * measured to change nothing). */
            continue;
        }
#endif
        if ((re & (POLLIN | POLLHUP | POLLERR)) || (s->tls_read_wants_write && (re & POLLOUT))) {
            long got = net_fill(s, &code, msg, sizeof msg);
            if (got < 0) {
                net_fail(s, code, msg);
                continue;
            }
            /* NEW BYTES OWE AN EVENT, whatever the program took before. Re-arming
             * only on a successful take was the first version, and it stalled
             * for good when a frame arrived a byte at a time: the first byte's
             * event found no whole line, nothing was taken, and the rest of the
             * line was never reported (measured, tests/net/peer.py split). */
            if (got > 0) {
                s->data_notified = 0;
            }
            if (s->in.len >= s->in_ceiling) {
                s->paused = 1;
            }
        }
        if (((re & POLLOUT) || (s->tls_write_wants_read && (re & POLLIN))) && s->out.len > 0) {
            if (net_flush_some(s, &code, msg, sizeof msg) < 0) {
                net_fail(s, code, msg);
                continue;
            }
            if (s->out.len == 0 && s->write_shut) {
                net_shut_write(s);
            }
        }
        if (re & POLLNVAL) {
            net_fail(s, "failed", "net: the descriptor is no longer valid");
        }
    }
}

/* The timing half: connect deadlines and idle connections. */
static void net_service_time(void) {
    double now = net_now();
    for (size_t i = 0; i < net_socket_count; i++) {
        NetSocket *s = net_sockets[i];
        if (!s->watched || !s->open || s->kind != NET_CONNECTION) {
            continue;
        }
        if (s->connecting && s->connect_deadline > 0 && now >= s->connect_deadline) {
            net_fail(s, "timeout", "net.connect: the connection did not complete within the timeout");
            continue;
        }
        if (s->tls_handshaking && ((s->connect_deadline > 0 && now >= s->connect_deadline) ||
                                   (s->connect_deadline <= 0 && s->idle_s > 0 &&
                                    now - s->last_activity >= s->idle_s))) {
            net_fail(s, "timeout", "net: the TLS handshake did not finish within the timeout");
            continue;
        }
        if (!s->connecting && s->idle_s > 0 && now - s->last_activity >= s->idle_s) {
            s->error_pending = 0;
            if (!s->close_code) {
                s->close_code = copy_string("idle");
            }
            s->connected_pending = 3;    /* 3 = owed as an `idle` event, then closed */
            net_close_fd(s);
            s->closed_pending = 1;
        }
        if (s->closing && s->out.len == 0 && s->open) {
            net_mark_closed(s, "local");
        }
    }
}

/* Report what changed. Returns 0 when a watcher raised. Walks a SNAPSHOT,
 * holding a reference to each, so a watcher that makes or closes sockets
 * cannot shift the registry under the walk. */
static int net_deliver(void) {
    size_t n = net_socket_count;
    if (n == 0) {
        return 1;
    }
    NetSocket **snap = malloc(sizeof(NetSocket *) * n);
    if (!snap) {
        abort();
    }
    for (size_t i = 0; i < n; i++) {
        snap[i] = net_sockets[i];
        snap[i]->ref_count++;
    }
    int ok = 1;
    for (size_t i = 0; i < n && ok; i++) {
        NetSocket *s = snap[i];
        if (!s->watched) {
            continue;
        }
        if (s->connected_pending == 2) {
            s->connected_pending = 0;
            s->announced = 1;
            if (s->accept_request.kind != VALUE_NULL) {
                /* an upgrade: no listener of net's, the request instead */
                Value extra = value_record(NULL, 0);
                record_set(&extra, "request", s->accept_request);
                s->accept_request = value_null();
                ok = net_emit_full("accepted", s, NULL, NULL, NULL, &extra);
                value_free(extra);
            } else {
                ok = net_emit("accepted", s, s->listener, NULL, NULL);
            }
            if (!ok) break;
        } else if (s->connected_pending == 1) {
            s->connected_pending = 0;
            ok = net_emit("connected", s, NULL, NULL, NULL);
            if (!ok) break;
        }
        if (s->tls_event_pending) {
            s->tls_event_pending = 0;
            ok = net_emit("tls", s, NULL, NULL, NULL);
            if (!ok) break;
        }
        if (s->kind == NET_DATAGRAM && s->dgram_pending.kind == VALUE_ARRAY) {
            /* One event per datagram, in arrival order. They are taken off
             * the front one at a time, so a watcher that raises leaves the
             * rest owed rather than lost. */
            ArrayStorage *st = s->dgram_pending.as.array.store;
            size_t done = 0;
            while (ok && done < st->count) {
                Value r = st->items[done];
                done++;
                ok = net_emit_full("datagram", s, NULL, NULL, NULL, &r);
                value_free(r);
            }
            memmove(st->items, st->items + done, sizeof(Value) * (st->count - done));
            st->count -= done;
            if (!ok) break;
        }
        if (s->kind == NET_CONNECTION && s->in.len > 0 && !s->data_notified) {
            s->data_notified = 1;
            ok = net_emit("data", s, NULL, NULL, NULL);
            if (!ok) break;
        }
        if (s->paused && !s->full_notified) {
            s->full_notified = 1;
            ok = net_emit("full", s, NULL, "full",
                          "the receive buffer reached its ceiling; reading has paused until the program takes");
            if (!ok) break;
        }
        if (s->want_writable && s->out.len < NET_LOW_WATER && s->open) {
            s->want_writable = 0;
            ok = net_emit("writable", s, NULL, NULL, NULL);
            if (!ok) break;
        }
        if (s->peer_closed && s->open && s->in.len == 0) {
            /* the peer is gone and everything it sent has been taken */
            net_mark_closed(s, "peer");
        } else if (s->peer_closed && s->open && s->data_notified && s->kind == NET_CONNECTION) {
            /* bytes remain, but nothing more will arrive: closed is reported now,
             * and the bytes stay takeable */
            net_mark_closed(s, "peer");
        }
        if (s->error_pending) {
            s->error_pending = 0;
            /* A connection a TLS listener never announced (its handshake
             * failed) names that listener, the one thing the program knows. */
            ok = net_emit("error", s->kind != NET_LISTENER ? s : NULL,
                          s->kind == NET_LISTENER ? s : (s->announced ? NULL : s->listener),
                          s->error_code, s->error_message);
            if (s->kind == NET_LISTENER) {
                free(s->error_code);
                s->error_code = NULL;
                free(s->error_message);
                s->error_message = NULL;
            }
            if (!ok) break;
        }
        if (s->connected_pending == 3) {
            s->connected_pending = 0;
            ok = net_emit("idle", s, NULL, "idle", "the connection was idle past its timeout and is closed");
            if (!ok) break;
        }
        if (s->closed_pending && !s->closed_delivered) {
            s->closed_delivered = 1;
            s->closed_pending = 0;
            ok = net_emit("closed", s->kind != NET_LISTENER ? s : NULL,
                          s->kind == NET_LISTENER ? s : (s->announced ? NULL : s->listener),
                          s->close_code ? s->close_code : "local", NULL);
            net_drop_loop_ref(s);
            if (!ok) break;
        }
    }
    for (size_t i = 0; i < n; i++) {
        net_socket_release(snap[i]);
    }
    free(snap);
    return ok;
}

/* Close everything at exit. A watched socket's loop reference is the only
 * thing keeping some of them alive, so dropping those frees them. */
static void net_shutdown(void) {
    size_t guard = net_socket_count * 2 + 8;
    while (net_socket_count > 0 && guard-- > 0) {
        NetSocket *s = net_sockets[net_socket_count - 1];
        net_close_fd(s);
        if (s->resolve) {
            net_resolve_finish(s->resolve);
            s->resolve = NULL;
        }
        if (s->loop_ref) {
            net_drop_loop_ref(s);       /* may free it, removing it from the registry */
        } else {
            net_registry_remove(s);     /* values still name it; they free it later */
        }
    }
    free(net_sockets);
    net_sockets = NULL;
    net_socket_count = 0;
#if HAVE_LIBSSL
    if (net_tls_default_client) {
        SSL_CTX_free(net_tls_default_client);
        net_tls_default_client = NULL;
    }
#endif
}

#ifdef _WIN32
/* the Winsock wrappers end with this file (see the block at its top) */
#undef socket
#undef connect
#undef bind
#undef listen
#undef accept
#undef recv
#undef send
#undef sendto
#undef recvmsg
#undef setsockopt
#undef getsockopt
#undef getsockname
#undef getpeername
#undef shutdown
#undef close
#undef write
#undef pipe
#undef strerror
#undef lstat
#endif
