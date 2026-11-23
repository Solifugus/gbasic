#!/usr/bin/env bash
# Holds `gbasic --modules` to what the binary actually does. Sourced or run;
# usage: tests/modules_check.sh BINARY. For each row it runs a probe that
# answers "built" when the module works and "left out" when it is refused as
# "not available in this build" (or, for builtins, when has_builtin says no),
# and prints one line per disagreement, then "modules: N rows, M disagree".
# Exit 1 on any disagreement or a row with no probe.
#
# Used by run_capabilities.sh (the full build) and run_packaging.sh (the lean
# one), so each direction is measured: a full build cannot show a left-out
# row is true, nor a lean one a built row. install.sh reports from --modules
# (gbasic books, 2026-11-18: --capabilities said ODBC was built on a RHEL
# machine with no unixODBC).
bin="${1:?usage: modules_check.sh BINARY}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# a program per row; each prints the word it decides on
probe() {
    local name="$1" body
    case "$name" in
        gsql|sqlite|pg|odbc|ldap|webclient|http|smtp|xml)
            body="load $name"$'\n'"print \"built\"" ;;
        xlsx) body='on error goto next
w = xlsx.new()
if error then
    print "left out"
else
    print "built"
end if' ;;
        zip) body='on error goto next
m = zip.members("'"$work"'/none.zip")
e = error
if e and contains(e.message, "not available in this build") then
    print "left out"
else
    print "built"
end if' ;;
        tls) body='load net
on error goto next
c = net.connect({ host: "127.0.0.1", port: 1, tls: true })
e = error
if e and contains(e.message, "not available in this build") then
    print "left out"
else
    print "built"
end if' ;;
        term) body='load term
on error goto next
term.open()
e = error
if e and contains(e.message, "not available in this build") then
    print "left out"
else
    print "built"
end if' ;;
        ui) body='if is_unknown(ui.backend()) then
    print "left out"
else
    print "built"
end if' ;;
        crypto) body='if has_builtin("sha256") then
    print "built"
else
    print "left out"
end if' ;;
        password_hash) body='if has_builtin("password_hash") then
    print "built"
else
    print "left out"
end if' ;;
        # GBASIC_COMPILE=report says what it compiled, or that this build
        # cannot (set below): built only when a function WAS compiled
        compile) body='function one()
    return 1
end function
print one()' ;;
        *) return 1 ;;
    esac
    printf '%s\n' "$body" > "$work/$name.bas"
    # A REFUSAL AT `load` STOPS THE PROGRAM, so it prints nothing and its
    # sentence is on stderr: read both, and that sentence decides first
    local out
    # the `ui` row is Qt's windows: asked for by name, since a build
    # without Qt still has the terminal backend (the `term` row)
    local chosen=""
    [ "$name" = ui ] && chosen=qt
    local compile=""
    [ "$name" = compile ] && compile=report
    out=$(cd "$work" && GBASIC_UI="$chosen" GBASIC_COMPILE="$compile" timeout 30 "$bin" "$name.bas" 2>&1)
    if printf '%s' "$out" | grep -q 'not available in this build'; then
        echo "left out"
    elif [ "$name" = compile ]; then
        printf '%s' "$out" | grep -qx 'compile: one: 1 statement, 0 lowered, ran 1 time' &&
            echo built || echo "neither: $out"
    else
        printf '%s\n' "$out" | tail -1
    fi
}

rows=0 bad=0
while IFS=$'\t' read -r name said _needs; do
    [ -z "$name" ] && continue
    rows=$((rows + 1))
    if ! got=$(probe "$name"); then
        printf 'NO PROBE for --modules row %s: add one to tests/modules_check.sh\n' "$name"
        bad=$((bad + 1))
        continue
    fi
    if [ "$got" != "$said" ]; then
        printf 'DISAGREE %s: --modules says "%s", the probe found "%s"\n' "$name" "$said" "${got:-nothing}"
        bad=$((bad + 1))
    fi
done < <("$bin" --modules)
printf 'modules: %d rows, %d disagree\n' "$rows" "$bad"
[ "$rows" -gt 0 ] && [ "$bad" = 0 ]
