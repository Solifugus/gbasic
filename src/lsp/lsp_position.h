#ifndef GBASIC_LSP_POSITION_H
#define GBASIC_LSP_POSITION_H

/* Source-location -> LSP-position conversion for gbasic-lsp (PLAN.md Phase L).
 *
 * gBASIC source columns are 1-based BYTE offsets within a line (see the gb_span
 * comment in include/diagnostics.h): column 1 is the first byte of the line, and
 * a multi-byte UTF-8 character advances the column by its byte count. LSP
 * positions are 0-based (line, character), where `character` is counted in the
 * code units of the encoding negotiated at `initialize`.
 *
 * This module is deliberately standalone (no LSP transport, no JSON, no
 * libgbasic) so the transcode can be unit-tested in isolation. */

typedef enum {
    LSP_ENCODING_UTF8,   /* character counted in UTF-8 code units == bytes  */
    LSP_ENCODING_UTF16   /* character counted in UTF-16 code units (LSP default) */
} lsp_encoding;

typedef struct {
    int line;       /* 0-based */
    int character;  /* 0-based, in the negotiated encoding's code units */
} lsp_position;

/* Convert a 1-based (line, byte-column) source location — as carried in a
 * gb_span endpoint — to a 0-based LSP position under `encoding`.
 *
 * `source` is the full document text (NUL-terminated); it is used to locate the
 * target line and, for UTF-16, to transcode that line's leading bytes. In the
 * UTF-8 case the character is just the byte offset (column - 1) and `source` is
 * not consulted. Robust to a column beyond the line's content (clamped to the
 * line's end) and to `source` being NULL (falls back to the byte offset). */
lsp_position lsp_position_from_byte(const char *source, int line, int column,
                                    lsp_encoding encoding);

#endif
