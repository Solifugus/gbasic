#!/usr/bin/env bash
# xml_bigfile_gen.sh — synthesize a large 13F-style information table for the
# WP-XML-6 constant-memory streaming test. The output is DELIBERATELY NOT checked
# in: it is generated on demand (>=100 MB by default) and deleted by the harness.
#
# Usage:
#   tools/xml_bigfile_gen.sh OUTPATH [TARGET_BYTES]
#
# The document is a namespaced <informationTable> with many <infoTable> records,
# structurally identical to examples/fixtures/edgar/f13_infotable_sample.xml, so
# the same skip_to("infoTable")/subtree windowing loop streams it. Sizing is
# approximate (counts characters as it grows past TARGET_BYTES, then closes the
# root), which is all the constant-memory assertion needs.
set -euo pipefail

out="${1:?usage: xml_bigfile_gen.sh OUTPATH [TARGET_BYTES]}"
target="${2:-104857600}"   # 100 MiB default

awk -v target="$target" 'BEGIN {
    print "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    print "<informationTable xmlns=\"http://www.sec.gov/edgar/document/thirteenf/informationtable\">"
    sz = 130
    n = 0
    while (sz < target) {
        n++
        rec = sprintf("  <infoTable>\n" \
            "    <nameOfIssuer>Issuer %d</nameOfIssuer>\n" \
            "    <titleOfClass>COM</titleOfClass>\n" \
            "    <cusip>%09d</cusip>\n" \
            "    <value>%d</value>\n" \
            "    <shrsOrPrnAmt>\n" \
            "      <sshPrnamt>%d</sshPrnamt>\n" \
            "      <sshPrnamtType>SH</sshPrnamtType>\n" \
            "    </shrsOrPrnAmt>\n" \
            "  </infoTable>", n, n, n * 100, n * 10)
        print rec
        sz += length(rec) + 1
    }
    print "</informationTable>"
    printf("xml_bigfile_gen: wrote %d records, ~%d bytes\n", n, sz) > "/dev/stderr"
}' > "$out"
