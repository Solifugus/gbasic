#!/usr/bin/env bash
# Total a corpus scan produced by tools/xlsx_corpus_check.sh.
#
# Separate from the scan so a run can be re-totalled without re-scanning, and so
# an interrupted scan still reports on what it managed.
#
# The agreement RATE alone is a misleading headline and this prints the whole
# table for that reason: when AND/OR/NOT were added, 410,121 cells moved from
# `unsupported` to `judged` and the rate DIPPED from 94.97% to 94.92%, because
# the newly judged population agreed at ~93% against an existing ~95%. Quoting
# the rate alone would have reported a real gain as a regression.
set -u

file=${1:-/tmp/xlsx_corpus_check.tsv}

if [ ! -s "$file" ]; then
    printf 'no scan data at %s\n' "$file" >&2
    exit 1
fi

awk '
$1 == "OK" {
    files++; agree += $2; disagree += $3; vol += $4; unsup += $5; sheets += $6
    if ($3 == 0) clean++
    if ($3 > 0) { dirty++; if ($3 > worst) { worst = $3; worstf = "" ; for (i = 7; i <= NF; i++) worstf = worstf (i > 7 ? " " : "") $i } }
    next
}
$1 == "ERR"     { err++;  next }
$1 == "TIMEOUT" { to++;   next }
{ other++ }
END {
    judged = agree + disagree
    printf "workbooks read ok        %10d\n", files
    printf "  read errors            %10d\n", err + 0
    printf "  timeouts               %10d\n", to + 0
    if (other) printf "  unparsed lines         %10d\n", other
    printf "sheets                   %10d\n", sheets
    printf "\n"
    printf "formula cells judged     %10d\n", judged
    printf "  agree                  %10d", agree
    if (judged) printf "  (%.2f%%)", 100.0 * agree / judged
    printf "\n"
    printf "  disagree               %10d", disagree
    if (judged) printf "  (%.2f%%)", 100.0 * disagree / judged
    printf "\n"
    printf "unsupported (skipped)    %10d\n", unsup
    printf "volatile (skipped)       %10d\n", vol
    printf "\n"
    printf "workbooks with ZERO disagreements %d of %d", clean + 0, files
    if (files) printf "  (%.1f%%)", 100.0 * clean / files
    printf "\n"
    if (worstf != "") printf "worst single workbook    %10d  %s\n", worst, worstf
    printf "\n"
    # The rate alone is not the story -- a fix that moves cells from
    # `unsupported` into `judged` can lower it while being a clear gain, so the
    # judged TOTAL has to be read alongside it.
    printf "note: compare `judged` as well as the rate; moving cells out of\n"
    printf "      `unsupported` into `judged` can lower the rate and still be a gain.\n"
}
' "$file"
