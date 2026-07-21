#!/bin/sh
# NAP-6 argv-fidelity helper: echo each argument on its own line, delimited so
# any shell metacharacters that leaked would be visible. process.run must deliver
# argv literally (no shell), so each arg appears verbatim between < >.
i=0
for a in "$@"; do
    printf 'arg[%d]=<%s>\n' "$i" "$a"
    i=$((i + 1))
done
