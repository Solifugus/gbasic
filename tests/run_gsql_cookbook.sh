#!/usr/bin/env bash
# The G-SQL cookbook (docs/gsql_cookbook.md) -- a tutorial that cannot lie.
# RUN / CODE / OUTPUT / COVER, all in tests/cookbook_harness.sh.
#
# Written 2026-11-01 so G-SQL could be tried from a page rather than from its
# design documents, and it found two things a reader would have met first.
# A line continues only inside brackets, so a long SQL string built with `+`
# at a line's end is a parse error -- the recipe wraps it in parentheses. And
# a comparison is checked when a ROW reaches it: `where name = 1` answered
# nothing over an empty table and is refused once a row is there, so recipe 10
# shows both rather than a refusal that depends on the data being present.
#
# Part 2 is the practices (docs/gsql_practices.md) as recipes: a database per
# tenant with the average-of-averages trap printed beside the right answer, a
# retry made safe by a UNIQUE request id, and loading in batches.
#
# Recipe 4's `database` block names a literal relative file, found from the
# directory the harness runs in (the repository root); it removes it before
# and after. Skips, saying so, on a build without G-SQL (POSIX only).
COOKBOOK=gsql
RECIPE_GLOB='examples/gsql_cookbook/*.bas'

cookbook_precheck() {
    local probe out
    probe="$(mktemp)"
    printf 'load gsql\nprint "loaded"\n' > "$probe.bas"
    out=$(./gbasic "$probe.bas" 2>&1)
    rm -f "$probe" "$probe.bas"
    if [ "$out" != loaded ]; then
        printf 'SKIP run_gsql_cookbook (this build has no G-SQL: %s -- the cookbook was NOT run)\n' "$out"
        exit 0
    fi
}

. "$(dirname "$0")/cookbook_harness.sh"
