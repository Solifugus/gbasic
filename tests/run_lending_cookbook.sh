#!/usr/bin/env bash
# The lending cookbook (docs/lending_cookbook.md) -- a tutorial that cannot lie.
#
# A cookbook's failure mode is not being wrong when written, it is being wrong
# six months later while still reading as authoritative. The four tiers below
# (RUN / CODE / OUTPUT / COVER, all in tests/cookbook_harness.sh) remove the
# gap between the page and the product in both directions: the page owns
# neither the code nor the output it shows.
#
# Never skips: `lending` is pure gBASIC over core `money`, and recipe 7's
# ledger tier reaches only `accounting`, which is pure gBASIC too.
#
# WHAT THIS PAGE IS FOR, beyond documentation. `lending`'s subject is
# CONVENTIONS -- accrual basis, payment waterfall, day count -- and every one
# of them fails by producing an ordinary-looking balance rather than an error.
# The recipes are therefore built as DIFFERENCES wherever a convention is in
# play (recipes 3 and 4 run the same loan and the same payments under both
# settings and show the answers parting company), which is the only shape that
# can distinguish a library that honours a declaration from one that ignores
# it. The library's own first version prorated the amortized basis by days,
# making the two algebraically identical; the declaration was decorative and
# nothing said so.
COOKBOOK=lending
RECIPE_GLOB='examples/lending_cookbook/*.bas'

. "$(dirname "$0")/cookbook_harness.sh"
