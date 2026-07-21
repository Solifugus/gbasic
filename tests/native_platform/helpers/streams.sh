#!/bin/sh
# NAP-6 stream/exit helper: distinct bytes on stdout vs stderr, nonzero exit.
printf 'to-stdout\n'
printf 'to-stderr\n' >&2
exit 3
