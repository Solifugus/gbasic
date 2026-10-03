#!/usr/bin/env bash
# WHAT A BOOK HAS TO LOOK AT AGAIN, since a release tag.
#
# `./tools/since-release.sh [tag]`  (default: the newest v* tag)
#
# WHY THIS AND NOT A HAND-KEPT LIST. A hand-kept list of "things the books must
# revisit" is a second record of what the commits already say, and this tree has
# spent a lot of effort on what happens to a second record: it goes stale, and
# its staleness is indistinguishable from there being nothing to report.
# CHANGELOG.md is the CURATED layer and stays hand-written, because "this breaks
# working programs" is a judgement. THIS is the mechanical layer under it: it
# derives everything from git and cannot rot, because there is nothing in it to
# maintain.
#
# THE ORACLE IS THE GOLDENS, WHICH ALREADY EXIST. 333 `.err` files pin exact
# diagnostic text and every `.out` pins exact behaviour, so "which messages
# changed" is not a question anybody has to remember to answer -- it is a diff.
# The 0.4.0 note's own headline was "eight diagnostics say something different,
# so anything that quotes a message verbatim has to re-capture it", and that set
# is exactly the MODIFIED `.err` files.
#
# ADDED AND MODIFIED ARE REPORTED SEPARATELY, because they are different jobs: a
# book cannot have quoted a message that did not exist, so an ADDED `.err` is
# something new to teach, while a MODIFIED one is a passage that is now WRONG.
#
# THE LIMIT, STATED SO NOBODY TRUSTS THIS ALONE: it reports only diagnostics that
# are PINNED BY A GOLDEN. Measured on v0.4.0..HEAD, one `.err` moved while the
# same commit reworded the sentence NINE module dispatchers emit -- none of those
# nine is pinned, so none appears here. The mechanical layer is a floor on what
# changed, never a ceiling, which is exactly why CHANGELOG.md stays hand-written:
# this makes the curated note cheap to write, not unnecessary.
#
# IT ALSO SEPARATES A MOVED SENTENCE FROM A MOVED POSITION. A golden that gained
# a `.nolineno` sidecar changes without its message changing, and three of the
# five this reported on its first run were exactly that -- a report that cannot
# tell them apart sends somebody to re-read a passage that is still correct.
set -uo pipefail
cd "$(dirname "$0")/.."

tag="${1:-$(git tag --list 'v*' --sort=-v:refname | head -1)}"
if ! git rev-parse "$tag" >/dev/null 2>&1; then
    printf 'no such tag: %s\n' "$tag" >&2
    printf 'tags: %s\n' "$(git tag --list 'v*' --sort=-v:refname | head -5 | tr '\n' ' ')" >&2
    exit 2
fi

n_commits="$(git rev-list --count "$tag..HEAD")"
printf '== since %s (%s commits) ==\n\n' "$tag" "$n_commits"

section() {   # <title> <diff-filter> <note> <paths...>
    local title="$1" filter="$2" note="$3"; shift 3
    local files
    files="$(git diff --name-only --diff-filter="$filter" "$tag..HEAD" -- "$@" 2>/dev/null)"
    printf -- '-- %s\n' "$title"
    if [ -z "$files" ]; then
        printf '   (none)\n\n'
        return
    fi
    [ -n "$note" ] && printf '   %s\n' "$note"
    printf '%s\n' "$files" | sed 's/^/   /'
    printf '\n'
}

section "DIAGNOSTICS THAT CHANGED -- a passage quoting one of these is now WRONG" \
    M "re-capture the message; the diff shows the old and new text:" '*.err'
section "NEW REFUSALS -- nothing to re-capture, but something new to teach" \
    A "" '*.err'
section "BEHAVIOUR THAT CHANGED -- a book showing this output is now WRONG" \
    M "" '*.out'
section "DOCS PAGES TOUCHED -- where the reasoning was written down" \
    M "" 'docs/*.md' 'docs/ai/*.md'
section "LANGUAGE SURFACE -- a new builtin or keyword lands here" \
    M "" 'src/builtins.c' 'src/parser.y' 'include/lexer.h'

# A GOLDEN CAN MOVE WITHOUT ITS MESSAGE MOVING, and the first run of this tool
# cried wolf about exactly that: three of the five it reported for v0.3.0..HEAD
# were `.nolineno` normalisations -- `edgar.bas:157:17` became
# `edgar.bas:LINE:COL` so that an insertion into a library stops rebaselining a
# golden it is not about. The SENTENCE is identical. A report that cannot tell
# those apart sends somebody to re-read a passage that is still correct, which
# is the cost this whole thing exists to avoid.
#
# Decided by stripping the POSITION from both sides and asking whether anything
# is left.
printf -- '-- THE EXACT TEXT OF EVERY CHANGED DIAGNOSTIC\n'
changed_err="$(git diff --name-only --diff-filter=M "$tag..HEAD" -- '*.err')"
if [ -z "$changed_err" ]; then
    printf '   (none)\n\n'
else
    for f in $changed_err; do
        before="$(git show "$tag:$f" 2>/dev/null | sed -E 's/:[0-9]+:[0-9]+:/:LINE:COL:/g')"
        after="$(sed -E 's/:[0-9]+:[0-9]+:/:LINE:COL:/g' "$f" 2>/dev/null)"
        if [ "$before" = "$after" ]; then
            printf '   %s\n' "$f"
            printf '      POSITION ONLY -- the sentence is unchanged, nothing to re-capture\n'
        else
            printf '   %s\n' "$f"
            git diff "$tag..HEAD" -- "$f" | grep -E '^[-+][^-+]' | sed 's/^/      /'
        fi
    done
    printf '\n'
fi

printf -- '-- COMMIT SUBJECTS\n'
git log --format='   %h %s' "$tag..HEAD"
printf '\n'
printf -- '-- CHANGELOG\n'
if grep -q '^## \[\?Unreleased' CHANGELOG.md 2>/dev/null; then
    printf '   CHANGELOG.md has an Unreleased section.\n'
elif [ "$n_commits" -gt 0 ]; then
    printf '   CHANGELOG.md has NO Unreleased section, and there are %s commits since %s.\n' \
        "$n_commits" "$tag"
    printf '   The curated layer is the one a reader of the next release sees; this report is not it.\n'
fi
