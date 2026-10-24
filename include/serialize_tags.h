#ifndef GBASIC_SERIALIZE_TAGS_H
#define GBASIC_SERIALIZE_TAGS_H

/* The value tags of `serialize`'s encoding (src/eval.c). Shared because G-SQL
 * reads the same encoding (src/gsql/key.c turns a value into an index key), and
 * a second copy of these numbers is a second thing that can disagree with the
 * writer. The numbers are on disk in every payload ever written, so they are
 * APPENDED to and never renumbered. */
typedef enum {
    SER_NULL = 1,
    SER_UNKNOWN,
    SER_BOOL,
    SER_NUMBER,
    SER_STRING,
    SER_ARRAY,
    SER_RECORD,
    SER_DATETIME,
    SER_DURATION,
    SER_MONEY,
    SER_FILE,
    SER_DIR,
    SER_ACTOR,  /* spawn-args only: an actor handle, realized by an inherited fd */
    SER_FUNCTION, /* a function value: registered name (+ optional library), §10 */
    SER_REGEX   /* a compiled regex: its pattern + flags; the receiver recompiles */
} SerTag;

#endif
