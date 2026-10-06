#!/usr/bin/env python3
"""How the VS Code grammar colours real gBASIC, asserted (tests/run_editor_grammar.sh).

A grammar's failure is a WRONG COLOUR, which nothing else in the tree can see:
the old one coloured `r.count` as a builtin and `resume` as a keyword, and did
not know `do`/`until` existed. This applies the generated grammar the way a
TextMate engine does -- at each position the EARLIEST match wins, ties go to
the pattern listed first, a begin/end rule (a string) consumes to its end -- and
asserts the scope of named tokens in lines written the way gBASIC is written.

Python's `re` stands in for Oniguruma; every construct the grammar uses
(lookbehind, lookahead, alternation) means the same in both.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GRAMMAR = json.loads((ROOT / "editors/vscode/gbasic/syntaxes/gbasic.tmLanguage.json").read_text(encoding="utf-8"))
CONFIG = json.loads((ROOT / "editors/vscode/gbasic/language-configuration.json").read_text(encoding="utf-8"))


def flatten(patterns):
    """Top-level patterns with includes resolved, in order."""
    out = []
    for p in patterns:
        if "include" in p:
            rule = GRAMMAR["repository"][p["include"][1:]]
            out.extend(flatten(rule["patterns"]) if "patterns" in rule and "begin" not in rule else [rule])
        else:
            out.append(p)
    return out


RULES = flatten(GRAMMAR["patterns"])


def tokenize(line):
    """[(text, scope)] for every matched token; unmatched text is skipped."""
    tokens, pos = [], 0
    while pos < len(line):
        best = None
        for rule in RULES:
            rx = rule.get("match") or rule.get("begin")
            m = re.compile(rx).search(line, pos)
            if m and (best is None or m.start() < best[1].start()):
                best = (rule, m)
        if best is None:
            break
        rule, m = best
        if "begin" in rule:
            end = re.compile(rule["end"]).search(line, m.end())
            stop = end.end() if end else len(line)
            tokens.append((line[m.start():stop], rule["name"]))
            # escapes inside the string, scoped by the string's own patterns
            for inner in rule.get("patterns", []):
                for im in re.finditer(inner["match"], line[m.end():stop]):
                    tokens.append((im.group(0), inner["name"]))
            pos = stop
            continue
        caps = rule.get("captures")
        if caps:
            for idx, cap in caps.items():
                if m.group(int(idx)):
                    tokens.append((m.group(int(idx)), cap["name"]))
        else:
            tokens.append((m.group(0), rule["name"]))
        pos = max(m.end(), pos + 1)
    return tokens


checks = 0
bad = 0


def scope_of(line, text):
    for t, s in tokenize(line):
        if t == text:
            return s
    return None


def expect(line, text, want):
    """`text` in `line` has scope `want` (None: it is not coloured at all)."""
    global checks, bad
    checks += 1
    got = scope_of(line, text)
    ok = (got is None) if want is None else (got is not None and got.startswith(want))
    if ok:
        print(f"ok   {text!r} in {line!r} -> {got}")
    else:
        bad += 1
        print(f"MISMATCH {text!r} in {line!r}: got {got}, want {want}")


# keywords the old grammar did not know, and one it should never have had
expect("do", "do", "keyword.control")
expect("until attempts !< 3", "until", "keyword.control")
expect("unwatch(inbox.messages)", "unwatch", "keyword.control")
expect("resume = 3", "resume", None)
# a keyword or builtin after a dot is a FIELD (keyword fields, rule.as, r.count)
expect("x = rule.as", "as", None)
expect("n = r.count + 1", "count", None)
expect("n = count(items)", "count", "support.function.builtin")
# a builtin name not being called is a variable
expect("count = 0", "count", None)
# constants, word operators, definitions, the receiver
expect("if x = nothing or y then", "nothing", "constant.language")
expect("if x = nothing or y then", "or", "keyword.operator.word")
expect("function bump(n)", "bump", "entity.name.function")
expect("server shop( port: 8080 )", "shop", "entity.name.type")
expect("    this.balance = this.balance + amount", "this", "variable.language")
# modifiers, inline and as a clause -- and a record literal is NOT one
expect('print {file}"/etc/hostname"', '{file}', "storage.modifier")
expect('when {date}= "2026-10-06"', '{date}', "storage.modifier")
expect('parts {split ","}= line', '{split ","}', "storage.modifier")
expect('r = { a: 1 }', '{ a: 1 }', None)
# numbers: scientific notation and hex; `1e` is the number 1 beside a name
expect("x = 6.02e23", "6.02e23", "constant.numeric")
expect("x = 0xFF", "0xFF", "constant.numeric")
expect("x = 1e", "1e", None)
# strings and escapes; an unknown escape is marked
expect('s = "a\\tb"', '\\t', "constant.character.escape")
expect('s = "a\\qb"', '\\q', "invalid.illegal")
expect('s = "it\'s"', "'s\"", None)          # an apostrophe in a string is not a comment
expect("x = 1 ' a comment", "' a comment", "comment.line")

# indentation: what opens a block and what closes one
inc = re.compile(CONFIG["indentationRules"]["increaseIndentPattern"])
dec = re.compile(CONFIG["indentationRules"]["decreaseIndentPattern"])
for line, want_inc in [("if x > 1 then", True), ("if x > 1 then y = 2", False),
                       ("do", True), ("with lock(f)", True), ("else", True),
                       ("for each r in rows", True), ("function f() return 1 end function", False)]:
    checks += 1
    if bool(inc.match(line)) == want_inc:
        print(f"ok   indent after {line!r}: {want_inc}")
    else:
        bad += 1
        print(f"MISMATCH indent after {line!r}: want {want_inc}")
for line in ["end if", "next", "until done", "else"]:
    checks += 1
    if dec.match(line):
        print(f"ok   dedent at {line!r}")
    else:
        bad += 1
        print(f"MISMATCH dedent at {line!r}")

if checks < 30:
    print(f"BROKEN: only {checks} checks ran")
    sys.exit(1)
print(f"mismatches: {bad}")
sys.exit(1 if bad else 0)
