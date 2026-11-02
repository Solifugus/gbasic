#ifndef GB_CSV_H
#define GB_CSV_H

/* Delimited-file reading (docs/csv_design.md §3). No interpreter types: the
 * reader hands out fields as spans into its own buffer, valid until the next
 * call, and the `csv` module (src/modules/csv.c) turns them into values. */

#include <stddef.h>
#include <stdint.h>

/* A record larger than this is refused rather than held, naming the line it
 * started on: almost always a quote that never closes, which would otherwise
 * read the rest of a 10 GB file into memory before saying so. */
#define GB_CSV_MAX_RECORD (64u * 1024u * 1024u)

typedef struct GbCsvReader GbCsvReader;

typedef enum {
    GB_CSV_OK = 0,
    GB_CSV_TEXT_AFTER_QUOTE      /* `"abc"def`: the record is ambiguous */
} GbCsvProblem;

/* `quote` is a byte, or -1 for a file with no quoting. Answers NULL with errno
 * set when the file cannot be opened. */
GbCsvReader *gb_csv_open_file(const char *path, char delimiter, int quote);
/* Reads `text` in place: it must outlive the reader. */
GbCsvReader *gb_csv_open_text(const char *text, size_t length, char delimiter, int quote);
void gb_csv_close(GbCsvReader *r);

/* 1: a record is in hand. 0: end of input. -1: an error the caller raises,
 * described by gb_csv_error (an unterminated quote, a record over the limit,
 * a read error). */
int gb_csv_next(GbCsvReader *r);

size_t gb_csv_field_count(const GbCsvReader *r);
const char *gb_csv_field(const GbCsvReader *r, size_t index, size_t *length);
/* Where the record in hand started: the line counted from 1, the byte from 0. */
uint64_t gb_csv_line(const GbCsvReader *r);
uint64_t gb_csv_offset(const GbCsvReader *r);
GbCsvProblem gb_csv_problem(const GbCsvReader *r);
const char *gb_csv_error(const GbCsvReader *r);

/* Writing: does this field need quoting under (delimiter, quote)? */
int gb_csv_needs_quote(const char *field, size_t length, char delimiter, int quote);

#endif
