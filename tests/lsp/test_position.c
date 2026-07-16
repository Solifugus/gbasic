/* Unit test for the byte-column -> LSP-position transcode (PLAN.md Phase L).
 *
 * Exercises the two things that make the conversion non-trivial: the 1-based ->
 * 0-based shift, and the divergence between UTF-8 (byte) columns and UTF-16 code
 * units for multi-byte and astral (surrogate-pair) characters. Links only
 * src/lsp/lsp_position.o — no LSP transport, no JSON, no libgbasic. */
#include "lsp_position.h"

#include <stdio.h>

static int failures = 0;

static void check(const char *name, lsp_position got, int want_line, int want_char) {
    if (got.line == want_line && got.character == want_char) {
        printf("    ok   %-28s -> (%d,%d)\n", name, got.line, got.character);
    } else {
        printf("    FAIL %-28s: got (%d,%d) want (%d,%d)\n",
               name, got.line, got.character, want_line, want_char);
        failures++;
    }
}

int main(void) {
    /* --- ASCII: UTF-8 and UTF-16 agree; only the 1-based -> 0-based shift --- */
    const char *ascii = "abcd efgh\n";
    check("ascii utf8 col5",  lsp_position_from_byte(ascii, 1, 5, LSP_ENCODING_UTF8),  0, 4);
    check("ascii utf16 col5", lsp_position_from_byte(ascii, 1, 5, LSP_ENCODING_UTF16), 0, 4);
    check("ascii col1",       lsp_position_from_byte(ascii, 1, 1, LSP_ENCODING_UTF16), 0, 0);

    /* --- 2-byte char: é = U+00E9 = 0xC3 0xA9. Line "éx = 1"; 'x' is byte col 3.
       UTF-8 char = 2 bytes before x; UTF-16 char = 1 (é is one code unit). --- */
    const char *two = "\xC3\xA9x = 1\n";
    check("2byte utf8 at x",  lsp_position_from_byte(two, 1, 3, LSP_ENCODING_UTF8),  0, 2);
    check("2byte utf16 at x", lsp_position_from_byte(two, 1, 3, LSP_ENCODING_UTF16), 0, 1);

    /* --- Astral char: 😀 = U+1F600 = 0xF0 0x9F 0x98 0x80 (4 bytes, 2 UTF-16
       units). Line "😀y"; 'y' is byte col 5. UTF-8 char = 4; UTF-16 char = 2. --- */
    const char *astral = "\xF0\x9F\x98\x80y\n";
    check("astral utf8 at y",  lsp_position_from_byte(astral, 1, 5, LSP_ENCODING_UTF8),  0, 4);
    check("astral utf16 at y", lsp_position_from_byte(astral, 1, 5, LSP_ENCODING_UTF16), 0, 2);

    /* --- Multi-line: a position on line 3 must resolve against line 3's start,
       not the document start. "a\nb\néx\n"; line 3 is "éx", 'x' at byte col 3. --- */
    const char *ml = "a\nb\n\xC3\xA9x\n";
    check("ml line3 utf16 at x", lsp_position_from_byte(ml, 3, 3, LSP_ENCODING_UTF16), 2, 1);
    check("ml line3 utf8 at x",  lsp_position_from_byte(ml, 3, 3, LSP_ENCODING_UTF8),  2, 2);

    /* --- Column at start of a later line -> character 0. --- */
    check("ml line3 col1", lsp_position_from_byte(ml, 3, 1, LSP_ENCODING_UTF16), 2, 0);

    if (failures == 0) {
        printf("PASS test_position\n");
        return 0;
    }
    printf("FAIL test_position (%d failure%s)\n", failures, failures == 1 ? "" : "s");
    return 1;
}
