#!/usr/bin/env bash
# The finio tutorial (docs/finio_tutorial.md) -- ONE CONTINUOUS PROBLEM worked
# end to end, on the SAME four tiers as a cookbook: RUN / CODE / OUTPUT / COVER,
# all in tests/cookbook_harness.sh.
#
# A TUTORIAL IS VERIFIED HERE AND THAT IS NEW. `docs/gui_tutorial.md` and
# `docs/edgar_tutorial.md` are checked by NOTHING -- their code blocks are prose,
# and run_doc_examples.sh covers only four pages, none of them a tutorial. A
# tutorial's code is copied by readers exactly as a cookbook's is, so holding it
# to a lower standard is backwards. The harness grew three path overrides
# (COOKBOOK_DOC/_DIR/_SYNC) rather than gaining a second copy, since a second
# harness is the drift that file was written to end.
#
# WHY THE STEPS READ tests/finio/foreign/ AND NOTHING ELSE. Every step on the
# page reads the SAME ten ACH files, from moov-io/ach -- files this project did
# not write. That is the whole argument for the page: a generator we wrote shares
# our misunderstandings, so a fixture written here can only confirm them, and
# these ten found three defects no fixture in this tree could. Steps 2 and 3 are
# the SHAPE OF THOSE FINDINGS rather than a demonstration arranged to succeed --
# which is also why step 2 reports 2 of 10 clean and does not hide it.
#
# THE ASSERTIONS THAT ARE NOT DEMONSTRATIONS:
#
#   Step 3  the close-up prints EVERY field of a 55-byte file_control, so the
#           `unknown` one is shown BESIDE seven that read correctly. Printing
#           the unknown alone would be satisfied by a reader that answers
#           unknown for everything, and the direction that actually hurts --
#           a partial field read as a SHORTER VALUE -- is the one the page
#           names and Axiom 7 refuses.
#   Step 5  read-then-write-unchanged is compared to the bytes that ARRIVED,
#           not to a second write. A writer that normalised would round-trip
#           against itself perfectly.
#           And `records that changed: 1` is a second claim in the same line:
#           a gBASIC record is a value, so set_field cannot reach the caller's
#           document -- the original is compared against the edited one record
#           by record, which is what proves both that exactly one moved and
#           that the input was not mutated.
#
# THE OVERRIDES CANNOT SILENTLY FALL BACK, and that is the new failure mode the
# harness change introduces: a tutorial suite that quietly re-checked the
# COOKBOOK would print fifteen PASS lines and assert nothing about this page.
# MEASURED rather than reasoned -- with COOKBOOK_DOC and COOKBOOK_DIR ignored,
# this suite reports PASS=0 and ten failures, because the recipe glob still
# names the tutorial's files and neither the page nor the directory holds them.
#
# PROVEN RED: CODE in isolation (a comment-only .bas edit, stdout confirmed
# byte-identical FIRST); OUTPUT in isolation (a perturbed page block, files
# untouched); COVER in both directions; and the override fallback above.
#
# Never skips: `finio` and `finio_nacha` are pure gBASIC, and this page touches
# neither XML adapter, so there is no native dependency under it at all.
COOKBOOK=finio
COOKBOOK_DOC=docs/finio_tutorial.md
COOKBOOK_DIR=examples/finio_tutorial
COOKBOOK_SYNC=tools/sync_finio_tutorial.sh
COOKBOOK_SUITE=run_finio_tutorial
RECIPE_GLOB='examples/finio_tutorial/*.bas'

. "$(dirname "$0")/cookbook_harness.sh"
