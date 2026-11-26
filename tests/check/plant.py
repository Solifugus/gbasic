#!/usr/bin/env python3
"""Plant one classic-BASIC mistake in a gBASIC file whose fix is certain.

plant.py KIND FILE OUT -> writes OUT, prints the 1-based line planted on, or
exits 1 when FILE has no place for KIND. The mistake is put on code, never in
a string or a comment: each line is read with its strings and comments
blanked out first. Kinds: ne (`!=` -> `<>`), elseif (`else if` -> `elseif`),
endif (`end if` -> `endif`), let (`x = ` at a statement's start -> `let x = `),
mod (`mod(a, b)` of two plain names or numbers, standing alone as an
assignment's whole right side -> `a mod b`).
"""
import re
import sys

kind, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(src, "rb").read()
try:
    text = data.decode("utf-8")
except UnicodeDecodeError:
    sys.exit(1)
lines = text.split("\n")


def code_only(line):
    """the line with string contents and any comment replaced by spaces"""
    out, quote, i = [], False, 0
    while i < len(line):
        c = line[i]
        if quote:
            if c == "\\" and i + 1 < len(line):
                out.append("  ")
                i += 2
                continue
            if c == '"':
                quote = False
                out.append('"')
            else:
                out.append(" ")
        elif c == '"':
            quote = True
            out.append('"')
        elif c == "'":
            out.append(" " * (len(line) - i))
            break
        else:
            out.append(c)
        i += 1
    return "".join(out)


# blocks whose bodies are not gBASIC statements: SQL, windows, servers
skip_open = re.compile(r"^\s*(sql\b|database\b|window\b|server\b|consider\b)", re.I)
skip_close = re.compile(r"^\s*end\s+(sql|database|window|server|consider)\b", re.I)
depth = 0
ATOM = r"[A-Za-z_][A-Za-z0-9_]*|[0-9]+"
KEYWORDS = {"if", "then", "else", "and", "or", "not", "return", "print", "while",
            "until", "to", "step"}
for n, line in enumerate(lines):
    code = code_only(line)
    if skip_open.match(code):
        depth += 1
        continue
    if skip_close.match(code):
        depth = max(0, depth - 1)
        continue
    if depth:
        continue
    new = None
    if kind == "ne":
        i = code.find("!=")
        if i >= 0:
            new = line[:i] + "<>" + line[i + 2:]
    elif kind == "elseif":
        m = re.match(r"^(\s*)else if\b", code)
        if m:
            new = line[:m.end(1)] + "elseif" + line[m.end(1) + 7:]
    elif kind == "endif":
        m = re.match(r"^(\s*)end if\s*$", code)
        if m:
            new = line[:m.end(1)] + "endif" + line[m.end(1) + 6:]
    elif kind == "let":
        m = re.match(r"^(\s*)([a-z_][a-z0-9_]*) = ", code)
        if m and m.group(2) not in ("let", "rem"):
            new = line[:m.end(1)] + "let " + line[m.end(1):]
    elif kind == "mod":
        # where `a mod b` binds as the call did: after `=`, `(`, `,`, `[`, a
        # comparison or a keyword, and before the line's end, a closing
        # bracket, a comparison, `+`, `-` or a keyword
        for m in re.finditer(r"(?<![A-Za-z0-9_.])mod\((" + ATOM + r"), (" + ATOM + r")\)", code):
            before = code[:m.start()].rstrip()
            after = code[m.end():].lstrip()
            word_before = re.search(r"([a-z]+)$", before)
            ok_before = (before == "" or before[-1] in "=(,[<>" or
                         (word_before and word_before.group(1) in KEYWORDS and
                          not before[:word_before.start()].endswith(".")))
            word_after = re.match(r"([a-z]+)\b", after)
            ok_after = (after == "" or after[0] in ")],=<>!+-" or
                        (word_after and word_after.group(1) in KEYWORDS))
            if ok_before and ok_after:
                new = line[:m.start()] + m.group(1) + " mod " + m.group(2) + line[m.end():]
                break
    if new is not None:
        lines[n] = new
        open(out, "wb").write("\n".join(lines).encode("utf-8"))
        print(n + 1)
        sys.exit(0)
sys.exit(1)
