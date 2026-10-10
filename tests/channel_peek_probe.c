/* Does gb_channel_peek_len size a frame correctly WITHOUT consuming it?
 *
 * WHY THIS IS A C PROGRAM AND NOT A gBASIC FIXTURE: the function has two
 * implementations and the one that matters here is the one this machine does
 * not use. Linux answers with a single zero-byte `recv(MSG_PEEK|MSG_TRUNC)`,
 * its own extension; macOS and the BSDs have no such thing, so they take a
 * grow-and-peek loop that NOTHING COULD EXERCISE -- no such machine is
 * reachable from this tree, and the first symptom of getting it wrong is actors
 * that deliver nothing at all, silently, because a mis-sized peek of 0 reads as
 * "the peer closed".
 *
 * Compiled TWICE by the runner, once per branch, and required to agree. That is
 * what makes the Linux implementation an ORACLE for the portable one rather
 * than two things nobody compared.
 *
 * SOCK_DGRAM deliberately, because that is what gb_channel_socketpair falls
 * back to where SOCK_SEQPACKET does not exist -- which is exactly the platform
 * the portable branch is for.
 */
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int fails = 0;

static void expect(const char *label, long got, long want) {
    if (got == want) {
        printf("  ok   %s (%ld)\n", label, got);
    } else {
        printf("  FAIL %s: got %ld, want %ld\n", label, got, want);
        fails++;
    }
}

int main(void) {
    /* UNBUFFERED, because a hang must show WHERE it hung: block-buffered
     * output is lost entirely when the runner's timeout kills this. */
    setvbuf(stdout, NULL, _IONBF, 0);
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) != 0) {
        printf("  FAIL socketpair\n");
        return 1;
    }

    /* Sizes chosen to straddle the grow loop's first buffer (4096): under it,
     * exactly on it, and well past it, since an off-by-one in the doubling
     * would show only at the boundary. */
    const size_t sizes[] = { 4, 100, 4095, 4096, 4097, 9000, 70000 };
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        size_t n = sizes[i];
        char *payload = malloc(n);
        if (!payload) { printf("  FAIL malloc\n"); return 1; }
        memset(payload, 'x', n);
        if (send(sv[1], payload, n, 0) != (ssize_t)n) {
            /* A datagram larger than the socket buffer is refused by the
             * kernel, not by us -- reported rather than counted as a failure,
             * since it is a fact about the machine. */
            printf("  skip  %zu bytes (the socket would not take it)\n", n);
            free(payload);
            continue;
        }
        char label[64];
        snprintf(label, sizeof label, "peek sizes a %zu-byte frame", n);
        expect(label, (long)gb_channel_peek_len(sv[0]), (long)n);

        /* AND IT DID NOT CONSUME: the frame must still be there, whole. */
        char *back = malloc(n);
        ssize_t got = recv(sv[0], back, n, 0);
        snprintf(label, sizeof label, "  and the frame is still readable");
        expect(label, (long)got, (long)n);
        free(back);
        free(payload);
    }

    close(sv[1]);
    close(sv[0]);

    /* CLOSURE, AND THIS TIER FOUND SOMETHING. `channel_recv_fds` reads a
     * zero-length frame as "the peer closed", which is sound only if the socket
     * HAS end-of-file semantics. MEASURED, and the two types disagree:
     *
     *   SOCK_SEQPACKET  peer closes -> recv returns 0        (closure is seen)
     *   SOCK_DGRAM      peer closes -> recv BLOCKS FOREVER   (it is not)
     *
     * Linux's actor channel is SEQPACKET so it is correct there. macOS has no
     * SOCK_SEQPACKET on AF_UNIX and gb_channel_socketpair falls back to
     * SOCK_DGRAM -- so on macOS an actor whose peer exits would leave the
     * reader blocked rather than reporting a clean shutdown. A HANG, not a
     * failure, which is the one outcome a test suite cannot tell from slow.
     *
     * So this is pinned as a PROPERTY OF EACH SOCKET TYPE rather than as a
     * single expectation, and the DGRAM half is read NON-BLOCKING so this probe
     * cannot become the hang it is describing.
     */
    int seq_closed = -1, dgram_closed = -1;
#ifdef SOCK_SEQPACKET
    {
        int p2[2];
        if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, p2) == 0) {
            close(p2[1]);
            char b[8];
            seq_closed = (int)recv(p2[0], b, sizeof b, MSG_DONTWAIT);
            close(p2[0]);
        }
    }
    expect("SEQPACKET: a closed peer reads as end of file", seq_closed, 0);
#else
    printf("  skip  SEQPACKET (not on this platform -- which is the macOS case)\n");
#endif
    {
        int p3[2];
        if (socketpair(AF_UNIX, SOCK_DGRAM, 0, p3) == 0) {
            close(p3[1]);
            char b[8];
            dgram_closed = (int)recv(p3[0], b, sizeof b, MSG_DONTWAIT);
            close(p3[0]);
        }
    }
    /* -1 with EAGAIN: nothing to read and NO end of file. Asserted as the
     * finding it is -- if a platform ever makes this 0, DGRAM would be a usable
     * fallback and this line is where that is noticed. */
    expect("DGRAM: a closed peer does NOT read as end of file", dgram_closed, -1);

    printf("%s\n", fails == 0 ? "probe: all passed" : "probe: FAILED");
    return fails == 0 ? 0 : 1;
}
