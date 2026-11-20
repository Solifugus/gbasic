/* ksyntax: KDE's syntax definitions (KSyntaxHighlighting's XML) read and
 * applied in plain C, for the terminal backend's `source` editor
 * (docs/tui_design.md §3.9). It follows the rules of KDE's engine, and
 * tests/run_ui_tui.sh holds it to that engine line by line.
 *
 * Positions are bytes of UTF-8. Nothing here draws; the editor asks for a
 * line's spans and paints them.
 */
#ifndef GB_KSYNTAX_H
#define GB_KSYNTAX_H

#include <stddef.h>
#include <stdint.h>

typedef struct KshRepo KshRepo;
typedef struct KshDef KshDef;
typedef struct KshState KshState;
typedef struct KshTheme KshTheme;

/* a run of one format in a line, in bytes; format -1 is none */
typedef struct {
    size_t start, len;
    int format;
} KshSpan;

/* what a format looks like under a theme: colours as 0xRRGGBB */
typedef struct {
    uint32_t fg, bg;
    int has_bg;
    int bold, italic, underline, strike;
} KshLook;

/* Where definitions and themes come from, in KDE's order: the user's data
 * directory, the system's, then every directory added here, each holding
 * definitions under `syntax/` and themes under `themes/`.
 * KDE's own themes, carried in the binary, come before the added
 * directories, where KDE's library has its compiled-in ones. */
KshRepo *ksh_repo_new(void);
void ksh_repo_add_path(KshRepo *repo, const char *dir);
void ksh_repo_free(KshRepo *repo);

/* a definition by its name or one of its alternative names, any case;
 * NULL when none is installed or it cannot be read */
KshDef *ksh_find(KshRepo *repo, const char *name);
const char *ksh_def_name(const KshDef *def);
const char *ksh_def_file(const KshDef *def);

/* the state before the first line, and each line's state after it; a NULL
 * state means the first line. The spans are malloc'd, the state returned is
 * the caller's to free. */
KshState *ksh_highlight(KshDef *def, const KshState *before, const char *line,
                        size_t len, KshSpan **spans, size_t *nspans);
int ksh_state_same(const KshState *a, const KshState *b);
KshState *ksh_state_copy(const KshState *s);
void ksh_state_free(KshState *s);

const char *ksh_format_name(const KshRepo *repo, int format);

/* themes by name; "" lists them sorted, one a line, malloc'd */
const KshTheme *ksh_theme(KshRepo *repo, const char *name);
char *ksh_theme_names(KshRepo *repo);
const char *ksh_theme_name(const KshTheme *theme);
/* "BackgroundColor", "TextSelection", "LineNumbers" ...; 0 if the theme has none */
int ksh_theme_editor(const KshTheme *theme, const char *role, uint32_t *rgb);
/* the theme nearest a background colour, as KDE's themeForPalette chooses */
const KshTheme *ksh_theme_for_background(KshRepo *repo, uint32_t rgb);
void ksh_look(const KshRepo *repo, int format, const KshTheme *theme, KshLook *out);

#endif
