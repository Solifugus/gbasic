/* Delimited-file reading (docs/csv_design.md §3).
 *
 * ONE STATE MACHINE OVER A BYTE STREAM, for a file and for text alike: a file
 * is read through a buffer that holds what has not been consumed, and text is
 * that buffer already full. So memory is the record in hand, never the file,
 * which is what lets `for each item in csv.records(f)` stream a file of
 * millions of records as `for each line in f` streams lines.
 *
 * NOT built on the line reader. A quoted field holds line breaks, so a record
 * is not a line, and splitting into lines first is the defect this replaces:
 * frame.read_csv made two records out of one at every quoted line break.
 *
 * A FIELD IS COUNTED BYTES, gathered into one buffer for the whole record with
 * an offset per field, so an interior NUL is content and a record of many
 * fields costs two allocations that are reused, not one per field. */

#include "gb_csv.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GB_CSV_CHUNK 65536

struct GbCsvReader {
    FILE *fp;                 /* NULL when reading text */
    const unsigned char *in;  /* the bytes in hand */
    size_t in_len;
    size_t in_pos;
    unsigned char *own;       /* the file's buffer */
    int at_eof;
    int failed;               /* a read error */

    char delimiter;
    int quote;

    uint64_t consumed;        /* bytes taken from the input so far */
    uint64_t line;            /* the line the next byte is on, from 1 */
    int started;              /* past the byte order mark check */

    char *field_buf;          /* this record's fields, end to end */
    size_t field_len;
    size_t field_cap;
    size_t *starts;           /* where each field begins in field_buf */
    size_t nfields;
    size_t fields_cap;

    uint64_t rec_line;
    uint64_t rec_offset;
    GbCsvProblem problem;
    char error[256];
};

static GbCsvReader *reader_new(char delimiter, int quote) {
    GbCsvReader *r = calloc(1, sizeof *r);
    if (!r) {
        abort();
    }
    r->delimiter = delimiter;
    r->quote = quote;
    r->line = 1;
    return r;
}

GbCsvReader *gb_csv_open_file(const char *path, char delimiter, int quote) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    GbCsvReader *r = reader_new(delimiter, quote);
    r->fp = fp;
    r->own = malloc(GB_CSV_CHUNK);
    if (!r->own) {
        abort();
    }
    r->in = r->own;
    return r;
}

GbCsvReader *gb_csv_open_text(const char *text, size_t length, char delimiter, int quote) {
    GbCsvReader *r = reader_new(delimiter, quote);
    r->in = (const unsigned char *)text;
    r->in_len = length;
    r->at_eof = 1;
    return r;
}

void gb_csv_close(GbCsvReader *r) {
    if (!r) {
        return;
    }
    if (r->fp) {
        fclose(r->fp);
    }
    free(r->own);
    free(r->field_buf);
    free(r->starts);
    free(r);
}

/* Refill when the buffer is spent. Consumed bytes are never needed again --
 * a field is copied out as it is read -- so a refill overwrites from the top
 * and there is nothing to compact. */
static int fill(GbCsvReader *r) {
    if (r->at_eof) {
        return 0;
    }
    size_t got = fread(r->own, 1, GB_CSV_CHUNK, r->fp);
    r->in_len = got;
    r->in_pos = 0;
    if (got == 0) {
        if (ferror(r->fp)) {
            r->failed = 1;
        }
        r->at_eof = 1;
        return 0;
    }
    return 1;
}

/* The next byte without taking it, or -1 at end of input. */
static int peek(GbCsvReader *r) {
    if (r->in_pos >= r->in_len && !fill(r)) {
        return -1;
    }
    return r->in[r->in_pos];
}

static void take(GbCsvReader *r) {
    r->in_pos++;
    r->consumed++;
}

static int put(GbCsvReader *r, unsigned char c) {
    if (r->field_len == r->field_cap) {
        if (r->field_cap >= GB_CSV_MAX_RECORD) {
            return 0;
        }
        size_t next = r->field_cap ? r->field_cap * 2 : 256;
        char *grown = realloc(r->field_buf, next);
        if (!grown) {
            abort();
        }
        r->field_buf = grown;
        r->field_cap = next;
    }
    r->field_buf[r->field_len++] = (char)c;
    return 1;
}

static void begin_field(GbCsvReader *r) {
    if (r->nfields == r->fields_cap) {
        size_t next = r->fields_cap ? r->fields_cap * 2 : 16;
        size_t *grown = realloc(r->starts, next * sizeof *grown);
        if (!grown) {
            abort();
        }
        r->starts = grown;
        r->fields_cap = next;
    }
    r->starts[r->nfields++] = r->field_len;
}

/* Take a line break that `c` begins: `\r\n`, `\n` or a lone `\r`, each one line. */
static void take_break(GbCsvReader *r, int c) {
    take(r);
    if (c == '\r' && peek(r) == '\n') {
        take(r);
    }
    r->line++;
}

static int too_long(GbCsvReader *r) {
    snprintf(r->error, sizeof r->error,
             "the record starting on line %llu is over %u MiB; a quote that never closes?",
             (unsigned long long)r->rec_line, GB_CSV_MAX_RECORD / (1024u * 1024u));
    return -1;
}

static int read_failed(GbCsvReader *r) {
    snprintf(r->error, sizeof r->error, "could not read the file (%s)", strerror(errno));
    return -1;
}

int gb_csv_next(GbCsvReader *r) {
    if (!r->started) {
        r->started = 1;
        /* A UTF-8 byte order mark is not part of the first field. Looked for
         * byte by byte, so a file that is only "\xEF" is one field of it. */
        if (peek(r) == 0xEF) {
            size_t save = r->in_pos;
            if (r->in_len - r->in_pos >= 3 && r->in[save + 1] == 0xBB && r->in[save + 2] == 0xBF) {
                take(r);
                take(r);
                take(r);
            }
        }
    }
    /* A blank line is skipped and still counted. */
    int c = peek(r);
    while (c == '\r' || c == '\n') {
        take_break(r, c);
        c = peek(r);
    }
    if (r->failed) {
        return read_failed(r);
    }
    if (c < 0) {
        return 0;
    }

    r->field_len = 0;
    r->nfields = 0;
    r->problem = GB_CSV_OK;
    r->rec_line = r->line;
    r->rec_offset = r->consumed;

    for (;;) {
        begin_field(r);
        c = peek(r);
        if (r->quote >= 0 && c == r->quote) {
            uint64_t opened = r->line;
            take(r);
            for (;;) {
                c = peek(r);
                if (c < 0) {
                    if (r->failed) {
                        return read_failed(r);
                    }
                    snprintf(r->error, sizeof r->error,
                             "the quote opened on line %llu is never closed",
                             (unsigned long long)opened);
                    return -1;
                }
                take(r);
                if (c == r->quote) {
                    if (peek(r) == r->quote) {
                        take(r);
                        if (!put(r, (unsigned char)c)) {
                            return too_long(r);
                        }
                        continue;
                    }
                    break;
                }
                /* A line break inside quotes is content, kept as written, and
                 * still a line for the next record's number. */
                if (c == '\n') {
                    r->line++;
                } else if (c == '\r') {
                    if (peek(r) == '\n') {
                        if (!put(r, '\r')) {
                            return too_long(r);
                        }
                        take(r);
                        c = '\n';
                    }
                    r->line++;
                }
                if (!put(r, (unsigned char)c)) {
                    return too_long(r);
                }
            }
            c = peek(r);
            if (c >= 0 && c != r->delimiter && c != '\r' && c != '\n' && r->problem == GB_CSV_OK) {
                r->problem = GB_CSV_TEXT_AFTER_QUOTE;
            }
        }
        /* Unquoted text, or what follows a closing quote: up to the delimiter,
         * a line break or the end. A quote here is just a byte (`5" pipe`). */
        for (;;) {
            c = peek(r);
            if (c < 0 || c == r->delimiter || c == '\r' || c == '\n') {
                break;
            }
            take(r);
            if (!put(r, (unsigned char)c)) {
                return too_long(r);
            }
        }
        if (r->failed) {
            return read_failed(r);
        }
        if (c == r->delimiter) {
            take(r);
            continue;
        }
        if (c == '\r' || c == '\n') {
            take_break(r, c);
        }
        return 1;
    }
}

size_t gb_csv_field_count(const GbCsvReader *r) {
    return r->nfields;
}

const char *gb_csv_field(const GbCsvReader *r, size_t index, size_t *length) {
    size_t start = r->starts[index];
    size_t end = index + 1 < r->nfields ? r->starts[index + 1] : r->field_len;
    *length = end - start;
    return r->field_buf ? r->field_buf + start : "";
}

uint64_t gb_csv_line(const GbCsvReader *r) {
    return r->rec_line;
}

uint64_t gb_csv_offset(const GbCsvReader *r) {
    return r->rec_offset;
}

GbCsvProblem gb_csv_problem(const GbCsvReader *r) {
    return r->problem;
}

const char *gb_csv_error(const GbCsvReader *r) {
    return r->error;
}

int gb_csv_needs_quote(const char *field, size_t length, char delimiter, int quote) {
    for (size_t i = 0; i < length; i++) {
        char c = field[i];
        if (c == delimiter || c == '\r' || c == '\n' || (quote >= 0 && (unsigned char)c == (unsigned)quote)) {
            return 1;
        }
    }
    return 0;
}
