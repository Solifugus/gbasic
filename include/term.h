#ifndef GBASIC_TERM_H
#define GBASIC_TERM_H

/* The terminal layer (docs/tui_design.md §2): raw mode and its restoration,
 * key and mouse decoding, a screen buffer of cells, and a redraw that writes
 * only the cells that changed. Plain C, no gBASIC values, so the parts that
 * decide what a byte means or what a frame writes are tested from C
 * (tests/term/term_test.c) as well as through a pty; src/modules/term.c is
 * the gBASIC module over it.
 *
 * The floor is an xterm-compatible terminal (§2.1). Nothing here reads
 * terminfo. */

#include <stddef.h>
#include <stdint.h>

/* ---- widths --------------------------------------------------------------
 * Cells a codepoint takes: 0, 1 or 2, or -1 for a control character, which
 * the layer never writes (it would move the cursor). The table is generated
 * (tools/make_term_widths.py) and pinned to one Unicode version. */
int term_width(uint32_t cp);
const char *term_width_unicode(void);
/* The cells UTF-8 text takes; -1 and *bad_offset at a control character or
 * invalid UTF-8. */
long term_text_width(const char *text, size_t len, size_t *bad_offset);

/* ---- input -----------------------------------------------------------------
 * One decoded event. `key` is a name ("up", "f5", "enter") or the character
 * typed ("a", "é"); `text` is what the key types, empty for keys that type
 * nothing. A paste is one event holding the pasted bytes. */
typedef enum { TERM_EV_KEY, TERM_EV_MOUSE, TERM_EV_PASTE } TermEventKind;

enum { TERM_MOD_SHIFT = 1, TERM_MOD_ALT = 2, TERM_MOD_CTRL = 4 };

typedef struct {
    TermEventKind kind;
    char key[16];
    char text[8];
    unsigned mods;
    /* mouse: 1-based column and row; button 1-3, 4/5 the wheel up/down, 0
     * for a move with nothing held; press, release or move */
    int column, row, button;
    char action[8];
    /* paste: malloc'd, owned by the event (term_event_free) */
    char *data;
    size_t data_len;
} TermEvent;

void term_event_free(TermEvent *e);

typedef struct {
    unsigned char *buf;
    size_t len, cap;
    int in_paste;
} TermDecoder;

void term_decoder_init(TermDecoder *d);
void term_decoder_free(TermDecoder *d);
void term_decoder_feed(TermDecoder *d, const unsigned char *bytes, size_t n);
/* 1 and an event; 0 when what is buffered is incomplete (or empty). With
 * `flush`, an incomplete sequence is given up on: a lone ESC is the escape
 * key, and an unfinished sequence is delivered a byte at a time as typed
 * text. The caller flushes when no byte has followed for a short while. */
int term_decoder_next(TermDecoder *d, int flush, TermEvent *out);
/* Whether bytes are held waiting for the rest of a sequence. */
int term_decoder_pending(const TermDecoder *d);

/* ---- the screen ------------------------------------------------------------
 * A colour is TERM_DEFAULT, or 0xRRGGBB. The redraw degrades it to what the
 * terminal has (term_depth). */
#define TERM_DEFAULT 0xFF000000u

enum { TERM_BOLD = 1, TERM_UNDERLINE = 2, TERM_REVERSE = 4, TERM_ITALIC = 8, TERM_DIM = 16 };

typedef struct {
    uint32_t fg, bg;
    unsigned attrs;
} TermStyle;

typedef struct {
    char ch[8];        /* one codepoint in UTF-8, NUL-terminated; "" for the
                        * right half of a wide character */
    unsigned char width;
    TermStyle style;
} TermCell;

typedef enum { TERM_DEPTH_16 = 16, TERM_DEPTH_256 = 256, TERM_DEPTH_24BIT = 24 } TermDepth;

typedef struct {
    int columns, rows;
    TermCell *want, *have;  /* what the program drew; what the terminal shows */
    int have_valid;         /* 0: the terminal's contents are unknown, redraw all */
    int cursor_column, cursor_row, cursor_visible; /* 1-based; 0 = hidden */
    TermDepth depth;
} TermScreen;

int term_screen_init(TermScreen *s, int columns, int rows, TermDepth depth);
void term_screen_free(TermScreen *s);
/* Keeps what fits; the terminal's contents become unknown. */
int term_screen_resize(TermScreen *s, int columns, int rows);
void term_screen_clear(TermScreen *s, TermStyle style);
/* Writes UTF-8 text at a 1-based cell, clipped at the right edge, and
 * answers the column after it. -1 and *bad_offset for invalid UTF-8 or a
 * control character (nothing written). A wide character that would straddle
 * the right edge is not drawn. A zero-width character joins the cell before
 * it, up to what a cell holds. */
int term_screen_put(TermScreen *s, int column, int row, const char *text, size_t len,
                    TermStyle style, size_t *bad_offset);
/* Appends to *out the bytes that make the terminal show `want`, and records
 * it as shown. */
typedef struct {
    char *p;
    size_t n, cap;
} TermBuf;
void term_buf_add(TermBuf *b, const char *p, size_t n);
void term_buf_free(TermBuf *b);
void term_screen_render(TermScreen *s, TermBuf *out);

TermDepth term_depth_from_env(const char *colorterm, const char *term);

/* ---- the terminal itself (POSIX) --------------------------------------------
 * Why a terminal cannot be used, or NULL when it can: stdin and stdout must
 * both be terminals and TERM must not be empty, "dumb" or unknown-to-be-VT. */
const char *term_unusable(void);
/* Enters raw mode, the alternate screen, and (with mouse) SGR mouse
 * reporting and bracketed paste; installs the restoring handlers for SIGINT,
 * SIGTERM, SIGHUP, SIGQUIT and atexit. 0 on success, else errno. */
int term_enter(int mouse);
/* Leaves all of that; idempotent, async-signal-safe. */
void term_leave(void);
int term_active(void);
int term_size(int *columns, int *rows);
/* Set by SIGWINCH; read and cleared. */
int term_take_resized(void);
/* While the screen is up, stderr (descriptor 2) is `held`, a temporary file,
 * and `saved` is the real one; term_stderr_replay copies what was held to it.
 * Registered here so the signal handlers replay it too. -1, -1 to forget. */
void term_stderr_hold(int saved, int held);
void term_stderr_replay(void);
/* Writes all of it to stdout, retrying on EINTR and short writes. */
int term_write(const char *p, size_t n);

#endif
