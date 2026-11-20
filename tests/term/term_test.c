/* The terminal layer's pure parts, from C (tests/run_term.sh tier `unit`):
 * the key decoder against byte sequences written by hand from xterm's
 * ctlseqs, and the screen's cells and redraw. The redraw's bytes are read
 * back by an independent emulator in the pty tier; here they are checked for
 * the property that matters most: a second render of an unchanged screen
 * writes no cell. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "term.h"

static int failures = 0, checks = 0;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *mods_text(unsigned m) {
    static char b[32];
    snprintf(b, sizeof b, "%s%s%s", m & TERM_MOD_CTRL ? "ctrl+" : "", m & TERM_MOD_ALT ? "alt+" : "", m & TERM_MOD_SHIFT ? "shift+" : "");
    return b;
}

/* Feeds `bytes`, drains with flush, and answers the events as one line:
 * "key:mods+name/text ..." */
static const char *decode(const char *bytes, size_t n, int flush) {
    static char out[1024];
    TermDecoder d;
    term_decoder_init(&d);
    term_decoder_feed(&d, (const unsigned char *)bytes, n);
    out[0] = 0;
    TermEvent e;
    while (term_decoder_next(&d, flush, &e)) {
        char one[256];
        if (e.kind == TERM_EV_KEY) {
            snprintf(one, sizeof one, "%s%s/%s ", mods_text(e.mods), e.key, e.text);
        } else if (e.kind == TERM_EV_MOUSE) {
            snprintf(one, sizeof one, "mouse:%s%s,%d,%d,%d ", mods_text(e.mods), e.action, e.button, e.column, e.row);
        } else {
            snprintf(one, sizeof one, "paste[%zu]:%s ", e.data_len, e.data);
        }
        strncat(out, one, sizeof out - strlen(out) - 1);
        term_event_free(&e);
    }
    if (term_decoder_pending(&d)) {
        strncat(out, "(pending)", sizeof out - strlen(out) - 1);
    }
    term_decoder_free(&d);
    return out;
}

#define DEC(s, flush, want) do { const char *got = decode(s, sizeof(s) - 1, flush); CHECK(strcmp(got, want) == 0, "decode %s: got [%s] want [%s]", #s, got, want); } while (0)

static void keys(void) {
    DEC("a", 0, "a/a ");
    DEC("A", 0, "A/A ");
    DEC(" ", 0, "space/  ");
    DEC("\r", 0, "enter/ ");
    DEC("\t", 0, "tab/ ");
    DEC("\x7f", 0, "backspace/ ");
    DEC("\x03", 0, "ctrl+c/ ");
    DEC("\x1a", 0, "ctrl+z/ ");
    DEC("\x00", 0, "ctrl+space/ ");
    DEC("\xc3\xa9", 0, "\xc3\xa9/\xc3\xa9 ");
    DEC("\xe4\xb8\xad", 0, "\xe4\xb8\xad/\xe4\xb8\xad ");
    DEC("\xf0\x9f\x98\x80", 0, "\xf0\x9f\x98\x80/\xf0\x9f\x98\x80 ");
    /* a codepoint cut short waits, and is given up on only at flush */
    DEC("\xe4\xb8", 0, "(pending)");
    DEC("\xe4\xb8", 1, "\xef\xbf\xbd/\xef\xbf\xbd \xef\xbf\xbd/\xef\xbf\xbd ");
    DEC("\xff", 0, "\xef\xbf\xbd/\xef\xbf\xbd ");
    /* arrows, normal and application mode */
    DEC("\x1b[A\x1b[B\x1b[C\x1b[D", 0, "up/ down/ right/ left/ ");
    DEC("\x1bOA\x1bOD", 0, "up/ left/ ");
    DEC("\x1b[H\x1b[F\x1b[1~\x1b[4~", 0, "home/ end/ home/ end/ ");
    DEC("\x1b[2~\x1b[3~\x1b[5~\x1b[6~", 0, "insert/ delete/ page_up/ page_down/ ");
    DEC("\x1bOP\x1bOS\x1b[15~\x1b[24~", 0, "f1/ f4/ f5/ f12/ ");
    /* xterm's modifier parameter */
    DEC("\x1b[1;5A", 0, "ctrl+up/ ");
    DEC("\x1b[1;2D", 0, "shift+left/ ");
    DEC("\x1b[1;3C", 0, "alt+right/ ");
    DEC("\x1b[1;8B", 0, "ctrl+alt+shift+down/ ");
    DEC("\x1b[3;5~", 0, "ctrl+delete/ ");
    DEC("\x1b[1;5P", 0, "ctrl+f1/ ");
    DEC("\x1b[Z", 0, "shift+tab/ ");
    /* alt as an ESC prefix */
    DEC("\x1b" "x", 0, "alt+x/ ");
    DEC("\x1b\x03", 0, "ctrl+alt+c/ ");
    DEC("\x1b\xc3\xa9", 0, "alt+\xc3\xa9/ ");
    /* a lone ESC waits for what may follow; at flush it is the key */
    DEC("\x1b", 0, "(pending)");
    DEC("\x1b", 1, "escape/ ");
    DEC("\x1b\x1b", 1, "escape/ escape/ ");
    DEC("\x1b[", 0, "(pending)");
    DEC("\x1b[1;5", 0, "(pending)");
    DEC("\x1b[1;5", 1, "escape/ [/[ 1/1 ;/; 5/5 ");
    /* an unknown sequence is dropped whole, and what follows survives */
    DEC("\x1b[99~q", 0, "q/q ");
    DEC("\x1b[?1;2cq", 0, "q/q ");
    /* SGR mouse */
    DEC("\x1b[<0;10;5M\x1b[<0;10;5m", 0, "mouse:press,1,10,5 mouse:release,1,10,5 ");
    DEC("\x1b[<2;1;1M", 0, "mouse:press,3,1,1 ");
    DEC("\x1b[<64;3;4M\x1b[<65;3;4M", 0, "mouse:wheel,4,3,4 mouse:wheel,5,3,4 ");
    DEC("\x1b[<32;7;8M\x1b[<35;7;9M", 0, "mouse:move,1,7,8 mouse:move,0,7,9 ");
    DEC("\x1b[<16;2;2M\x1b[<4;2;2M", 0, "mouse:ctrl+press,1,2,2 mouse:shift+press,1,2,2 ");
    DEC("\x1b[<0;0;5M", 0, "");
    /* bracketed paste: one event, whatever is inside, ESC included */
    DEC("\x1b[200~hi\x1b[Athere\r\n\x1b[201~x", 0, "paste[12]:hi\x1b[Athere\r\n x/x ");
    DEC("\x1b[200~unfinished", 1, "(pending)");
}

/* A sequence split at every byte boundary decodes as it does whole. */
static void splits(void) {
    static const char *const seqs[] = {
        "\x1b[1;5A", "\x1b[<0;10;5M", "\x1b[200~ab\x1b[201~", "\xe4\xb8\xad", "\x1bOP", "\x1b[15~", "\x1b" "x",
    };
    for (size_t k = 0; k < sizeof seqs / sizeof seqs[0]; k++) {
        const char *s = seqs[k];
        size_t n = strlen(s);
        char whole[256];
        snprintf(whole, sizeof whole, "%s", decode(s, n, 0));
        for (size_t cut = 1; cut < n; cut++) {
            TermDecoder d;
            term_decoder_init(&d);
            char got[256] = "";
            TermEvent e;
            term_decoder_feed(&d, (const unsigned char *)s, cut);
            int early = 0;
            while (term_decoder_next(&d, 0, &e)) {
                early++;
                term_event_free(&e);
            }
            term_decoder_feed(&d, (const unsigned char *)s + cut, n - cut);
            while (term_decoder_next(&d, 0, &e)) {
                if (e.kind == TERM_EV_KEY) {
                    snprintf(got + strlen(got), sizeof got - strlen(got), "%s%s/%s ", mods_text(e.mods), e.key, e.text);
                } else {
                    snprintf(got + strlen(got), sizeof got - strlen(got), "%d ", (int)e.kind);
                }
                term_event_free(&e);
            }
            CHECK(early == 0, "seq %zu cut at %zu: %d events before the rest arrived", k, cut, early);
            if (strncmp(whole, "mouse", 5) != 0 && strncmp(whole, "paste", 5) != 0) {
                CHECK(strcmp(got, whole) == 0, "seq %zu cut at %zu: [%s] want [%s]", k, cut, got, whole);
            }
            term_decoder_free(&d);
        }
    }
}

static void widths(void) {
    /* written by hand from UnicodeData.txt and EastAsianWidth.txt */
    struct { unsigned cp; int w; } want[] = {
        { 0x41, 1 }, { 0x7E, 1 }, { 0x1F, -1 }, { 0x7F, -1 }, { 0x9F, -1 }, { 0xA0, 1 },
        { 0xAD, 1 },      /* SOFT HYPHEN: Cf, but drawn */
        { 0xE9, 1 }, { 0x301, 0 }, { 0x200B, 0 }, { 0x200D, 0 }, { 0xFE0F, 0 },
        { 0x600, 1 },     /* ARABIC NUMBER SIGN: a prepended concatenation mark */
        { 0x1160, 0 }, { 0x11A8, 0 }, { 0x1100, 2 }, { 0xAC00, 2 },
        { 0x4E2D, 2 }, { 0x3000, 2 }, { 0xFF21, 2 }, { 0xFF61, 1 },
        { 0x1F600, 2 }, { 0x2603, 1 }, { 0x2614, 2 }, { 0x20000, 2 },
        { 0x3B1, 1 },     /* Greek alpha: ambiguous, one cell */
    };
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        CHECK(term_width(want[i].cp) == want[i].w, "width U+%04X: %d want %d", want[i].cp, term_width(want[i].cp), want[i].w);
    }
    CHECK(strcmp(term_width_unicode(), "16.0.0") == 0, "unicode %s", term_width_unicode());
}

static int count(const char *hay, const char *needle) {
    int n = 0;
    for (const char *p = hay; (p = strstr(p, needle)); p += strlen(needle)) {
        n++;
    }
    return n;
}

static void screen(void) {
    TermScreen s;
    TermStyle plain = { TERM_DEFAULT, TERM_DEFAULT, 0 };
    TermStyle red = { 0xFF0000, TERM_DEFAULT, TERM_BOLD };
    CHECK(term_screen_init(&s, 20, 5, TERM_DEPTH_24BIT) == 0, "init");
    size_t bad = 99;
    CHECK(term_screen_put(&s, 1, 1, "hello", 5, plain, &bad) == 6, "put answers the next column");
    CHECK(term_screen_put(&s, 1, 2, "a\x1b[2Jb", 6, plain, &bad) == -1 && bad == 1, "a control character is refused at its offset");
    CHECK(s.want[20].ch[0] == ' ', "a refused put writes nothing");
    CHECK(term_screen_put(&s, 1, 2, "ok\xc3", 3, plain, &bad) == -1 && bad == 2, "invalid UTF-8 refused at its offset");
    /* clipping, and a wide character that would straddle the edge */
    CHECK(term_screen_put(&s, 18, 3, "abcdef", 6, plain, &bad) == 21, "clipped at the right edge");
    CHECK(strcmp(s.want[2 * 20 + 19].ch, "c") == 0, "last column holds c");
    term_screen_put(&s, 20, 4, "\xe4\xb8\xad", 3, plain, &bad);
    CHECK(s.want[3 * 20 + 19].ch[0] == ' ', "a wide character does not straddle the edge");
    term_screen_put(&s, 1, 4, "\xe4\xb8\xad" "x", 4, red, &bad);
    CHECK(s.want[60].width == 2 && s.want[61].width == 0 && s.want[61].ch[0] == 0 && strcmp(s.want[62].ch, "x") == 0, "wide character takes two cells");
    /* overwriting half of a wide character blanks the other half */
    term_screen_put(&s, 2, 4, "y", 1, plain, &bad);
    CHECK(s.want[60].ch[0] == ' ' && s.want[60].width == 1 && strcmp(s.want[61].ch, "y") == 0, "right half overwritten: left half blank");
    /* a combining mark joins the cell before it */
    term_screen_put(&s, 1, 5, "e\xcc\x81z", 4, plain, &bad);
    CHECK(strcmp(s.want[80].ch, "e\xcc\x81") == 0 && strcmp(s.want[81].ch, "z") == 0, "combining mark joins");

    TermBuf b = { 0 };
    term_screen_render(&s, &b);
    CHECK(count(b.p, "\x1b[2J") == 1, "first render clears");
    CHECK(strstr(b.p, "hello") != NULL, "a run of cells is written without moving between them");
    CHECK(strstr(b.p, "38;2;255;0;0") != NULL, "24-bit colour");
    term_buf_free(&b);
    term_screen_render(&s, &b);
    CHECK(count(b.p, "H") == 0 && strstr(b.p, "hello") == NULL, "an unchanged screen writes no cell: [%s]", b.p);
    term_buf_free(&b);
    /* a wide character, rendered and rendered again: nothing the second time */
    term_screen_put(&s, 10, 1, "\xe4\xb8\xad", 3, plain, &bad);
    term_screen_render(&s, &b);
    CHECK(strstr(b.p, "\xe4\xb8\xad") != NULL, "the wide character is drawn");
    term_buf_free(&b);
    term_screen_render(&s, &b);
    CHECK(strstr(b.p, "\xe4\xb8\xad") == NULL && count(b.p, "H") == 0, "an unchanged wide character is not drawn again: [%s]", b.p);
    term_buf_free(&b);
    term_screen_put(&s, 3, 1, "L", 1, plain, &bad);
    term_screen_render(&s, &b);
    CHECK(strstr(b.p, "\x1b[1;3HL") != NULL && count(b.p, ";1H") == 0, "one changed cell, one move: [%s]", b.p);
    term_buf_free(&b);
    /* colour depth */
    s.depth = TERM_DEPTH_256;
    term_screen_put(&s, 1, 1, "Q", 1, red, &bad);
    term_screen_render(&s, &b);
    CHECK(strstr(b.p, "38;5;196") != NULL, "256 colours: pure red is 196: [%s]", b.p);
    term_buf_free(&b);
    s.depth = TERM_DEPTH_16;
    TermStyle navy = { 0x000080, 0x00FF00, 0 };
    term_screen_put(&s, 1, 1, "R", 1, navy, &bad);
    term_screen_render(&s, &b);
    CHECK(strstr(b.p, ";34;102m") != NULL, "16 colours: navy is blue, green background is bright green: [%s]", b.p);
    term_buf_free(&b);
    /* resize keeps what fits and redraws all */
    CHECK(term_screen_resize(&s, 3, 2) == 0 && strcmp(s.want[2].ch, "L") == 0, "resize keeps cells");
    term_screen_render(&s, &b);
    CHECK(count(b.p, "\x1b[2J") == 1, "resize redraws all");
    term_buf_free(&b);
    term_screen_free(&s);

    CHECK(term_depth_from_env("truecolor", "xterm") == TERM_DEPTH_24BIT, "COLORTERM truecolor");
    CHECK(term_depth_from_env(NULL, "xterm-256color") == TERM_DEPTH_256, "TERM 256color");
    CHECK(term_depth_from_env("", "xterm") == TERM_DEPTH_16, "plain xterm");
    CHECK(term_depth_from_env(NULL, "xterm-direct") == TERM_DEPTH_24BIT, "xterm-direct");
}

int main(void) {
    keys();
    splits();
    widths();
    screen();
    printf("term unit: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
