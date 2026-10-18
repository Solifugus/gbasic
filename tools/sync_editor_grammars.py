#!/usr/bin/env python3
"""Generate every editor definition's word lists from the interpreter's source.

    python3 tools/sync_editor_grammars.py          # write them
    python3 tools/sync_editor_grammars.py --check  # exit 1 if any is out of date

THREE DEFINITIONS, ONE SOURCE. Only the VS Code grammar was generated; Kate's
and GtkSourceView's lists were hand-copied, and editors/vscode/gbasic/README.md
said so in as many words -- "still carries hand-copied lists and is not
checked". MEASURED 2026-10-18, that note documented the rot instead of
preventing it:

                       keywords missing   registry builtins missing   ghosts
    VS Code (generated)        0                    0                   0
    Kate                       5                  105             resume, this,
                                                                  constructor
    GtkSourceView              5                  n/a                resume

The five are `do`, `until`, `unwatch`, `excluding` and `intersecting` -- the
last two being 0.6.0's set operators. THE GHOSTS ARE THE MORE MISLEADING
DIRECTION: `resume` was retired when `on error goto next` replaced
`on error resume next`, and `this`/`constructor` are not keywords at all, so a
program naming a variable `resume` had it coloured as reserved. The VS Code
grammar had exactly this defect before it was generated, which is the whole
argument for generating the other two.

AND THE KATE FILE IS NO LONGER ONLY A CONVENIENCE: §0.14 made it the definition
the Qt `source` widget LOADS AT RUN TIME, so it is what a gBASIC editor
highlights with.

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
# GtkSourceView splits its keywords into two contexts and Kate does not. BOTH
# OF ITS STYLES map-to="def:keyword", so the split is cosmetic -- it is kept
# because the file is hand-written apart from its word lists and reproducing
# its shape is what lets only the lists be generated. A keyword that is not
# named here lands in the ordinary `keywords` context, which is the harmless
# default rather than a decision this tool has to make.
DECLARATIONS = {"program", "library", "function", "modifier", "load", "use", "export"}


def lexer_keywords():
    text = (ROOT / "src/lexer.c").read_text(encoding="utf-8")
    words = set(re.findall(r'keyword_equals\(start, length, "([a-z_]+)"\)', text))
    if len(words) < 20:
        sys.exit(f"sync_editor_grammars: found only {len(words)} keywords in src/lexer.c -- has its table changed shape?")
    return words


def builtins():
    """Every name a program can call without qualification.

    THERE ARE TWO LISTS IN src/builtins.c AND THIS READ ONLY ONE, which is why
    `read`, `write`, `exists`, `files`, `folders`, `lock`, `all`, `any`, `mod`
    and nine more were uncoloured in every editor. `builtins[]` is the
    registry; `dispatch_only[]` holds the names that are callable but
    deliberately NOT registered -- `warning` must resolve AFTER the environment
    walk, which is the single placement decision that bought the warning
    channel zero reserved words, and registering it would take that away.

    `gbasic_has_builtin` is the function that answers for both, and "a name
    callable without qualification" is exactly what a grammar should colour, so
    the union is the right set rather than either list alone. THE DRIFT CHECK
    COULD NOT SEE THIS: it compares the grammar against whatever this function
    returns, so a generator reading one list and a check reading the same one
    agreed perfectly while the answer was incomplete -- agreement is not
    correctness, which is PLAT-NUMFMT's recorded lesson one level along.
    """
    text = (ROOT / "src/builtins.c").read_text(encoding="utf-8")
    table = re.search(r"static const char \*builtins\[\] = \{(.*?)\};", text, re.S)
    if not table:
        sys.exit("sync_editor_grammars: no `static const char *builtins[]` table in src/builtins.c")
    names = set(re.findall(r'"([A-Za-z_][A-Za-z0-9_]*)"', table.group(1)))
    if len(names) < 50:
        sys.exit(f"sync_editor_grammars: found only {len(names)} builtins -- has the registry changed shape?")

    only = re.search(r"dispatch_only\[\] = \{(.*?)\n    \};", text, re.S)
    if not only:
        sys.exit("sync_editor_grammars: no `dispatch_only[]` list in src/builtins.c -- "
                 "has gbasic_has_builtin changed shape?")
    extra = set(re.findall(r'^\s*"([A-Za-z_][A-Za-z0-9_]*)",', only.group(1), re.M))
    # A FLOOR IN BOTH PLACES, because a regex that stops matching reports a
    # clean run rather than a broken one -- this gate's own recorded failure.
    if len(extra) < 10:
        sys.exit(f"sync_editor_grammars: found only {len(extra)} dispatch-only names -- "
                 "has that list changed shape?")
    return names | extra


def alternation(words):
    # Longest first, so a regex engine trying alternatives in order never
    # stops at a prefix (`is` before `is_string`).
    return "|".join(sorted(words, key=lambda w: (-len(w), w)))


def render():
    reserved = lexer_keywords()
    missing = (CONSTANTS | WORD_OPERATORS) - reserved
    if missing:
        sys.exit(f"sync_editor_grammars: the lexer no longer reserves {sorted(missing)}")
    keywords = (reserved - CONSTANTS - WORD_OPERATORS) | set(CONTEXTUAL)
    # A builtin that is also a keyword (print) is coloured as the keyword.
    calls = builtins() - reserved
    text = TEMPLATE.read_text(encoding="utf-8")
    for placeholder, words in (("@KEYWORDS@", keywords), ("@CONSTANTS@", CONSTANTS),
                               ("@WORD_OPERATORS@", WORD_OPERATORS), ("@BUILTINS@", calls)):
        if placeholder not in text:
            sys.exit(f"sync_editor_grammars: {placeholder} missing from {TEMPLATE.name}")
        text = text.replace(placeholder, alternation(words))
    json.loads(text)        # what ships must be valid JSON
    return text, len(keywords), len(calls)


def replace_block(text, opener, closer, body, label):
    """Swap one delimited block's body, keeping everything around it."""
    i = text.find(opener)
    if i < 0:
        sys.exit(f"sync_editor_grammars: {label}: no {opener!r}")
    j = text.find(closer, i + len(opener))
    if j < 0:
        sys.exit(f"sync_editor_grammars: {label}: {opener!r} is never closed")
    return text[:i + len(opener)] + body + text[j:]


def kate(keywords, calls):
    """editors/kate/gbasic.xml -- four <list> bodies, nothing else touched."""
    path = ROOT / "editors/kate/gbasic.xml"
    text = path.read_text(encoding="utf-8")
    for name, words in (("keywords", keywords), ("constants", CONSTANTS),
                        ("operators", WORD_OPERATORS), ("builtins", calls)):
        body = "\n" + "".join(f"      <item>{w}</item>\n"
                               for w in sorted(words)) + "    "
        text = replace_block(text, f'<list name="{name}">', "</list>", body,
                             f"kate {name}")
    return path, text


def gtksourceview(keywords):
    """stdlib/gtksourceview/gbasic.lang -- the four keyword contexts.

    KEYWORDS ONLY, which is this file's own stated design (its header grounds
    it in docs/TOKENS.md) rather than an omission -- it carries no builtin list
    and adding one would be a change in kind, not a repair. What is repaired is
    the rot: five keywords missing and `resume`, which the language retired.
    """
    path = ROOT / "stdlib/gtksourceview/gbasic.lang"
    text = path.read_text(encoding="utf-8")
    # THE WORD OPERATORS GO IN HERE, because this file has no operator style of
    # its own -- Kate has a third list for them and GtkSourceView does not, so
    # leaving them out of both dropped `and`, `or` and `not` from the only
    # place this file could colour them. A regression caught by checking the
    # GENERATED files against the lexer rather than by trusting the generator.
    plain = (keywords | WORD_OPERATORS) - DECLARATIONS - set(CONTEXTUAL)
    for cid, words in (("boolean", {"true", "false"}),
                       ("constant", CONSTANTS - {"true", "false"}),
                       ("declarations", DECLARATIONS),
                       ("keywords", plain)):
        body = "\n" + "".join(f"      <keyword>{w}</keyword>\n"
                               for w in sorted(words)) + "    "
        # The opener carries the id, so find it and then the first `>` after.
        marker = f'<context id="{cid}"'
        i = text.find(marker)
        if i < 0:
            sys.exit(f"sync_editor_grammars: gtksourceview: no context {cid!r}")
        head = text.find(">", i) + 1
        j = text.find("</context>", head)
        if j < 0:
            sys.exit(f"sync_editor_grammars: gtksourceview: {cid} is never closed")
        text = text[:head] + body + text[j:]
    return path, text


def main():
    text, nk, nb = render()
    reserved = lexer_keywords()
    keywords = (reserved - CONSTANTS - WORD_OPERATORS) | set(CONTEXTUAL)
    calls = builtins() - reserved
    outputs = [(GRAMMAR, text), kate(keywords, calls), gtksourceview(keywords)]

    if "--check" in sys.argv[1:]:
        stale = [p for p, want in outputs
                 if (p.read_text(encoding="utf-8") if p.exists() else "") != want]
        if stale:
            names = ", ".join(str(p.relative_to(ROOT)) for p in stale)
            sys.exit(f"out of date with the lexer/builtins: {names} -- "
                     "run python3 tools/sync_editor_grammars.py")
        print(f"all three current: {nk} keywords, {nb} builtins")
        return
    for p, want in outputs:
        p.write_text(want, encoding="utf-8", newline="\n")
        print(f"wrote {p.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
