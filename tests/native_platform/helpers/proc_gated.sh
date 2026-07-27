#!/bin/sh
# PLAT-PROC incrementality helper. Writes CHUNK-ONE, then BLOCKS until the parent
# creates the gate file $1, then writes CHUNK-TWO and exits 0. The gate makes
# "output arrived in more than one read" a FACT rather than a race: while the gate
# is absent the child cannot have written CHUNK-TWO and cannot have exited, so a
# reader that has seen CHUNK-ONE is provably mid-run. Deterministic under valgrind.
printf 'CHUNK-ONE\n'
while [ ! -f "$1" ]; do
    sleep 0.02
done
printf 'CHUNK-TWO\n'
