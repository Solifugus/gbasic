#!/usr/bin/env bash
# Run a command under the shared valgrind policy (tests/valgrind_tier.sh), for
# a caller that takes a command prefix rather than a shell function.
. "$(dirname "$0")/../valgrind_tier.sh"
vg_run "$@"
