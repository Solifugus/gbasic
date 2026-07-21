#!/bin/sh
# NAP-6 binary-safety helper: emit exactly A, NUL, B (3 bytes). A strlen-based
# capture would truncate at the NUL and report 1 byte; a binary-safe capture keeps 3.
printf 'A'
head -c 1 /dev/zero
printf 'B'
