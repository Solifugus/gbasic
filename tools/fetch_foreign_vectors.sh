#!/usr/bin/env bash
# Fetch TEST VECTORS PRODUCED BY SOMEBODY ELSE, and record where each came
# from, when, and under what terms.
#
# WHY THESE EXIST. Every fixture in tests/finio/ was written by this project,
# generated from this project's model of the format -- so the arithmetic
# oracles, strong as they are, check our reader against our generator's SHARED
# understanding. A file written by a different implementation embodies someone
# else's model, which is the only kind of disagreement that can find a
# misunderstanding we hold consistently.
#
# THE RULE THIS SCRIPT ENFORCES, and it is the operator's rule rather than a
# convenience:
#
#   an EXPLICIT PROHIBITION on use          -> exclude, and record why
#   an EXPLICIT PERMISSION                  -> use; redistribute only if it says so
#   SILENCE                                 -> may be used, must NOT be redistributed
#
# Silence is the conservative case and it is the common one: a bank publishing
# a sample statement in its developer documentation has granted nothing in
# writing, so the sample may be read and must not be committed here.
#
# NOTHING IS FETCHED WITHOUT A LICENCE LINE IN THE MANIFEST BELOW. Adding a
# source means stating its terms first, which is the point.
set -u
cd "$(dirname "$0")/.."
out="${1:-tests/finio/foreign}"
mkdir -p "$out"
today="$(date -u +%Y-%m-%d)"

# name | url | licence | use | redistribute
MANIFEST="
ppd-debit.ach|https://raw.githubusercontent.com/moov-io/ach/master/test/testdata/ppd-debit.ach|Apache-2.0|yes|yes
extended-ascii.ach|https://raw.githubusercontent.com/moov-io/ach/master/test/testdata/extended-ascii.ach|Apache-2.0|yes|yes
cor-example.ach|https://raw.githubusercontent.com/moov-io/ach/master/test/testdata/cor-example.ach|Apache-2.0|yes|yes
adv.ach|https://raw.githubusercontent.com/moov-io/ach/master/test/testdata/adv.ach|Apache-2.0|yes|yes
gl-debit.ach|https://raw.githubusercontent.com/moov-io/ach/master/test/testdata/gl-debit.ach|Apache-2.0|yes|yes
20180713-IAT.ach|https://raw.githubusercontent.com/moov-io/ach/master/test/testdata/20180713-IAT.ach|Apache-2.0|yes|yes
NACHA_SAMPLE_TEL_REVERSAL.ach|https://raw.githubusercontent.com/moov-io/ach/master/test/testdata/NACHA_SAMPLE_TEL_REVERSAL.ach|Apache-2.0|yes|yes
20110805A.ach|https://raw.githubusercontent.com/moov-io/ach/master/test/testdata/20110805A.ach|Apache-2.0|yes|yes
FISERV-ZEROFILE.ach|https://raw.githubusercontent.com/moov-io/ach/master/test/testdata/FISERV-ZEROFILE-PIMRET825324_032720_110221.ach|Apache-2.0|yes|yes
20110729A-invalid.ach|https://raw.githubusercontent.com/moov-io/ach/master/test/testdata/20110729A-invalid.ach|Apache-2.0|yes|yes
"

man="$out/PROVENANCE.txt"
{
  printf '# Test vectors produced by implementations OTHER THAN THIS ONE.\n'
  printf '#\n'
  printf '# Written by tools/fetch_foreign_vectors.sh. Every line records where a\n'
  printf '# file came from, when it was retrieved, the licence it carries, and\n'
  printf '# whether that licence permits USE and REDISTRIBUTION. A file whose terms\n'
  printf '# prohibit use is not fetched at all; a file whose terms are SILENT may be\n'
  printf '# used and may NOT be committed, so it will not appear here.\n'
  printf '#\n'
  printf '# ATTRIBUTION. The ACH files below come from the moov-io/ach project\n'
  printf '# (https://github.com/moov-io/ach), Copyright The Moov Authors, licensed\n'
  printf '# under the Apache License, Version 2.0. They are redistributed here\n'
  printf '# unmodified under section 4 of that licence; see tests/finio/foreign/LICENSE-moov-io-ach.\n'
  printf '#\n'
  printf '# EXCLUDED, and why -- recorded because an absent source that is merely\n'
  printf '# forgotten and one that was deliberately not used look identical later:\n'
  printf '#   Goldman Sachs camt.053 samples (developer.gs.com) -- HTTP 403 to an\n'
  printf '#     automated fetch on %s. NOT a prohibition: unavailable, which is a\n' "$today"
  printf '#     different fact and is why finio_watch has `unreachable` as its own\n'
  printf '#     finding. A person may retrieve these by hand.\n'
  printf '#   Nacha ACH Guide for Developers (achdevguide.nacha.org) -- HTTP 403,\n'
  printf '#     same reason.\n'
  printf '#\n'
  printf '# name\tsha256\tbytes\tretrieved\tlicence\tuse\tredistribute\tsource\n'
} > "$man"

fetched=0
skipped=0
printf '%s\n' "$MANIFEST" | while IFS='|' read -r name url lic use redist; do
    [ -z "${name:-}" ] && continue
    if [ "$use" != "yes" ]; then
        printf '  EXCLUDED %s -- its terms prohibit use (%s)\n' "$name" "$lic"
        continue
    fi
    if ! timeout 30 curl -sSf -o "$out/$name" "$url"; then
        printf '  UNREACHABLE %s -- not fetched; recorded as unavailable, not prohibited\n' "$name"
        continue
    fi
    sum="$(sha256sum "$out/$name" | cut -d' ' -f1)"
    sz="$(wc -c <"$out/$name")"
    if [ "$redist" != "yes" ]; then
        # Usable, not committable. Keep the record, drop the bytes.
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$sum" "$sz" "$today" "$lic" "$use" "$redist" "$url" >> "$man"
        rm -f "$out/$name"
        printf '  NOT COMMITTED %s -- licence is silent on redistribution; hash and source recorded\n' "$name"
        continue
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$sum" "$sz" "$today" "$lic" "$use" "$redist" "$url" >> "$man"
    printf '  %-32s %7s bytes  %s\n' "$name" "$sz" "$lic"
done

# The licence itself travels with the files, as Apache-2.0 section 4 requires.
timeout 30 curl -sSf -o "$out/LICENSE-moov-io-ach" https://raw.githubusercontent.com/moov-io/ach/master/LICENSE \
  && printf '  %-32s %7s bytes  (the licence, as section 4 requires)\n' "LICENSE-moov-io-ach" "$(wc -c <"$out/LICENSE-moov-io-ach")"
