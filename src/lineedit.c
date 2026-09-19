/* Line editing and history for the gBASIC prompt.
 *
 * WHY IT IS HERE AND NOT LINKED: see include/lineedit.h. In short, readline's
 * licence and libedit's availability both cost more than the code does.
 *
 * WHAT IT IS: one line, edited in place, with history. Deliberately ONE line --
 * a multi-line block is entered a line at a time and each line goes into
 * history on its own, which is what every prompt with a readline behind it
 * does. Recalling a whole function is what the resident program is for
 * (`list`, retype the declaration, `run`).
 *
 * THE CTRL-C SPLIT IS THE PART WORTH READING. Raw mode is held ONLY while a
 * line is being edited, and it turns ISIG OFF, so Ctrl-C there is a byte this
 * file reads and means "throw this line away". While a chunk RUNS the terminal
 * is back in its ordinary mode with ISIG on, so Ctrl-C is a signal and
 * interrupts the program (src/repl.c). Two different and both correct meanings
 * for one key, decided by which of the two the prompt is doing.
 *
 * UTF-8: the buffer is bytes and the cursor moves by CODEPOINT, skipping
 * continuation bytes, so an accented character deletes in one press rather than
 * leaving half of itself behind. Display width is assumed to be one column per
 * codepoint, which is wrong for CJK and for combining marks; the consequence is
 * a misplaced cursor on a line containing them, never a corrupted line.
 *
 * LINES LONGER THAN THE TERMINAL are scrolled horizontally rather than wrapped.
 * Wrapping is what a naive redraw gets wrong -- it has no idea how many rows it
 * has used, so the next redraw paints over the wrong ones and the screen fills
 * with fragments. Scrolling keeps every redraw to exactly one row. */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>

#include "lineedit.h"

#define LE_MAX_HISTORY 1000

/* ---- history ------------------------------------------------------------- */

static char **history = NULL;
static size_t history_count = 0;

void line_history_add(const char *line) {
    if (!line || !*line) {
        return;
    }
    int blank = 1;
    for (const char *c = line; *c; c++) {
        if (!isspace((unsigned char)*c)) {
            blank = 0;
            break;
        }
    }
    if (blank) {
        return;
    }
    /* An immediate repeat is not a second thing you did. */
    if (history_count && strcmp(history[history_count - 1], line) == 0) {
        return;
    }
    if (history_count == LE_MAX_HISTORY) {
        free(history[0]);
        memmove(&history[0], &history[1], sizeof(char *) * (LE_MAX_HISTORY - 1));
        history_count--;
    }
    char **grown = realloc(history, sizeof(char *) * (history_count + 1));
    if (!grown) {
        return;   /* history is a convenience; losing an entry is not a failure */
    }
    history = grown;
    history[history_count] = strdup(line);
    if (!history[history_count]) {
        return;
    }
    history_count++;
}

void line_history_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
            buf[--n] = '\0';
        }
        line_history_add(buf);
    }
    fclose(f);
}

void line_history_save(const char *path) {
    /* 0600 BEFORE anything is written. A prompt session can contain a
     * connection string with a password in it, and a history file readable by
     * everyone on the machine is how that leaves the session. Created with the
     * mode rather than chmod'ed after, so there is no window where it is not. */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return;
    }
    FILE *f = fdopen(fd, "w");
    if (!f) {
        close(fd);
        return;
    }
    for (size_t i = 0; i < history_count; i++) {
        fprintf(f, "%s\n", history[i]);
    }
    fclose(f);
}

void line_history_free(void) {
    for (size_t i = 0; i < history_count; i++) {
        free(history[i]);
    }
    free(history);
    history = NULL;
    history_count = 0;
}

/* ---- raw mode ------------------------------------------------------------ */

static struct termios cooked;
static int raw_active = 0;

static void raw_off(void) {
    if (raw_active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &cooked);
        raw_active = 0;
    }
}

static int raw_on(void) {
    if (tcgetattr(STDIN_FILENO, &cooked) == -1) {
        return 0;
    }
    struct termios raw = cooked;
    /* ISIG OFF: while a line is being edited, Ctrl-C is a byte meaning "throw
     * this line away", not a signal. It goes back on the moment editing ends,
     * which is what lets the same key interrupt a running program. */
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return 0;
    }
    if (!raw_active) {
        /* Once. A program that dies holding raw mode leaves the terminal with
         * no echo and no line discipline, which looks to the user like their
         * shell has broken. */
        static int registered = 0;
        if (!registered) {
            atexit(raw_off);
            registered = 1;
        }
    }
    raw_active = 1;
    return 1;
}

/* ---- the buffer ---------------------------------------------------------- */

typedef struct {
    char  *buf;
    size_t len;      /* bytes in use */
    size_t cap;
    size_t pos;      /* cursor, a BYTE offset that always lands on a codepoint */
    const char *prompt;
    size_t prompt_cols;
    size_t cols;     /* terminal width */
} LineState;

static int is_continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

static size_t utf8_prev(const char *s, size_t pos) {
    if (pos == 0) {
        return 0;
    }
    pos--;
    while (pos > 0 && is_continuation((unsigned char)s[pos])) {
        pos--;
    }
    return pos;
}

static size_t utf8_next(const char *s, size_t len, size_t pos) {
    if (pos >= len) {
        return len;
    }
    pos++;
    while (pos < len && is_continuation((unsigned char)s[pos])) {
        pos++;
    }
    return pos;
}

/* Codepoints in s[0..n). The display column, on the stated assumption that a
 * codepoint occupies one column. */
static size_t utf8_cols(const char *s, size_t n) {
    size_t cols = 0;
    for (size_t i = 0; i < n; i++) {
        if (!is_continuation((unsigned char)s[i])) {
            cols++;
        }
    }
    return cols;
}

static size_t terminal_cols(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }
    return 80;
}

static void le_grow(LineState *ls, size_t need) {
    if (ls->len + need + 1 <= ls->cap) {
        return;
    }
    while (ls->len + need + 1 > ls->cap) {
        ls->cap = ls->cap ? ls->cap * 2 : 128;
    }
    char *next = realloc(ls->buf, ls->cap);
    if (!next) {
        abort();
    }
    ls->buf = next;
}

/* Repaint the single row this line occupies. Everything the editor does ends
 * here, so there is one place that can be wrong about the screen rather than
 * one per key. */
static void le_refresh(LineState *ls) {
    /* Re-read the width every time rather than once per line: a window resized
     * mid-edit would otherwise keep scrolling against the old one, and the
     * cursor would land in the wrong column for the rest of the line. One
     * ioctl per keystroke is nothing next to the write that follows it. */
    ls->cols = terminal_cols();

    size_t cursor_cols = utf8_cols(ls->buf, ls->pos);
    size_t avail = ls->cols > ls->prompt_cols + 1 ? ls->cols - ls->prompt_cols - 1 : 1;

    /* The scroll window, in ONE pass. The obvious loop -- advance `start` while
     * the cursor is off screen -- recomputes the column count from the
     * beginning each time, which is quadratic in the length of the line and
     * turns a long line into a stutter. Here the first column to show is
     * arithmetic, and the walk to it counts as it goes. */
    size_t skip_cols = cursor_cols >= avail ? cursor_cols - avail + 1 : 0;
    size_t start = 0;
    for (size_t seen = 0; seen < skip_cols && start < ls->len; seen++) {
        start = utf8_next(ls->buf, ls->len, start);
    }
    size_t end = start;
    for (size_t shown = 0; shown < avail && end < ls->len; shown++) {
        end = utf8_next(ls->buf, ls->len, end);
    }

    char seq[64];
    fputs("\r", stdout);
    fputs(ls->prompt, stdout);
    fwrite(ls->buf + start, 1, end - start, stdout);
    fputs("\033[K", stdout);                      /* erase the rest of the row */
    size_t col = ls->prompt_cols + (cursor_cols - skip_cols);
    if (col > 0) {
        snprintf(seq, sizeof(seq), "\r\033[%zuC", col);
        fputs(seq, stdout);
    } else {
        fputs("\r", stdout);
    }
    fflush(stdout);
}

static void le_insert(LineState *ls, const char *bytes, size_t n) {
    le_grow(ls, n);
    memmove(ls->buf + ls->pos + n, ls->buf + ls->pos, ls->len - ls->pos);
    memcpy(ls->buf + ls->pos, bytes, n);
    ls->len += n;
    ls->pos += n;
    ls->buf[ls->len] = '\0';
}

static void le_delete_range(LineState *ls, size_t from, size_t to) {
    if (from >= to) {
        return;
    }
    memmove(ls->buf + from, ls->buf + to, ls->len - to);
    ls->len -= (to - from);
    ls->pos = from;
    ls->buf[ls->len] = '\0';
}

static void le_set(LineState *ls, const char *text) {
    size_t n = strlen(text);
    ls->len = 0;
    le_grow(ls, n);
    memcpy(ls->buf, text, n);
    ls->len = n;
    ls->pos = n;
    ls->buf[n] = '\0';
}

/* ---- reading ------------------------------------------------------------- */

/* One byte, retried across an interrupted read: a window resize delivers
 * SIGWINCH and would otherwise look to the editor like end of input. */
static int read_byte(char *out) {
    ssize_t n;
    do {
        n = read(STDIN_FILENO, out, 1);
    } while (n < 0 && errno == EINTR);
    return n == 1;
}

/* One byte, but only if it is already there. AN ARROW KEY IS THREE BYTES THAT
 * ARRIVE TOGETHER and the Escape key is one byte that arrives alone, so the
 * only thing separating them is whether more is waiting -- and reading
 * unconditionally makes a bare Escape hang the prompt until the user presses
 * something else, which reads as a freeze. 50ms is far longer than a terminal
 * takes to deliver the rest of a sequence and far shorter than a person takes
 * to press the next key. */
static int read_byte_soon(char *out) {
    struct timeval tv = { 0, 50000 };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    int r;
    do {
        r = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    } while (r < 0 && errno == EINTR);
    if (r <= 0) {
        return 0;
    }
    return read_byte(out);
}

/* A terminal we could not put into raw mode -- and there is one that matters:
 * a pty whose attributes cannot be read. Falling back to a plain read keeps the
 * prompt WORKING without editing, where returning NULL would look to the caller
 * like end of input and close the session on its first line. */
static char *plain_line(const char *prompt) {
    if (prompt) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    size_t cap = 128, len = 0;
    char *line = malloc(cap);
    if (!line) {
        abort();
    }
    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n') {
        if (len + 2 > cap) {
            cap *= 2;
            char *next = realloc(line, cap);
            if (!next) {
                abort();
            }
            line = next;
        }
        line[len++] = (char)c;
    }
    if (c == EOF && len == 0) {
        free(line);
        return NULL;
    }
    line[len] = '\0';
    return line;
}

char *line_edit(const char *prompt) {
    if (!raw_on()) {
        return plain_line(prompt);
    }

    LineState ls;
    memset(&ls, 0, sizeof(ls));
    ls.prompt = prompt ? prompt : "";
    ls.prompt_cols = utf8_cols(ls.prompt, strlen(ls.prompt));
    le_grow(&ls, 0);
    ls.buf[0] = '\0';

    /* -1 means "editing a fresh line"; otherwise an index into history. The
     * fresh line is kept so walking up and back down returns what was typed. */
    size_t hist_index = history_count;
    char *saved = NULL;

    le_refresh(&ls);

    for (;;) {
        char c;
        if (!read_byte(&c)) {
            raw_off();
            free(saved);
            if (ls.len == 0) {
                free(ls.buf);
                return NULL;               /* end of input on an empty line */
            }
            ls.buf[ls.len] = '\0';
            return ls.buf;
        }

        switch ((unsigned char)c) {
        case 13:      /* Enter */
        case 10:
            raw_off();
            fputs("\r\n", stdout);
            fflush(stdout);
            free(saved);
            ls.buf[ls.len] = '\0';
            return ls.buf;

        case 3:       /* Ctrl-C: throw this line away, not the session */
            raw_off();
            fputs("^C\r\n", stdout);
            fflush(stdout);
            free(saved);
            ls.buf[0] = '\0';
            return ls.buf;

        case 4:       /* Ctrl-D: end of input when empty, delete-right when not */
            if (ls.len == 0) {
                raw_off();
                fputs("\r\n", stdout);
                fflush(stdout);
                free(saved);
                free(ls.buf);
                return NULL;
            }
            le_delete_range(&ls, ls.pos, utf8_next(ls.buf, ls.len, ls.pos));
            break;

        case 127:     /* Backspace */
        case 8:
            if (ls.pos > 0) {
                le_delete_range(&ls, utf8_prev(ls.buf, ls.pos), ls.pos);
            }
            break;

        case 1:  ls.pos = 0; break;                         /* Ctrl-A: home  */
        case 5:  ls.pos = ls.len; break;                    /* Ctrl-E: end   */
        case 2:  ls.pos = utf8_prev(ls.buf, ls.pos); break; /* Ctrl-B: left  */
        case 6:  ls.pos = utf8_next(ls.buf, ls.len, ls.pos); break; /* Ctrl-F */

        case 11:      /* Ctrl-K: kill to end */
            ls.len = ls.pos;
            ls.buf[ls.len] = '\0';
            break;

        case 21:      /* Ctrl-U: kill to start */
            le_delete_range(&ls, 0, ls.pos);
            break;

        case 23: {    /* Ctrl-W: delete the word before the cursor */
            size_t end = ls.pos;
            while (end > 0 && isspace((unsigned char)ls.buf[end - 1])) {
                end--;
            }
            while (end > 0 && !isspace((unsigned char)ls.buf[end - 1])) {
                end--;
            }
            le_delete_range(&ls, end, ls.pos);
            break;
        }

        case 12:      /* Ctrl-L: clear the screen, keep the line */
            fputs("\033[H\033[2J", stdout);
            break;

        case 9:       /* Tab: two spaces. There is nothing to complete against
                       * yet, and a block body at a prompt wants indenting. */
            le_insert(&ls, "  ", 2);
            break;

        case 27: {    /* an escape sequence, or the Escape key by itself */
            char a, b;
            if (!read_byte_soon(&a)) {
                break;
            }
            if (a != '[' && a != 'O') {
                break;
            }
            if (!read_byte_soon(&b)) {
                break;
            }
            if (b >= '0' && b <= '9') {
                char t;
                if (!read_byte_soon(&t)) {
                    break;
                }
                if (t == '~') {
                    if (b == '3') {   /* Delete */
                        le_delete_range(&ls, ls.pos, utf8_next(ls.buf, ls.len, ls.pos));
                    } else if (b == '1' || b == '7') {
                        ls.pos = 0;
                    } else if (b == '4' || b == '8') {
                        ls.pos = ls.len;
                    }
                }
                break;
            }
            switch (b) {
            case 'C': ls.pos = utf8_next(ls.buf, ls.len, ls.pos); break;
            case 'D': ls.pos = utf8_prev(ls.buf, ls.pos); break;
            case 'H': ls.pos = 0; break;
            case 'F': ls.pos = ls.len; break;
            case 'A':   /* Up */
            case 'B': { /* Down */
                if (history_count == 0) {
                    break;
                }
                if (hist_index == history_count) {
                    /* Leaving the line being typed: keep it, so coming back
                     * down returns what was there rather than an empty line. */
                    free(saved);
                    saved = strdup(ls.buf);
                }
                if (b == 'A') {
                    if (hist_index == 0) {
                        break;
                    }
                    hist_index--;
                } else {
                    if (hist_index >= history_count) {
                        break;
                    }
                    hist_index++;
                }
                le_set(&ls, hist_index == history_count
                            ? (saved ? saved : "")
                            : history[hist_index]);
                break;
            }
            default: break;
            }
            break;
        }

        default:
            if ((unsigned char)c >= 32) {
                le_insert(&ls, &c, 1);
            }
            break;
        }
        le_refresh(&ls);
    }
}
