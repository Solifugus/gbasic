#!/bin/sh
# NAP-6 timeout helper: sleep far longer than any test timeout so process.run must
# kill it. sleep is a child of this script; killing the process GROUP reaps both.
sleep 30
