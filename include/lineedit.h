#ifndef GBASIC_LINEEDIT_H
#define GBASIC_LINEEDIT_H

/* One line of input with editing and history (src/lineedit.c).
 *
 * Written rather than linked. GNU readline is GPL-3 and gBASIC is Apache-2.0,
 * so linking it would relicense the binary we ask a beginner to download;
 * libedit is BSD but is one more thing a distribution may not have, and "no
 * arrow keys on your system" is exactly the note a first chapter cannot carry.
 * Same reasoning as the hand-written ZIP container and base32.
 *
 * `line_edit` returns a malloc'd line WITHOUT its newline (caller frees), or
 * NULL at end of input. Call it only when stdin is a terminal; a pipe wants a
 * plain read, and this would write escape sequences into its output. */
char *line_edit(const char *prompt);

/* History. `add` ignores a blank line and an immediate repeat. `load`/`save`
 * are best-effort: a history file that cannot be read or written is not an
 * error a prompt should report, it is a prompt without history. */
void line_history_add(const char *line);
void line_history_load(const char *path);
void line_history_save(const char *path);
void line_history_free(void);

#endif
