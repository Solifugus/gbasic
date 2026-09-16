#!/usr/bin/env bash
# The finio cookbook (docs/finio_cookbook.md) -- a tutorial that cannot lie.
# RUN / CODE / OUTPUT / COVER, all in tests/cookbook_harness.sh.
#
# NEVER SKIPS. `finio` and all five adapters are pure gBASIC; the only native
# thing under them is `xml`, and the two XML adapters reach it through
# `finio_iso20022` -- so a build without libxml2 would skip the camt and
# pain.001 recipes rather than the suite. That case is wired (the harness's
# "module compiled out" class) and has not been observed here, because this
# build has libxml2.
#
# THE RECIPES READ tests/finio/, WHICH IS DELIBERATE and is the one thing about
# this suite worth arguing. No other cookbook reads a test fixture; the
# convention is examples/fixtures/<lib>/. The convention is right where a
# fixture is small and made for the page, and wrong here: these are the files
# the five adapter suites assert against, and a second copy under examples/
# would be a second representation of one thing, which is the drift this tree
# has a tripwire for in three other places. Sharing them means the page's
# numbers ARE the validated numbers -- the property run_accounting_cookbook
# states in its own words ("page and validation share their figures") -- and a
# fixture that changes moves this page's goldens and says so, loudly, rather
# than leaving the page quietly describing a file that no longer exists.
#
# WHAT THE PAGE ASSERTS THAT PROSE COULD NOT. Three recipes are DIFFERENCES
# rather than demonstrations, and each would pass on a broken library if
# written the ordinary way:
#
#   Recipe 3  a broken amount reads `invalid` with value `unknown` -- and the
#             page prints `fld.value = 0` as FALSE beside it, because "it is
#             unknown" alone passes on a library that answers unknown for
#             everything, and reading a broken amount as ZERO is the direction
#             that actually understates a payment file.
#   Recipe 5  the same payments framed three ways (newlines, CRLF, blocked with
#             no separator at all) must give ONE credit total. Printing any one
#             framing alone proves nothing.
#             AND THE PERTURBATION CORRECTED THE PAGE. The recipe first said a
#             reader assuming newlines "reports 1 record and 0 cents"; forcing
#             `open_text` to ignore the framing it was handed does NOT produce
#             that -- it produces a REFUSAL, because framing is decided once in
#             `recognise` and recognition and reading go through the same
#             `open_text` call, so such a reader fails to recognise the blocked
#             file at all. The silent outcome is what a naive reader does and is
#             not a state this library can reach. The page says the measured
#             thing now; it had been asserting the plausible one.
#   Recipe 8  a lossy write is refused, the SAME write with allow_lossy
#             succeeds, and an impossible one stays refused WITH allow_lossy.
#             The third is the control: without it, "it refused" is satisfied
#             by a writer that refuses everything.
#
# Recipe 2's last three lines are the other one: the byte range a location
# names is sliced out of the original file and compared to what the field
# reported. That is the Axiom 2 claim checked against the file rather than
# against ourselves.
#
# PROVEN RED: CODE in isolation (a comment-only .bas edit, stdout confirmed
# byte-identical FIRST, then the suite red on CODE alone, 29 of 30); OUTPUT in
# isolation (a perturbed page block with the .bas and .out untouched, red on
# OUTPUT alone); COVER in BOTH directions (an undocumented recipe -- three
# failures, since a recipe with no entry trips the code, output and cover
# checks; and a marker naming a file that does not exist); and the library
# perturbation above, which reddens recipes 5 AND 6 and which is what corrected
# the page.
COOKBOOK=finio
RECIPE_GLOB='examples/finio_cookbook/*.bas'

. "$(dirname "$0")/cookbook_harness.sh"
