# The gBASIC discovery cookbook

Five recipes for `stdlib/discovery.bas` — **what a database estate says about
itself**, and what it cannot. Every code block below is a real program in
`examples/discovery_cookbook/`, and every output block is its committed golden;
`tests/run_discovery_cookbook.sh` fails while this page disagrees with either.
The page cannot lie.

**None of these recipes needs a database.** `discovery` reads one to build a
catalog, but the half that answers *"where did this number come from"* is a pure
function of SQL **text**, and the annotation half is a pure function of a catalog
value. That is also the honest shape of the library: reading SQL and writing
down what a person knows are things you can do to an estate you cannot currently
reach.

The governing rule, and the reason to trust an answer from it: **a fact never
wears clothes it did not earn.** What the catalog declared, what a query text
says, and what a person asserted are three different tiers and stay
distinguishable. Design and rationale:
[`discovery_design.md`](discovery_design.md).

## 1. What does this statement touch?

The floor. Give it SQL and it tells you what the statement reads and what it
writes — the two questions an impact analysis starts from. `gaps` is what it
could not resolve, reported rather than guessed.

<!--CODE:01_read_sql-->

```basic
' What a statement touches -- no database required.
load discovery

sql = ("insert into warehouse.fact_volume (deal_id, gross_vol_mmbtu) " +
       "select d.id, d.gross_vol_mmbtu from warehouse.stg_deal d " +
       "join trading.ctp k on k.code = d.ctp_code where k.is_active = 1")

r = discovery.references(sql)
print "reads:  " + join(sort(r.reads), ", ")
print "writes: " + join(sort(r.writes), ", ")
print "gaps:   " + string(count(r.gaps))
```

<!--OUT:01_read_sql-->

```
reads:  trading.ctp, warehouse.stg_deal
writes: warehouse.fact_volume
gaps:   0
```

## 2. Two reports, one column name, different numbers

The question this library exists for. Two reports both have a `total_volume`,
both are correct, and they disagree — which today costs somebody a day of
reading SQL by hand.

`explain` reports **differences only**, and says so when there are none. Note
that `name` is the *column* being compared, not the module: both reports call it
`total_volume`, which is the whole difficulty.

<!--CODE:02_why_two_numbers_differ-->

```basic
' Two reports, both called total_volume, both correct. Why do they differ?
load discovery

gross = ("select effective_date, sum(gross_vol_mmbtu) as total_volume " +
         "from warehouse.fact_volume group by effective_date")
net = ("select effective_date, sum(avail_after_pvr) as total_volume " +
       "from warehouse.fact_volume where deal_status = 'ACTIVE' group by effective_date")

' `name` is the COLUMN being compared, not the module -- both reports call it
' total_volume, which is the whole difficulty.
d = discovery.explain({ name: "total_volume", body: gross },
                      { name: "total_volume", body: net })
print "shared ancestor: " + join(sort(d.shared), ", ")
if d.identical then
    print "the two derivations are identical"
else
    print "why they differ:"
    for each why in d.differences
        print "  - " + why
    end for
end if
```

<!--OUT:02_why_two_numbers_differ-->

```
shared ancestor: warehouse.fact_volume
why they differ:
  - the expression differs: sum(gross_vol_mmbtu)   versus   sum(avail_after_pvr)
  - they are built from different columns: [gross_vol_mmbtu] versus [avail_after_pvr]
  - only the second narrows on -- where: deal_status = 'ACTIVE'
```

The third line is the one that usually ends the argument. The two numbers are
built from different columns *and* one of them narrows on a `where` — **the
predicate is part of the lineage**, not metadata about it.

## 3. Where did this column come from?

One level finer than "this table reads that table", which is too coarse to be
useful: knowing `fact_volume` reads `stg_deal` says nothing about whether
`avail_after_pvr` came from `gross_vol_mmbtu`, from `pvr_pct`, from both, or
from a constant — and a constant is one of the commonest reasons a number is
wrong and nobody can see why.

<!--CODE:03_where_did_this_column_come_from-->

```basic
' A column's derivation, read out of the SQL that produces it.
load discovery

body = ("insert into warehouse.fact_volume (deal_id, avail_after_pvr) " +
        "select s.id, s.gross_vol_mmbtu * (1 - s.pvr_pct) from warehouse.stg_deal s")

for each wrote in discovery.derivations(body)
    print "target: " + wrote.target + "  (" + wrote.kind + ")"
    for each col in wrote.columns
        print "  " + col.output + " <- " + col.expression
        print "    from: " + join(sort(col.sources), ", ")
    end for
end for
```

<!--OUT:03_where_did_this_column_come_from-->

```
target: warehouse.fact_volume  (insert)
  deal_id <- s.id
    from: id
  avail_after_pvr <- s.gross_vol_mmbtu * (1 - s.pvr_pct)
    from: gross_vol_mmbtu, pvr_pct
```

A column with a writer and **no sources** is a step with an empty source list,
which is a different fact from having no writer at all.

## 4. What a person knows and the database does not

Four tables called `customer`, and nothing in any schema says which one the
business actually uses. That is not a gap in the reader — **it is not in the
database at all**, and no amount of scanning will find it.

`annotate` is where it gets written down. Seven kinds: `means`, `unit` and
`owner` for documentation, `authority` and `derived_from` for analysis and
lineage, `synonyms` and `not_modelled` for a question asked in English.

<!--CODE:04_what_a_person_knows-->

```basic
' The third tier of fact: what a person knows and the database does not.
load discovery

' A catalog `scan` would have produced -- written out here so the recipe needs
' no database.
cat = { source: "erp",
        tables: { "erp.sales.customer":    { name: "customer",    type: "TABLE" },
                  "erp.sales.customer2":   { name: "customer2",   type: "TABLE" },
                  "erp.sales.customer_new": { name: "customer_new", type: "TABLE" } },
        columns: { "erp.sales.customer2.balance": { name: "balance" } },
        primary_keys: [], edges: [] }

' Nothing in any schema says which of the three the business actually uses.
' That is not a gap in the reader -- it is not in the database at all.
cat = discovery.annotate(cat, {
        "erp.sales.customer":     { authority: "superseded", means: "the 2019 customer master" },
        "erp.sales.customer2":    { authority: "live", means: "the customer master", owner: "sales ops" },
        "erp.sales.customer_new": { authority: "archive", means: "an abandoned migration" },
        "erp.sales.customer2.balance": { means: "outstanding balance", unit: "USD" } })

for each id in sort(keys(discovery.notes_of(cat, "authority")))
    print discovery.notes_of(cat, "authority")[id] + "  " + id
end for
print ""
print "and every note says who said so: " + cat.notes["erp.sales.customer2"].known_by
```

<!--OUT:04_what_a_person_knows-->

```
superseded  erp.sales.customer
live  erp.sales.customer2
archive  erp.sales.customer_new

and every note says who said so: supplied
```

**Every note carries `known_by: "supplied"`**, and that is the load-bearing
part. An inferred fact must never wear the clothes of a declared one — and a
supplied fact is the same hazard from the other direction, worse in one respect:
it is the only tier nobody can check.

A note about an object **not in the catalog** is refused by name. It is a typo,
or a note left behind by a table that was dropped, and answering from it would
describe something that is not there.

## 5. A data dictionary, generated

Documentation that is *derived* rather than maintained: the catalog supplies the
shape, the notes supply the meaning, and the page is regenerated rather than
edited.

<!--CODE:05_a_data_dictionary-->

```basic
' Generated documentation: the catalog plus what a person wrote down.
load discovery

cat = { source: "erp",
        tables: { "erp.sales.invoice": { name: "invoice", type: "TABLE" } },
        columns: { "erp.sales.invoice.total":    { name: "total",    type_name: "decimal" },
                   "erp.sales.invoice.raised_on": { name: "raised_on", type_name: "date" },
                   "erp.sales.invoice.ref":      { name: "ref",      type_name: "varchar" } },
        primary_keys: [], edges: [] }
cat = discovery.annotate(cat, {
        "erp.sales.invoice":          { means: "one invoice as issued", owner: "finance" },
        "erp.sales.invoice.total":    { means: "invoice total, tax included", unit: "USD" },
        "erp.sales.invoice.raised_on": { means: "the date the invoice was issued" } })

means = discovery.notes_of(cat, "means")
units = discovery.notes_of(cat, "unit")
for each id in sort(keys(cat.columns))
    line = "  " + cat.columns[id].name + " (" + cat.columns[id].type_name + ")"
    if has(means, id) then
        line = line + " -- " + means[id]
    else
        ' A column nobody has described is worth SEEING, not hiding.
        line = line + " -- (undocumented)"
    end if
    if has(units, id) then
        line = line + " [" + units[id] + "]"
    end if
    print line
end for
```

<!--OUT:05_a_data_dictionary-->

```
  raised_on (date) -- the date the invoice was issued
  ref (varchar) -- (undocumented)
  total (decimal) -- invoice total, tax included [USD]
```

Note the third column. **A column nobody has described is worth seeing**, not
hiding — a dictionary that silently omits what it does not know reads as
complete, which is the one thing it must not do.

## Where to go next

- **A question asked in English** over the same estate:
  [`nlq_cookbook.md`](nlq_cookbook.md), which reads these notes rather than
  keeping its own copy.
- **Reading a real database**: `discovery.scan(connection, {source})` needs
  `odbc`; the identity rules it follows were measured against four drivers and
  are in [`discovery_design.md`](discovery_design.md).
- **What is deliberately absent**: inference of any kind. An inferred
  relationship is the result of a *search*, and a search over a 500-table estate
  is ~50 million candidate pairs where coincidences are a certainty rather than
  a risk. Inference arrives with a null model or not at all.
