#include "lsp_position.h"

/* Count UTF-16 code units in the first `nbytes` bytes of `line`.
 *
 * We never need the scalar value, only the unit count: a UTF-8 sequence of
 * length 1/2/3 encodes a BMP scalar (1 UTF-16 unit); a length-4 sequence encodes
 * an astral scalar (a surrogate pair -> 2 units). So the lead byte's implied
 * length is all we read. A stray continuation byte or invalid lead counts as one
 * unit and advances one byte (defensive; well-formed UTF-8 never hits this). If a
 * sequence would run past the byte window (a column landing mid-character — which
 * does not happen for token-aligned spans, but we stay robust), it is clamped and
 * the remainder counted as single units. */
static int utf16_units(const char *line, int nbytes) {
    int units = 0;
    int i = 0;
    while (i < nbytes) {
        unsigned char c = (unsigned char)line[i];
        int len;
        if (c < 0x80) {
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        } else {
            len = 1; /* stray continuation / invalid lead */
        }
        if (i + len > nbytes) {
            len = 1; /* clamp to the window; treat the leftover byte as one unit */
        }
        units += (len == 4) ? 2 : 1;
        i += len;
    }
    return units;
}

lsp_position lsp_position_from_byte(const char *source, int line, int column,
                                    lsp_encoding encoding) {
    lsp_position pos;
    pos.line = (line > 0) ? line - 1 : 0;

    /* Byte offset within the line (0-based). column is 1-based bytes. */
    int col0 = (column > 0) ? column - 1 : 0;

    /* UTF-8: LSP characters are bytes, so the offset is the answer. No need to
     * consult the source. Same trivial answer when there is nothing to transcode
     * or no source to transcode against. */
    if (encoding == LSP_ENCODING_UTF8 || col0 == 0 || source == 0) {
        pos.character = col0;
        return pos;
    }

    /* Locate the start of the 1-based target line (count '\n', matching the
     * lexer's line model). */
    const char *p = source;
    int cur = 1;
    while (*p && cur < line) {
        if (*p == '\n') {
            cur++;
        }
        p++;
    }

    /* Read up to col0 bytes of this line, stopping at the line end so a column
     * past the content clamps cleanly. */
    int avail = 0;
    while (avail < col0 && p[avail] != '\0' && p[avail] != '\n') {
        avail++;
    }

    pos.character = utf16_units(p, avail);
    return pos;
}
