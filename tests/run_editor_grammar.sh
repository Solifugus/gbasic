#!/usr/bin/env bash
# The VS Code extension's grammar (editors/vscode/gbasic), held to the language.
#
# Two tiers, because a grammar fails in two ways and neither is visible to any
# other suite -- the result of both is a WRONG COLOUR in somebody's editor:
#
#   DRIFT  the keyword, constant, word-operator and builtin lists are GENERATED
#          from src/lexer.c and src/builtins.c (tools/sync_vscode_grammar.py),
#          and this fails when the shipped grammar is not what they produce.
#          The hand-copied list it replaced had fallen three keywords behind
#          the lexer (do, until, unwatch) and still coloured `resume`, which the
#          language deliberately made an ordinary name.
#   COLOUR tests/editor_grammar_check.py applies the grammar the way a TextMate
#          engine does and asserts the scope of named tokens in real lines: a
#          keyword or builtin after a dot is a FIELD, a builtin name not being
#          called is a variable, a record literal is not a modifier clause,
#          `1e` is not a number. Run against the grammar it replaced, it reports
#          15 mismatches -- which is the evidence it is not vacuous.
#
# Headless, no Node or VS Code needed. Skips only without a working python3.
set -u
cd "$(dirname "$0")/.."

py=""
for c in python3 /c/msys64/ucrt64/bin/python3 python; do
    # A Windows "App execution alias" stub answers to the name and runs nothing,
    # so ask the candidate to actually execute something.
    if command -v "$c" >/dev/null 2>&1 && [ "$("$c" -c 'print(42)' 2>/dev/null)" = 42 ]; then
        py="$c"
        break
    fi
done
if [ -z "$py" ]; then
    printf 'SKIP tests/run_editor_grammar.sh (no working python3)\n'
    exit 0
fi

status=0
if out="$("$py" tools/sync_vscode_grammar.py --check 2>&1)"; then
    printf 'PASS drift: %s\n' "$out"
else
    printf 'FAIL drift: %s\n' "$out"
    status=1
fi

# SNIPPETS: each expands to code people paste into a program, so each must
# PARSE. Two did not: `on error resume next` (retired for frame-scoped
# `on error goto next`) and `f(file)= "path"` (the paren modifier spelling the
# parser now refuses). Placeholders become their defaults; parsed, never run.
make >/dev/null 2>&1 || { printf 'FAIL build\n'; exit 1; }
gb=./gbasic
[ -x ./gbasic.exe ] && gb=./gbasic.exe
snip_dir="$(mktemp -d)"
trap 'rm -rf "$snip_dir"' EXIT
"$py" - "$snip_dir" <<'PY' || { printf 'FAIL snippets: could not read editors/vscode/gbasic/snippets/gbasic.json\n'; exit 1; }
import json, re, sys
from pathlib import Path
out = Path(sys.argv[1])
snips = json.loads(Path("editors/vscode/gbasic/snippets/gbasic.json").read_text(encoding="utf-8"))
for i, (name, s) in enumerate(snips.items()):
    body = "\n".join(s["body"])
    body = re.sub(r"\$\{\d+:([^}]*)\}", r"\1", body)   # ${1:default} -> default
    body = re.sub(r"\$\d+", "", body)                  # $0, $2 -> nothing
    (out / f"{i:02d}.bas").write_text(body + "\n", encoding="utf-8")
    (out / f"{i:02d}.name").write_text(name, encoding="utf-8")
PY
snip_bad=0
snip_n=0
for f in "$snip_dir"/*.bas; do
    snip_n=$((snip_n + 1))
    if ! "$gb" --ast "$f" >/dev/null 2>"$f.err"; then
        printf 'FAIL snippet "%s" does not parse: %s\n' "$(cat "${f%.bas}.name")" "$(head -1 "$f.err")"
        snip_bad=1
    fi
done
if [ "$snip_n" -lt 5 ]; then
    printf 'FAIL snippets: only %d found\n' "$snip_n"
    status=1
elif [ "$snip_bad" = 0 ]; then
    printf 'PASS snippets (%d parse)\n' "$snip_n"
else
    status=1
fi

out="$("$py" tests/editor_grammar_check.py 2>&1)"
last="$(printf '%s\n' "$out" | tail -1)"
if [ "$last" = "mismatches: 0" ]; then
    printf 'PASS colour (%s checks)\n' "$(printf '%s\n' "$out" | grep -c '^ok ')"
else
    printf 'FAIL colour\n'
    printf '%s\n' "$out" | grep -E 'MISMATCH|BROKEN'
    status=1
fi
exit "$status"
