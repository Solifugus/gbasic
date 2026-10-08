#!/usr/bin/env python3
"""Generate the VS Code grammar's word lists from the interpreter's source.

    python3 tools/sync_vscode_grammar.py          # write the grammar
    python3 tools/sync_vscode_grammar.py --check  # exit 1 if it is out of date

editors/vscode/gbasic/syntaxes/gbasic.tmLanguage.template.json is edited by
hand; its @KEYWORDS@ @CONSTANTS@ @WORD_OPERATORS@ @BUILTINS@ placeholders are
filled from:

  - src/lexer.c, every `keyword_equals(start, length, "word")` -- the words
    the lexer reserves. Of those, true/false/nothing/unknown are constants and
    and/or/not are operators; the rest are keywords.
  - src/builtins.c, the registry of builtin function names.
  - CONTEXTUAL below: words that are not reserved but are part of a construct,
    each with the reason, so the list cannot grow without one.

The grammar the extension ships is the result. The old one was a hand-copied
list that had fallen three keywords behind the lexer (do, until, unwatch) and
still coloured `resume`, which the language deliberately made an ordinary name;
tests/run_editor_grammar.sh runs --check so that cannot happen silently again.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEMPLATE = ROOT / "editors/vscode/gbasic/syntaxes/gbasic.tmLanguage.template.json"
GRAMMAR = ROOT / "editors/vscode/gbasic/syntaxes/gbasic.tmLanguage.json"

CONSTANTS = {"true", "false", "nothing", "unknown"}
WORD_OPERATORS = {"and", "or", "not"}
# Not reserved, but keywords where they appear. Reason beside each.
CONTEXTUAL = {
    "from": "`load NAME from \"path\"`",
}


def lexer_keywords():
    text = (ROOT / "src/lexer.c").read_text(encoding="utf-8")
    words = set(re.findall(r'keyword_equals\(start, length, "([a-z_]+)"\)', text))
    if len(words) < 20:
        sys.exit(f"sync_vscode_grammar: found only {len(words)} keywords in src/lexer.c -- has its table changed shape?")
    return words


def builtins():
    text = (ROOT / "src/builtins.c").read_text(encoding="utf-8")
    table = re.search(r"static const char \*builtins\[\] = \{(.*?)\};", text, re.S)
    if not table:
        sys.exit("sync_vscode_grammar: no `static const char *builtins[]` table in src/builtins.c")
    names = set(re.findall(r'"([A-Za-z_][A-Za-z0-9_]*)"', table.group(1)))
    if len(names) < 50:
        sys.exit(f"sync_vscode_grammar: found only {len(names)} builtins -- has the registry changed shape?")
    return names


def alternation(words):
    # Longest first, so a regex engine trying alternatives in order never
    # stops at a prefix (`is` before `is_string`).
    return "|".join(sorted(words, key=lambda w: (-len(w), w)))


def render():
    reserved = lexer_keywords()
    missing = (CONSTANTS | WORD_OPERATORS) - reserved
    if missing:
        sys.exit(f"sync_vscode_grammar: the lexer no longer reserves {sorted(missing)}")
    keywords = (reserved - CONSTANTS - WORD_OPERATORS) | set(CONTEXTUAL)
    # A builtin that is also a keyword (print) is coloured as the keyword.
    calls = builtins() - reserved
    text = TEMPLATE.read_text(encoding="utf-8")
    for placeholder, words in (("@KEYWORDS@", keywords), ("@CONSTANTS@", CONSTANTS),
                               ("@WORD_OPERATORS@", WORD_OPERATORS), ("@BUILTINS@", calls)):
        if placeholder not in text:
            sys.exit(f"sync_vscode_grammar: {placeholder} missing from {TEMPLATE.name}")
        text = text.replace(placeholder, alternation(words))
    json.loads(text)        # what ships must be valid JSON
    return text, len(keywords), len(calls)


def main():
    text, nk, nb = render()
    if "--check" in sys.argv[1:]:
        current = GRAMMAR.read_text(encoding="utf-8") if GRAMMAR.exists() else ""
        if current != text:
            sys.exit(f"{GRAMMAR.relative_to(ROOT)} is out of date with the lexer/builtins: "
                     "run python3 tools/sync_vscode_grammar.py")
        print(f"grammar current: {nk} keywords, {nb} builtins")
        return
    GRAMMAR.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {GRAMMAR.relative_to(ROOT)}: {nk} keywords, {nb} builtins")


if __name__ == "__main__":
    main()
