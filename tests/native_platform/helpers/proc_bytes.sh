#!/bin/sh
# PLAT-PROC byte-fidelity helper. Emits, in two gate-separated halves, output with
# embedded newlines, a UTF-8 multi-byte character, and a final line with NO
# trailing newline. The split lands MID-CODEPOINT on purpose: half one ends with
# the FIRST byte of the two-byte e-acute (\303) and half two opens with its second
# (\251). A reader that reassembles bytes exactly gets "héllo"; one that loses,
# duplicates, or re-encodes a chunk boundary does not.
#   half 1: 'line-one\nline-two\nh\303'            = 20 bytes
#   half 2: '\251llo\nno-trailing-newline'         = 24 bytes
printf 'line-one\nline-two\nh\303'
while [ ! -f "$1" ]; do
    sleep 0.02
done
printf '\251llo\nno-trailing-newline'
