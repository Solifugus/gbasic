#!/usr/bin/env bash
# GATE-LANE: serial -- drives real ptys and times what comes back (a lone ESC within a second, a timer's ticks while no key comes), which a loaded machine would turn into a guess
#
# The `term` module and the terminal layer under it (docs/tui_design.md §2,
# increment 1): raw mode and every way of leaving it, keys and the mouse, the
# screen of cells and its redraw. src/term.c is plain C; src/modules/term.c
# is the module.
#
# THE ORACLES ARE OUTSIDE THE CODE: byte sequences written by hand from
# xterm's ctlseqs (the decoder's unit tier and the driver), the widths held to
# Unicode's own files, the pty's settings read by the driver itself, and, for
# what is drawn, pyte, a terminal emulator that is not ours.
#
# Tiers:
#   unit     tests/term/term_test.c over src/term.c: 46 key sequences, seven
#            split at every byte, the widths of 26 codepoints written by hand,
#            cells, clipping, wide and combining characters, colour depth, a
#            redraw writing only what changed. Under ASan and UBSan when the
#            compiler has them.
#   widths   src/term_widths.h regenerated from the Unicode files
#            (tools/make_term_widths.py) must equal the committed one; skips
#            without /usr/share/unicode at the pinned version.
#   refuse   no terminal (pipes), TERM=dumb and no `load`: term.can() false,
#            term.open refused naming why, nothing drawn.
#   keys     through a pty: raw mode as set, 24 key, mouse and paste
#            sequences each one event, a lone ESC the escape key within a
#            second, a sequence split across two writes one key, a resize; and
#            the pty restored after.
#   watch    watch(term.events) beside a timer that keeps firing; term.wait
#            refused while watched; unwatching ends the program.
#   restore  ten endings: the end, term.close, an uncaught error (shown
#            after the screen closes), stderr held until then, an actor
#            ending (the parent's terminal untouched), SIGINT, SIGTERM, SIGHUP
#            (each dying by its signal), a typed Ctrl-C, SIGTSTP and SIGCONT.
#            The pty's settings after each must equal those before.
#   screen   what is drawn, read back by pyte: text, colours, a wide
#            character, a combining mark, clipping, the cursor; an unchanged
#            frame writes no cell, one changed cell writes only it. Skips
#            without pyte (python3-pyte), unless TERM_PYTHON names a python3
#            that has it.
#   valgrind the keys tier with the program under valgrind.
#
# The pty tiers skip without python3.
. "$(dirname "$0")/portable.sh"
set -u
cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_term: build failed\n'
    exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
GB="$PWD/gbasic"
fixtures="$PWD/tests/term"

tier() {
    local name=$1 out=$2 rc=$3
    if [ "$rc" = 77 ]; then
        printf '%s\n' "$out" | grep '^SKIP' | head -1
    elif [ "$rc" = 0 ]; then
        printf 'PASS %s (%s checks)\n' "$name" "$(printf '%s\n' "$out" | grep -c '^PASS')"
    else
        printf 'FAIL %s (rc %s)\n' "$name" "$rc"
        printf '%s\n' "$out" | grep -v '^PASS' | sed 's/^/    /'
        status=1
    fi
}

# --- unit
cc=${CC:-cc}
if command -v "$cc" >/dev/null 2>&1; then
    san="-fsanitize=address,undefined -fno-omit-frame-pointer"
    if ! printf 'int main(void){return 0;}\n' | "$cc" $san -x c - -o "$work/probe" >/dev/null 2>&1; then
        san=""
    fi
    if "$cc" -std=c11 -Wall -Wextra -Wpedantic -Werror -g $san \
        -Iinclude -Isrc src/term.c tests/term/term_test.c -o "$work/term_test" 2>"$work/cc.log"; then
        if out=$("$work/term_test" 2>&1); then
            printf 'PASS unit%s: %s\n' "${san:+ (ASan, UBSan)}" "$(printf '%s\n' "$out" | tail -1)"
        else
            printf 'FAIL unit\n'
            printf '%s\n' "$out" | sed 's/^/    /'
            status=1
        fi
    else
        printf 'FAIL unit: the test did not build\n'
        sed 's/^/    /' "$work/cc.log"
        status=1
    fi
else
    printf 'SKIP unit (no C compiler)\n'
fi

# --- widths
if command -v python3 >/dev/null 2>&1 && [ -f /usr/share/unicode/EastAsianWidth.txt ]; then
    if python3 -I tools/make_term_widths.py >"$work/widths.h" 2>"$work/widths.err"; then
        if cmp -s "$work/widths.h" src/term_widths.h; then
            printf 'PASS widths (src/term_widths.h is what the generator makes from Unicode %s)\n' \
                "$(sed -n 's/.*TERM_WIDTHS_UNICODE "\(.*\)"/\1/p' src/term_widths.h)"
        else
            printf 'FAIL widths: src/term_widths.h differs from what tools/make_term_widths.py makes\n'
            diff "$work/widths.h" src/term_widths.h | head -10 | sed 's/^/    /'
            status=1
        fi
    else
        printf 'SKIP widths (%s)\n' "$(cat "$work/widths.err")"
    fi
else
    printf 'SKIP widths (no python3 or no /usr/share/unicode: nothing regenerated)\n'
fi

# --- refuse, without a terminal
cat >"$work/can.bas" <<'EOF'
load term
print term.can()
on error goto next
term.open()
if error then
    print error.message
end if
EOF
got=$("$GB" "$work/can.bas" </dev/null 2>&1 | cat)
want=$'false\nterm.open: standard input is not a terminal'
if [ "$got" = "$want" ]; then
    printf 'PASS refuse (no terminal: term.can() false, term.open refused naming why)\n'
else
    printf 'FAIL refuse: got\n%s\n' "$got" | sed '2,$s/^/    /'
    status=1
fi
printf 'x = term.can()\n' >"$work/noload.bas"
got=$("$GB" "$work/noload.bas" 2>&1 </dev/null)
case "$got" in
    *'library not loaded: term (write `load term` first)'*)
        printf 'PASS refuse (term without load names the fix)\n' ;;
    *)  printf 'FAIL refuse: without load: %s\n' "$got"; status=1 ;;
esac

if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP keys, watch, restore, screen, valgrind (no python3: nothing drove a pty)\n'
    exit $status
fi
py=python3
for t in refuse keys watch restore; do
    out=$(timeout 120 "$py" tests/term/drive.py "$t" "$GB" "$fixtures" 2>&1); rc=$?
    tier "$t (pty)" "$out" $rc
done
spy=${TERM_PYTHON:-python3}
out=$(timeout 60 "$spy" tests/term/drive.py screen "$GB" "$fixtures" 2>&1); rc=$?
tier "screen (pyte)" "$out" $rc

# --- valgrind over the keys tier: the pty driver runs the program through a
# wrapper, so a valgrind error becomes the program's exit status (VG_EXIT),
# which the driver's exit check reports. Its clocks are scaled by ten.
if vg_available; then
    {
        printf '#!/usr/bin/env bash\n'
        printf 'exec valgrind %s --log-file=%q %q "$@"\n' "$VG_FLAGS" "$work/vg.log" "$GB"
    } >"$work/vg.sh"
    chmod +x "$work/vg.sh"
    out=$(GATE_TIME_SCALE=10 timeout 600 "$py" tests/term/drive.py keys "$work/vg.sh" "$fixtures" 2>&1); rc=$?
    if [ $rc = 0 ] && ! grep -q "ERROR SUMMARY: [1-9]" "$work/vg.log"; then
        printf 'PASS valgrind (the keys tier under valgrind: no invalid access, no definite leak, no descriptor left)\n'
    else
        printf 'FAIL valgrind (rc %s)\n' "$rc"
        printf '%s\n' "$out" | grep -v '^PASS' | sed 's/^/    /'
        grep -E "Invalid|definitely|ERROR SUMMARY|Open file" "$work/vg.log" 2>/dev/null | head -10 | sed 's/^/    /'
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi
exit $status
