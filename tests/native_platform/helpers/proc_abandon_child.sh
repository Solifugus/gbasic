#!/bin/sh
# PLAT-PROC abandonment helper. Records its OWN pid in the file named by $1, then
# `exec`s a long sleep -- exec keeps the pid, so the recorded number identifies the
# surviving process exactly. The runner later checks each recorded pid with
# `kill -0`, which is immune to the command-line string matching that would
# otherwise also match the test runner itself.
echo $$ >> "$1"
exec sleep 3117
