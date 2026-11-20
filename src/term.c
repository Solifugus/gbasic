/* The terminal layer: see include/term.h and docs/tui_design.md §2. */

/* Not _POSIX_C_SOURCE alone: SIGWINCH is not POSIX, and that macro hides it
 * on glibc, macOS and the BSDs. _DEFAULT_SOURCE is glibc's POSIX-2008-and-BSD
 * set; the BSDs and macOS show everything when no strict macro is defined. */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE

#include "term.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "term_widths.h"

/* ---- widths ------------------------------------------------------------- */

static int in_ranges(const unsigned int (*r)[2], size_t n, uint32_t cp) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < r[mid][0]) {
            hi = mid;
        } else if (cp > r[mid][1]) {
            lo = mid + 1;
        } else {
            return 1;
        }
    }
    return 0;
}

int term_width(uint32_t cp) {
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) {
        return -1;
    }
    if (cp < 0x300) {
        return 1;
    }
    if (in_ranges(term_width_zero, TERM_WIDTH_ZERO_COUNT, cp)) {
        return 0;
    }
    if (in_ranges(term_width_wide, TERM_WIDTH_WIDE_COUNT, cp)) {
        return 2;
    }
    return 1;
}

const char *term_width_unicode(void) {
    return TERM_WIDTHS_UNICODE;
}

static int utf8_decode(const unsigned char *p, size_t n, uint32_t *cp);

long term_text_width(const char *text, size_t len, size_t *bad_offset) {
    const unsigned char *p = (const unsigned char *)text;
    long cells = 0;
    for (size_t i = 0; i < len;) {
        uint32_t cp;
        int n = utf8_decode(p + i, len - i, &cp);
        int w = n > 0 ? term_width(cp) : -1;
        if (w < 0) {
            if (bad_offset) {
                *bad_offset = i;
            }
            return -1;
        }
        cells += w;
        i += (size_t)n;
    }
    return cells;
}

/* One UTF-8 codepoint at p (n bytes left): its length, or 0 if invalid, or
 * -1 if it is a valid start cut short. Overlongs and surrogates are invalid. */
static int utf8_decode(const unsigned char *p, size_t n, uint32_t *cp) {
    unsigned char b = p[0];
    int len;
    uint32_t v, min;
    if (b < 0x80) {
        *cp = b;
        return 1;
    } else if ((b & 0xE0) == 0xC0) {
        len = 2; v = b & 0x1F; min = 0x80;
    } else if ((b & 0xF0) == 0xE0) {
        len = 3; v = b & 0x0F; min = 0x800;
    } else if ((b & 0xF8) == 0xF0 && b <= 0xF4) {
        len = 4; v = b & 0x07; min = 0x10000;
    } else {
        return 0;
    }
    for (int i = 1; i < len; i++) {
        if ((size_t)i >= n) {
            return -1;
        }
        if ((p[i] & 0xC0) != 0x80) {
            return 0;
        }
        v = (v << 6) | (p[i] & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v < 0xE000)) {
        return 0;
    }
    *cp = v;
    return len;
}

/* ---- input -------------------------------------------------------------- */

void term_event_free(TermEvent *e) {
    free(e->data);
    e->data = NULL;
}

void term_decoder_init(TermDecoder *d) {
    memset(d, 0, sizeof *d);
}

void term_decoder_free(TermDecoder *d) {
    free(d->buf);
    memset(d, 0, sizeof *d);
}

void term_decoder_feed(TermDecoder *d, const unsigned char *bytes, size_t n) {
    if (d->len + n > d->cap) {
        size_t cap = d->cap ? d->cap : 256;
        while (cap < d->len + n) {
            cap *= 2;
        }
        unsigned char *p = realloc(d->buf, cap);
        if (!p) {
            abort();
        }
        d->buf = p;
        d->cap = cap;
    }
    memcpy(d->buf + d->len, bytes, n);
    d->len += n;
}

int term_decoder_pending(const TermDecoder *d) {
    return d->len > 0;
}

static void consume(TermDecoder *d, size_t n) {
    memmove(d->buf, d->buf + n, d->len - n);
    d->len -= n;
}

static void ev_key(TermEvent *e, const char *key, const char *text, unsigned mods) {
    memset(e, 0, sizeof *e);
    e->kind = TERM_EV_KEY;
    snprintf(e->key, sizeof e->key, "%s", key);
    snprintf(e->text, sizeof e->text, "%s", text);
    e->mods = mods;
}

/* xterm's modifier parameter: 1 + (shift 1, alt 2, ctrl 4, meta 8). */
static unsigned xterm_mods(long m) {
    unsigned out = 0;
    if (m > 1) {
        m -= 1;
        if (m & 1) out |= TERM_MOD_SHIFT;
        if (m & 2) out |= TERM_MOD_ALT;
        if (m & 4) out |= TERM_MOD_CTRL;
        if (m & 8) out |= TERM_MOD_ALT;
    }
    return out;
}

static const char *tilde_key(long n) {
    switch (n) {
    case 1: case 7: return "home";
    case 2: return "insert";
    case 3: return "delete";
    case 4: case 8: return "end";
    case 5: return "page_up";
    case 6: return "page_down";
    case 11: return "f1";
    case 12: return "f2";
    case 13: return "f3";
    case 14: return "f4";
    case 15: return "f5";
    case 17: return "f6";
    case 18: return "f7";
    case 19: return "f8";
    case 20: return "f9";
    case 21: return "f10";
    case 23: return "f11";
    case 24: return "f12";
    default: return NULL;
    }
}

static const char *final_key(unsigned char f) {
    switch (f) {
    case 'A': return "up";
    case 'B': return "down";
    case 'C': return "right";
    case 'D': return "left";
    case 'H': return "home";
    case 'F': return "end";
    case 'P': return "f1";
    case 'Q': return "f2";
    case 'R': return "f3";
    case 'S': return "f4";
    default: return NULL;
    }
}

/* A key that is one byte or one codepoint, not an escape sequence, at the
 * front of the buffer. 1, 0 (incomplete), as term_decoder_next. */
static int plain_key(TermDecoder *d, int flush, unsigned mods, size_t skip, TermEvent *out) {
    unsigned char b = d->buf[skip];
    char name[16];
    if (b == 0x0D || b == 0x0A) {
        ev_key(out, "enter", "", mods);
    } else if (b == 0x09) {
        ev_key(out, "tab", "", mods);
    } else if (b == 0x7F || b == 0x08) {
        ev_key(out, "backspace", "", mods);
    } else if (b == 0x00) {
        ev_key(out, "space", "", mods | TERM_MOD_CTRL);
    } else if (b < 0x1B) {
        name[0] = (char)('a' + b - 1);
        name[1] = 0;
        ev_key(out, name, "", mods | TERM_MOD_CTRL);
    } else if (b < 0x20) {
        static const char *const rest[] = { "escape", "\\", "]", "^", "_" };
        ev_key(out, rest[b - 0x1B], "", b == 0x1B ? mods : mods | TERM_MOD_CTRL);
    } else if (b == ' ') {
        ev_key(out, "space", mods & TERM_MOD_ALT ? "" : " ", mods);
    } else {
        uint32_t cp;
        int len = utf8_decode(d->buf + skip, d->len - skip, &cp);
        if (len < 0 && !flush) {
            return 0;
        }
        if (len <= 0) {
            /* invalid, or cut short and given up on: the replacement
             * character, one byte consumed */
            ev_key(out, "\xEF\xBF\xBD", "\xEF\xBF\xBD", mods);
            consume(d, skip + 1);
            return 1;
        }
        memcpy(name, d->buf + skip, (size_t)len);
        name[len] = 0;
        ev_key(out, name, mods & TERM_MOD_ALT ? "" : name, mods);
        consume(d, skip + (size_t)len);
        return 1;
    }
    consume(d, skip + 1);
    return 1;
}

static long param_at(const char *params, int index) {
    const char *p = params;
    for (int i = 0; i < index; i++) {
        p = strchr(p, ';');
        if (!p) {
            return -1;
        }
        p++;
    }
    if (*p < '0' || *p > '9') {
        return -1;
    }
    return strtol(p, NULL, 10);
}

int term_decoder_next(TermDecoder *d, int flush, TermEvent *out) {
    for (;;) {
        if (d->len == 0) {
            return 0;
        }
        if (d->in_paste) {
            static const char end[] = "\x1b[201~";
            for (size_t i = 0; i + 6 <= d->len; i++) {
                if (memcmp(d->buf + i, end, 6) == 0) {
                    memset(out, 0, sizeof *out);
                    out->kind = TERM_EV_PASTE;
                    out->data = malloc(i + 1);
                    if (!out->data) {
                        abort();
                    }
                    memcpy(out->data, d->buf, i);
                    out->data[i] = 0;
                    out->data_len = i;
                    consume(d, i + 6);
                    d->in_paste = 0;
                    return 1;
                }
            }
            return 0;   /* a paste ends only at its end marker, flush or not */
        }
        if (d->buf[0] != 0x1B) {
            return plain_key(d, flush, 0, 0, out);
        }
        if (d->len == 1) {
            if (!flush) {
                return 0;
            }
            ev_key(out, "escape", "", 0);
            consume(d, 1);
            return 1;
        }
        unsigned char b1 = d->buf[1];
        if (b1 == '[') {
            size_t i = 2;
            while (i < d->len && d->buf[i] >= 0x20 && d->buf[i] <= 0x3F) {
                i++;
            }
            if (i >= d->len) {
                if (!flush) {
                    return 0;
                }
                ev_key(out, "escape", "", 0);
                consume(d, 1);
                return 1;
            }
            unsigned char f = d->buf[i];
            if (f < 0x40 || f > 0x7E) {
                /* not a CSI after all: ESC is the key, the rest is typed */
                ev_key(out, "escape", "", 0);
                consume(d, 1);
                return 1;
            }
            char params[64];
            size_t pn = i - 2 < sizeof params - 1 ? i - 2 : sizeof params - 1;
            memcpy(params, d->buf + 2, pn);
            params[pn] = 0;
            size_t whole = i + 1;
            if (params[0] == '<' && (f == 'M' || f == 'm')) {
                long b = param_at(params + 1, 0), x = param_at(params + 1, 1), y = param_at(params + 1, 2);
                consume(d, whole);
                if (b < 0 || x < 1 || y < 1) {
                    continue;
                }
                memset(out, 0, sizeof *out);
                out->kind = TERM_EV_MOUSE;
                out->column = (int)x;
                out->row = (int)y;
                if (b & 4) out->mods |= TERM_MOD_SHIFT;
                if (b & 8) out->mods |= TERM_MOD_ALT;
                if (b & 16) out->mods |= TERM_MOD_CTRL;
                if (b & 64) {
                    out->button = (b & 1) ? 5 : 4;
                    snprintf(out->action, sizeof out->action, "wheel");
                } else if (b & 32) {
                    out->button = (b & 3) == 3 ? 0 : (int)(b & 3) + 1;
                    snprintf(out->action, sizeof out->action, "move");
                } else {
                    out->button = (int)(b & 3) + 1;
                    snprintf(out->action, sizeof out->action, f == 'M' ? "press" : "release");
                }
                return 1;
            }
            if (f == '~' && strcmp(params, "200") == 0) {
                consume(d, whole);
                d->in_paste = 1;
                continue;
            }
            const char *key = NULL;
            unsigned mods = 0;
            if (f == '~') {
                key = tilde_key(param_at(params, 0));
                mods = xterm_mods(param_at(params, 1));
            } else if (f == 'Z') {
                key = "tab";
                mods = TERM_MOD_SHIFT;
            } else if (params[0] != '<' && params[0] != '?' && params[0] != '>') {
                key = final_key(f);
                mods = xterm_mods(param_at(params, 1));
            }
            consume(d, whole);
            if (!key) {
                continue;   /* a sequence this layer has no name for: dropped */
            }
            ev_key(out, key, "", mods);
            return 1;
        }
        if (b1 == 'O') {
            if (d->len < 3) {
                if (!flush) {
                    return 0;
                }
                ev_key(out, "escape", "", 0);
                consume(d, 1);
                return 1;
            }
            const char *key = final_key(d->buf[2]);
            if (!key) {
                /* ESC O then something else: alt+O, then the rest */
                return plain_key(d, flush, TERM_MOD_ALT, 1, out);
            }
            ev_key(out, key, "", 0);
            consume(d, 3);
            return 1;
        }
        if (b1 == 0x1B) {
            ev_key(out, "escape", "", 0);
            consume(d, 1);
            return 1;
        }
        /* ESC and a key: that key with alt, as xterm sends meta */
        int got = plain_key(d, flush, TERM_MOD_ALT, 1, out);
        if (got == 0 && flush) {
            ev_key(out, "escape", "", 0);
            consume(d, 1);
            return 1;
        }
        return got;
    }
}

/* ---- the screen ------------------------------------------------------------ */

static void blank(TermCell *c, TermStyle style) {
    memset(c, 0, sizeof *c);
    c->ch[0] = ' ';
    c->width = 1;
    c->style = style;
}

static TermStyle default_style(void) {
    TermStyle s = { TERM_DEFAULT, TERM_DEFAULT, 0 };
    return s;
}

int term_screen_init(TermScreen *s, int columns, int rows, TermDepth depth) {
    memset(s, 0, sizeof *s);
    s->depth = depth;
    return term_screen_resize(s, columns, rows);
}

void term_screen_free(TermScreen *s) {
    free(s->want);
    free(s->have);
    memset(s, 0, sizeof *s);
}

int term_screen_resize(TermScreen *s, int columns, int rows) {
    if (columns < 1 || rows < 1 || columns > 10000 || rows > 10000) {
        return -1;
    }
    size_t n = (size_t)columns * (size_t)rows;
    TermCell *want = malloc(n * sizeof *want), *have = malloc(n * sizeof *have);
    if (!want || !have) {
        abort();
    }
    for (size_t i = 0; i < n; i++) {
        blank(&want[i], default_style());
        blank(&have[i], default_style());
    }
    for (int r = 0; r < rows && r < s->rows; r++) {
        for (int c = 0; c < columns && c < s->columns; c++) {
            want[(size_t)r * columns + c] = s->want[(size_t)r * s->columns + c];
        }
        /* a wide character cut in half by the new right edge */
        TermCell *last = &want[(size_t)r * columns + columns - 1];
        if (last->width == 2) {
            blank(last, last->style);
        }
    }
    free(s->want);
    free(s->have);
    s->want = want;
    s->have = have;
    s->columns = columns;
    s->rows = rows;
    s->have_valid = 0;
    return 0;
}

void term_screen_clear(TermScreen *s, TermStyle style) {
    for (size_t i = 0; i < (size_t)s->columns * s->rows; i++) {
        blank(&s->want[i], style);
    }
}

/* Clears the wide pair a cell belongs to, so overwriting half of a wide
 * character never leaves the other half drawn. */
static void unpair(TermScreen *s, int r, int c) {
    TermCell *row = &s->want[(size_t)r * s->columns];
    if (row[c].width == 0 && row[c].ch[0] == 0 && c > 0) {
        blank(&row[c - 1], row[c - 1].style);
    } else if (row[c].width == 2 && c + 1 < s->columns) {
        blank(&row[c + 1], row[c + 1].style);
    }
}

int term_screen_put(TermScreen *s, int column, int row, const char *text, size_t len,
                    TermStyle style, size_t *bad_offset) {
    const unsigned char *p = (const unsigned char *)text;
    /* refuse first, so a refusal writes nothing */
    for (size_t i = 0; i < len;) {
        uint32_t cp;
        int n = utf8_decode(p + i, len - i, &cp);
        if (n <= 0 || term_width(cp) < 0) {
            if (bad_offset) {
                *bad_offset = i;
            }
            return -1;
        }
        i += (size_t)n;
    }
    if (row < 1 || row > s->rows) {
        return column;
    }
    int r = row - 1, c = column - 1;
    TermCell *line = &s->want[(size_t)r * s->columns];
    int last = -1;  /* the cell a zero-width character joins */
    for (size_t i = 0; i < len;) {
        uint32_t cp;
        int n = utf8_decode(p + i, len - i, &cp);
        int w = term_width(cp);
        if (w == 0) {
            if (last >= 0) {
                size_t have = strlen(line[last].ch);
                if (have + (size_t)n < sizeof line[last].ch) {
                    memcpy(line[last].ch + have, p + i, (size_t)n);
                    line[last].ch[have + n] = 0;
                }
            }
            i += (size_t)n;
            continue;
        }
        if (c + w > s->columns) {
            break;
        }
        if (c >= 0) {
            unpair(s, r, c);
            if (w == 2) {
                unpair(s, r, c + 1);
            }
            memset(&line[c], 0, sizeof line[c]);
            memcpy(line[c].ch, p + i, (size_t)n);
            line[c].width = (unsigned char)w;
            line[c].style = style;
            if (w == 2) {
                memset(&line[c + 1], 0, sizeof line[c + 1]);
                line[c + 1].style = style;
            }
            last = c;
        } else if (c + w > 0) {
            /* a wide character half off the left edge: its visible half blank */
            blank(&line[0], style);
        }
        c += w;
        i += (size_t)n;
    }
    return c + 1;
}

void term_buf_add(TermBuf *b, const char *p, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->n + n + 1) {
            cap *= 2;
        }
        char *q = realloc(b->p, cap);
        if (!q) {
            abort();
        }
        b->p = q;
        b->cap = cap;
    }
    memcpy(b->p + b->n, p, n);
    b->n += n;
    b->p[b->n] = 0;
}

void term_buf_free(TermBuf *b) {
    free(b->p);
    memset(b, 0, sizeof *b);
}

static void buf_printf(TermBuf *b, const char *fmt, int a, int c) {
    char tmp[48];
    int n = snprintf(tmp, sizeof tmp, fmt, a, c);
    term_buf_add(b, tmp, (size_t)n);
}

/* xterm's 16 colours, the values its default palette gives them */
static const uint32_t ansi16[16] = {
    0x000000, 0xCD0000, 0x00CD00, 0xCDCD00, 0x0000EE, 0xCD00CD, 0x00CDCD, 0xE5E5E5,
    0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00, 0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
};

static long dist2(uint32_t a, uint32_t b) {
    long dr = (long)((a >> 16) & 255) - (long)((b >> 16) & 255);
    long dg = (long)((a >> 8) & 255) - (long)((b >> 8) & 255);
    long db = (long)(a & 255) - (long)(b & 255);
    return dr * dr + dg * dg + db * db;
}

static int nearest16(uint32_t rgb) {
    int best = 0;
    for (int i = 1; i < 16; i++) {
        if (dist2(rgb, ansi16[i]) < dist2(rgb, ansi16[best])) {
            best = i;
        }
    }
    return best;
}

/* The xterm 256-colour palette: 16 + a 6x6x6 cube + 24 greys. */
static uint32_t xterm256(int i) {
    static const int level[6] = { 0, 95, 135, 175, 215, 255 };
    if (i < 16) {
        return ansi16[i];
    }
    if (i < 232) {
        i -= 16;
        return ((uint32_t)level[i / 36] << 16) | ((uint32_t)level[(i / 6) % 6] << 8) | (uint32_t)level[i % 6];
    }
    int g = 8 + (i - 232) * 10;
    return ((uint32_t)g << 16) | ((uint32_t)g << 8) | (uint32_t)g;
}

static int nearest256(uint32_t rgb) {
    int best = 16;
    for (int i = 16; i < 256; i++) {
        if (dist2(rgb, xterm256(i)) < dist2(rgb, xterm256(best))) {
            best = i;
        }
    }
    return best;
}

static void colour(TermBuf *b, uint32_t rgb, int background, TermDepth depth) {
    if (rgb == TERM_DEFAULT) {
        term_buf_add(b, background ? ";49" : ";39", 3);
    } else if (depth == TERM_DEPTH_24BIT) {
        char tmp[32];
        int n = snprintf(tmp, sizeof tmp, ";%d;2;%u;%u;%u", background ? 48 : 38,
                         (unsigned)((rgb >> 16) & 255), (unsigned)((rgb >> 8) & 255), (unsigned)(rgb & 255));
        term_buf_add(b, tmp, (size_t)n);
    } else if (depth == TERM_DEPTH_256) {
        buf_printf(b, ";%d;5;%d", background ? 48 : 38, nearest256(rgb));
    } else {
        int i = nearest16(rgb);
        buf_printf(b, ";%d", (i < 8 ? (background ? 40 : 30) : (background ? 100 : 90)) + i % 8, 0);
    }
}

static void style_sgr(TermBuf *b, TermStyle st, TermDepth depth) {
    term_buf_add(b, "\x1b[0", 3);
    if (st.attrs & TERM_BOLD) term_buf_add(b, ";1", 2);
    if (st.attrs & TERM_DIM) term_buf_add(b, ";2", 2);
    if (st.attrs & TERM_ITALIC) term_buf_add(b, ";3", 2);
    if (st.attrs & TERM_UNDERLINE) term_buf_add(b, ";4", 2);
    if (st.attrs & TERM_REVERSE) term_buf_add(b, ";7", 2);
    if (st.fg != TERM_DEFAULT) colour(b, st.fg, 0, depth);
    if (st.bg != TERM_DEFAULT) colour(b, st.bg, 1, depth);
    term_buf_add(b, "m", 1);
}

static int same_style(TermStyle a, TermStyle b) {
    return a.fg == b.fg && a.bg == b.bg && a.attrs == b.attrs;
}

static int same_cell(const TermCell *a, const TermCell *b) {
    return a->width == b->width && same_style(a->style, b->style) && strcmp(a->ch, b->ch) == 0;
}

void term_screen_render(TermScreen *s, TermBuf *out) {
    term_buf_add(out, "\x1b[?25l", 6);
    TermStyle cur = default_style();
    term_buf_add(out, "\x1b[0m", 4);
    if (!s->have_valid) {
        term_buf_add(out, "\x1b[2J", 4);
        for (size_t i = 0; i < (size_t)s->columns * s->rows; i++) {
            blank(&s->have[i], default_style());
        }
        s->have_valid = 1;
    }
    int at_r = -1, at_c = -1;   /* where the terminal's cursor is, 0-based; -1 unknown */
    for (int r = 0; r < s->rows; r++) {
        TermCell *want = &s->want[(size_t)r * s->columns];
        TermCell *have = &s->have[(size_t)r * s->columns];
        for (int c = 0; c < s->columns; c++) {
            int w = want[c].width == 2 && c + 1 < s->columns ? 2 : 1;
            if (want[c].width == 0) {
                continue;   /* the right half of a wide character: drawn with its left */
            }
            int changed = !same_cell(&want[c], &have[c]) || (w == 2 && !same_cell(&want[c + 1], &have[c + 1]));
            if (!changed) {
                continue;
            }
            if (at_r != r || at_c != c) {
                buf_printf(out, "\x1b[%d;%dH", r + 1, c + 1);
            }
            if (!same_style(cur, want[c].style)) {
                style_sgr(out, want[c].style, s->depth);
                cur = want[c].style;
            }
            term_buf_add(out, want[c].ch, strlen(want[c].ch));
            have[c] = want[c];
            if (w == 2) {
                have[c + 1] = want[c + 1];
            } else if (have[c].width == 2) {
                have[c].width = 1;
            }
            at_r = r;
            at_c = c + w;
            if (at_c >= s->columns) {
                at_r = -1;  /* a write in the last column leaves the cursor in xterm's pending wrap */
            }
        }
    }
    term_buf_add(out, "\x1b[0m", 4);
    if (s->cursor_visible && s->cursor_row >= 1 && s->cursor_column >= 1) {
        buf_printf(out, "\x1b[%d;%dH", s->cursor_row, s->cursor_column);
        term_buf_add(out, "\x1b[?25h", 6);
    }
}

TermDepth term_depth_from_env(const char *colorterm, const char *term) {
    if (colorterm && (strcasecmp(colorterm, "truecolor") == 0 || strcasecmp(colorterm, "24bit") == 0)) {
        return TERM_DEPTH_24BIT;
    }
    if (term && (strstr(term, "256color") || strstr(term, "direct"))) {
        return strstr(term, "direct") ? TERM_DEPTH_24BIT : TERM_DEPTH_256;
    }
    return TERM_DEPTH_16;
}

/* ---- the terminal itself ---------------------------------------------------- */

static struct termios saved_termios;
static volatile sig_atomic_t entered = 0;
/* The process that entered. A forked child must never restore a terminal its
 * parent is still drawing on. DEFENCE IN DEPTH, NOT REACHED TODAY, measured
 * 2026-11-19: an actor resets its signal handlers to the defaults
 * (/proc/PID/status), and leaves without running atexit, so removing either
 * guard left tests/run_term.sh's actor ending green. A fork that kept them
 * would be the first to need this. */
static pid_t entered_by = 0;
static int stderr_saved = -1, stderr_held = -1;
static volatile sig_atomic_t resized = 0;
static int with_mouse = 0;
static struct sigaction old_int, old_term, old_hup, old_quit, old_winch, old_tstp, old_cont, old_abrt;
static int handlers_installed = 0;
static int atexit_installed = 0;

static const char enter_seq[] = "\x1b[?1049h\x1b[?25l\x1b[2J";
static const char mouse_on_seq[] = "\x1b[?1000h\x1b[?1002h\x1b[?1006h\x1b[?2004h";
static const char mouse_off_seq[] = "\x1b[?1006l\x1b[?1002l\x1b[?1000l\x1b[?2004l";
static const char leave_seq[] = "\x1b[0m\x1b[?25h\x1b[?1049l";

const char *term_unusable(void) {
    if (!isatty(STDIN_FILENO)) {
        return "standard input is not a terminal";
    }
    if (!isatty(STDOUT_FILENO)) {
        return "standard output is not a terminal";
    }
    const char *t = getenv("TERM");
    if (!t || !*t) {
        return "TERM is not set, so the terminal's kind is unknown";
    }
    if (strcmp(t, "dumb") == 0) {
        return "TERM is dumb: the terminal cannot position the cursor";
    }
    return NULL;
}

int term_write(const char *p, size_t n) {
    while (n > 0) {
        ssize_t w = write(STDOUT_FILENO, p, n);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

void term_stderr_hold(int saved, int held) {
    stderr_saved = saved;
    stderr_held = held;
}

/* Async-signal-safe: lseek, read and write. */
void term_stderr_replay(void) {
    if (stderr_saved < 0 || stderr_held < 0) {
        return;
    }
    char buf[4096];
    if (lseek(stderr_held, 0, SEEK_SET) < 0) {
        return;
    }
    for (;;) {
        ssize_t n = read(stderr_held, buf, sizeof buf);
        if (n <= 0) {
            break;
        }
        for (ssize_t off = 0; off < n;) {
            ssize_t w = write(stderr_saved, buf + off, (size_t)(n - off));
            if (w <= 0) {
                return;
            }
            off += w;
        }
    }
    /* replayed once: what is written from here goes after it */
    if (ftruncate(stderr_held, 0) == 0) {
        lseek(stderr_held, 0, SEEK_SET);
    }
}

/* The restoring half, async-signal-safe: write(2) and tcsetattr only. */
static void restore_now(void) {
    if (with_mouse) {
        (void)!write(STDOUT_FILENO, mouse_off_seq, sizeof mouse_off_seq - 1);
    }
    (void)!write(STDOUT_FILENO, leave_seq, sizeof leave_seq - 1);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
}

static int make_raw(void) {
    struct termios raw = saved_termios;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cflag |= CS8;
    /* ISIG stays: Ctrl-C still interrupts, and a handler restores first */
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void put_back(int sig) {
    struct sigaction *old = sig == SIGINT ? &old_int : sig == SIGTERM ? &old_term : sig == SIGHUP ? &old_hup
                          : sig == SIGQUIT ? &old_quit : &old_abrt;
    sigaction(sig, old, NULL);
}

static void on_fatal(int sig) {
    if (entered && getpid() == entered_by) {
        restore_now();
        entered = 0;
        if (stderr_saved >= 0) {
            dup2(stderr_saved, 2);
            term_stderr_replay();
        }
    }
    /* the signal then does what it did before: by default it ends the
     * program; a handler gBASIC had installed runs as it would have */
    put_back(sig);
    raise(sig);
}

static void on_winch(int sig) {
    (void)sig;
    resized = 1;
}

static void on_tstp(int sig) {
    (void)sig;
    if (entered && getpid() == entered_by) {
        restore_now();
    }
    signal(SIGTSTP, SIG_DFL);
    raise(SIGTSTP);
}

static void on_cont(int sig) {
    (void)sig;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_tstp;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTSTP, &sa, NULL);
    if (entered && getpid() == entered_by) {
        make_raw();
        (void)!write(STDOUT_FILENO, enter_seq, sizeof enter_seq - 1);
        if (with_mouse) {
            (void)!write(STDOUT_FILENO, mouse_on_seq, sizeof mouse_on_seq - 1);
        }
        resized = 1;    /* the screen was the shell's meanwhile: redraw it all */
    }
}

static void at_exit_restore(void) {
    term_leave();
}

static void install(int sig, void (*fn)(int), struct sigaction *old) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = sig == SIGWINCH ? SA_RESTART : 0;
    sigaction(sig, &sa, old);
}

int term_enter(int mouse) {
    if (entered) {
        return 0;
    }
    if (tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
        return errno;
    }
    with_mouse = mouse;
    if (!handlers_installed) {
        install(SIGINT, on_fatal, &old_int);
        install(SIGTERM, on_fatal, &old_term);
        install(SIGHUP, on_fatal, &old_hup);
        install(SIGQUIT, on_fatal, &old_quit);
        install(SIGABRT, on_fatal, &old_abrt);
        install(SIGWINCH, on_winch, &old_winch);
        install(SIGTSTP, on_tstp, &old_tstp);
        install(SIGCONT, on_cont, &old_cont);
        handlers_installed = 1;
    }
    if (!atexit_installed) {
        atexit(at_exit_restore);
        atexit_installed = 1;
    }
    if (make_raw() != 0) {
        return errno;
    }
    entered = 1;
    entered_by = getpid();
    resized = 0;
    term_write(enter_seq, sizeof enter_seq - 1);
    if (mouse) {
        term_write(mouse_on_seq, sizeof mouse_on_seq - 1);
    }
    return 0;
}

void term_leave(void) {
    if (!entered || getpid() != entered_by) {
        return;
    }
    restore_now();
    entered = 0;
    if (handlers_installed) {
        sigaction(SIGINT, &old_int, NULL);
        sigaction(SIGTERM, &old_term, NULL);
        sigaction(SIGHUP, &old_hup, NULL);
        sigaction(SIGQUIT, &old_quit, NULL);
        sigaction(SIGABRT, &old_abrt, NULL);
        sigaction(SIGWINCH, &old_winch, NULL);
        sigaction(SIGTSTP, &old_tstp, NULL);
        sigaction(SIGCONT, &old_cont, NULL);
        handlers_installed = 0;
    }
}

int term_active(void) {
    return entered && getpid() == entered_by;
}

int term_size(int *columns, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0 || ws.ws_row == 0) {
        return -1;
    }
    *columns = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

int term_take_resized(void) {
    int r = resized;
    resized = 0;
    return r;
}
