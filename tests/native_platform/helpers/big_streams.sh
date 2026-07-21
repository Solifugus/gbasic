#!/bin/sh
# NAP-6 deadlock helper: ~70 KB on BOTH streams (each > a 64 KB pipe buffer), so a
# naive "drain one fully then the other" parent would deadlock. Fixed-width lines
# ("OUTLINE-00000\n" = 14 bytes) make the exact byte count trivial: 5000*14=70000.
i=0
while [ "$i" -lt 5000 ]; do
    printf 'OUTLINE-%05d\n' "$i"
    printf 'ERRLINE-%05d\n' "$i" >&2
    i=$((i + 1))
done
