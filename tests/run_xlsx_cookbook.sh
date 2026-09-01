#!/usr/bin/env bash
# The xlsx cookbook (docs/xlsx_cookbook.md) -- a tutorial that cannot lie.
#
# A cookbook's failure mode is not being wrong when written, it is being wrong
# six months later while still reading as authoritative. This suite removes the
# gap between the page and the product in both directions:
#
#   RUN     -- every recipe in examples/xlsx_cookbook/ executes, and its stdout
#              must match its committed .out byte for byte. So the page's output
#              is not a transcript somebody pasted; it is a golden.
#   CODE    -- every ```basic block on the page must equal the .bas file it came
#              from, byte for byte. Editing a recipe without re-running
#              tools/sync_xlsx_cookbook.sh fails here.
#   OUTPUT  -- every output block on the page must equal that recipe's .out. So
#              a behaviour change that legitimately moves a golden ALSO moves
#              the page, and the two cannot drift apart silently.
#   COVER   -- every recipe file must appear on the page, and every marker on
#              the page must have a file. Adding recipe 13 and forgetting to
#              document it is a failure, not a silent omission.
#
# Skips cleanly when the build has no zlib/libxml2 (no xlsx at all); the two
# recipes that need sqlite skip independently.
COOKBOOK=xlsx
RECIPE_GLOB='examples/xlsx_cookbook/*.bas'

COOKBOOK_CLEANUP='examples/tmp_cookbook_edit.xlsx'

# Ask the BINARY whether xlsx is in this build, rather than probing for zlib
# and libxml2 from the shell: the answer that matters is the one the
# interpreter gives.
cookbook_precheck() {
    probe="$(mktemp)"; probe_bas="${probe}.bas"
    printf 'program main()\n    wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")\nend program\n' > "$probe_bas"
    ./gbasic "$probe_bas" >/dev/null 2>"$probe"
    if grep -q 'not available in this build' "$probe"; then
        printf 'SKIP run_xlsx_cookbook (xlsx not in this build: needs zlib + libxml2)\n'
        rm -f "$probe" "$probe_bas"
        exit 0
    fi
    rm -f "$probe" "$probe_bas"

    have_sqlite=1
    sq="$(mktemp)"; sq_bas="${sq}.bas"
    printf 'program main()\n    db = sqlite.connect(":memory:")\nend program\n' > "$sq_bas"
    ./gbasic "$sq_bas" >/dev/null 2>"$sq"
    grep -q 'not available in this build' "$sq" && have_sqlite=0
    rm -f "$sq" "$sq_bas"
}

# Recipes 11 and 12 put a frame into a database, so they skip INDEPENDENTLY of
# the rest of the page when sqlite is not in the build.
cookbook_recipe_skip() {
    case "$1" in
        11_*|12_*)
            if [ "$have_sqlite" = "0" ]; then
                printf 'SKIP %s (needs sqlite)\n' "examples/xlsx_cookbook/$1.bas"
                return 0
            fi
            ;;
    esac
    return 1
}

. "$(dirname "$0")/cookbook_harness.sh"
