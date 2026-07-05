#!/usr/bin/env bash
# WP-XML-6 — MANUAL-TIER constant-memory streaming test. Proves the streaming
# claim: a >=100 MB document is windowed via skip_to("infoTable")/subtree while
# peak resident memory (VmHWM) stays under a fixed ceiling — i.e. memory is
# bounded by the element, not the file.
#
# NOT part of `make test` / run_examples.sh: it generates a large file and takes
# tens of seconds. Invoke directly:
#
#   ./tests/run_xml_bigfile.sh
#   BOUND_KB=65536 TARGET_BYTES=104857600 ./tests/run_xml_bigfile.sh
#
# Exit 0 = peak VmHWM stayed under BOUND_KB (default 64 MiB). Exit 1 = breached
# (would indicate the reader buffered the whole document instead of streaming).
set -euo pipefail

cd "$(dirname "$0")/.."

BIG="examples/tmp_xml_big.xml"
PROG="examples/tmp_xml_big_stream.bas"
BOUND_KB="${BOUND_KB:-65536}"          # 64 MiB peak-RSS ceiling
TARGET_BYTES="${TARGET_BYTES:-104857600}"  # 100 MiB source document

cleanup() { rm -f "$BIG" "$PROG"; }
trap cleanup EXIT

echo "== WP-XML-6 constant-memory streaming test =="
echo "generating $BIG (target >= $TARGET_BYTES bytes) ..."
tools/xml_bigfile_gen.sh "$BIG" "$TARGET_BYTES"
size=$(wc -c < "$BIG")
echo "source file size: $size bytes ($((size / 1024 / 1024)) MiB)"

# The windowing loop under test: stream every <infoTable>, summing <value>, so
# the whole file is genuinely walked (not short-circuited). Counts + checksum are
# printed for a sanity signal.
cat > "$PROG" <<EOF
program main(args)
    load xml
    r = xml.reader("$BIG")
    n = 0
    total = 0
    while xml.skip_to(r, "infoTable")
        t = xml.subtree(r)
        total = total + number(xml.text(xml.find(t, "value")))
        n = n + 1
    end while
    xml.close(r)
    print("records=" + string(n))
    print("checksum=" + string(total))
end program
EOF

echo "streaming while sampling /proc/<pid>/status VmHWM ..."
GBASIC_PATH=stdlib ./gbasic "$PROG" > "$PROG.out" 2>&1 &
pid=$!

before_kb=""
peak_kb=0
while kill -0 "$pid" 2>/dev/null; do
    if [[ -r "/proc/$pid/status" ]]; then
        hwm=$(awk '/^VmHWM:/ {print $2}' "/proc/$pid/status" 2>/dev/null || true)
        if [[ -n "$hwm" ]]; then
            [[ -z "$before_kb" ]] && before_kb="$hwm"
            (( hwm > peak_kb )) && peak_kb="$hwm"
        fi
    fi
done
wait "$pid"; rc=$?

echo "--- program output ---"
cat "$PROG.out"
rm -f "$PROG.out"
echo "----------------------"
echo "gbasic exit code:        $rc"
echo "VmHWM before (first obs): ${before_kb:-unknown} kB"
echo "VmHWM peak (during run):  $peak_kb kB"
echo "peak ceiling (BOUND_KB):  $BOUND_KB kB"
echo "source file:              $size bytes"

if [[ "$rc" -ne 0 ]]; then
    echo "FAIL: gbasic exited nonzero"
    exit 1
fi
if (( peak_kb == 0 )); then
    echo "FAIL: never sampled VmHWM (process too fast?)"
    exit 1
fi
if (( peak_kb < BOUND_KB )); then
    echo "PASS: peak VmHWM ${peak_kb} kB < ${BOUND_KB} kB ceiling while streaming a ${size}-byte file"
    echo "      (peak is ~$(( peak_kb * 1024 * 100 / size ))% of the source size — memory is bounded by the element, not the file)"
    exit 0
else
    echo "FAIL: peak VmHWM ${peak_kb} kB >= ${BOUND_KB} kB ceiling — the reader did NOT stream"
    exit 1
fi
