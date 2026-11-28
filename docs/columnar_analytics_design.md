# Columnar analytics: what is slow, measured, and what to build

**Status: Partial.** §5 A is built (tests/run_frame.sh); B and C do not exist. §2–§4 are measurements, rerun
with `tools/bench_analytics/run.sh WORKDIR`.

Matthew, 2026-11-27: "the item that bothers me most is Numeric speed on large
arrays. We should look into that. Personally, I find pandas has too big of a
learning curve. I feel like gBASIC has good foundations to start address this."
The order agreed: measure first (an analytics benchmark, gBASIC four ways
against pandas and polars), then this note (where the time goes, a packed
column kind, and whether it needs any grammar, which decides before or after
1.0).

## 1. The answer first

- **gBASIC's answers are right and exact**: every task agrees with pandas and
  polars at every size, and gBASIC's sums are exact decimals where theirs are
  floats rounded to the cent.
- **Loading is most of the cost.** Reading 1M rows into columns takes 4.3 s, against pandas'
  0.46 s and polars' 0.045 s. The tasks after it are 6–80 times pandas
  (one, month, is faster than pandas).
- **frame.bas cannot be used past about 10,000 rows** for grouping by
  something with many values, or for a join: its `summarize` and `join` are
  O(n × groups) and O(n × m). That is an algorithm, not the language, and
  it was fixed the same day (§5 A).
- **The time is the interpreter's cost per operation** (about half a
  microsecond a statement), not how a value is stored, and the compiler does
  not change it (§4).
- **No grammar is needed** for any of the fixes (§6). So it can follow 1.0,
  if 1.0 keeps one door open (§6).

## 2. What was measured

Orders, deterministic (tools/bench_analytics/gen.py): `id, customer (1–10000),
region (8), day (2025–2026), qty (1–20), price (1.00–500.00, two decimals)`, and
10,000 customers with one of 5 segments. Tasks, each answered to the cent:

| task | asks |
|---|---|
| filter | region West and qty ≥ 10: count, and sum of qty × price |
| group | per region: count and sum |
| bycust | per customer (10,000 groups): sum, the top 5 |
| top | the 10 largest orders by qty × price |
| join | orders joined to customers: sum per segment |
| month | orders per month of `day` |

Implementations: **loop** (tools/bench_analytics/loop.bas: `csv.records` into
arrays, loops, a record as the hash map), the same compiled
(`GBASIC_COMPILE=all`), **gsql** (gsqlbench.bas: loaded by `db.insert` in
batches of 10,000, each task one query), **frame** (framebench.bas: the verbs
of stdlib/frame.bas), **pandas** 3.0.3 and **polars** 2.2.0 as their users
would write them (py_bench.py). 22-core x86-64, gBASIC at c87c2102,
2026-11-28. A gBASIC task is one process, load included, best of three (one
run at 10M); a Python task is timed in its process, best of three, load apart.

## 3. Results

### 3.1 One million rows

Seconds. For the loop, a task's own cost is its process less `load`'s.

| | load | filter | group | bycust | top | join | month | peak memory |
|---|---|---|---|---|---|---|---|---|
| polars | 0.045 | 0.0013 | 0.0034 | 0.013 | 0.025 | 0.021 | 0.070 | 501 MB |
| pandas | 0.46 | 0.044 | 0.061 | 0.019 | 0.27 | 0.12 | 2.79 | 276 MB |
| loop | 4.34 | 0.27 | 1.61 | 1.48 | 3.53 | 1.70 | 1.56 | 413 MB (906 for top) |
| loop compiled | 5.10 | 0.28 | 1.15 | 0.89 | 3.07 | 0.92 | 0.82 | 482 MB |
| gsql (a query, its process included) | build 14.7 | 0.63 | 0.98 | 1.10 | 1.49 | 3.54 | 1.06 | 32 MB |

Loop against pandas, task by task: 6×, 26×, 78×, 13×, 15×, and 0.56× (pandas'
`strftime` is slow; polars' is not).

### 3.2 Ten million rows (one run each)

| | load | filter | group | bycust | top | join | month | peak memory |
|---|---|---|---|---|---|---|---|---|
| polars | 0.22 | 0.012 | 0.027 | 0.10 | 0.45 | 0.26 | 0.70 | 2.7 GB |
| pandas | 4.70 | 0.46 | 0.60 | 0.12 | 3.25 | 1.35 | 28.6 | 2.0 GB |
| loop (whole process) | 53.4 | 53.9 | 59.0 | 56.7 | 80.3 | 51.2 | 50.1 | 3.9 GB (8.8 for top) |
| gsql | build 144 | 6.35 | 10.4 | 10.9 | 15.5 | 37.8 | 11.2 | 32 MB |

At 10M the loop's own task costs are within one run's noise of its load, so
§3.1 is the place to read them. The loop holds about 390 bytes a row in
columns (six values), about 65 a value; as an array of records, 825 a row.

### 3.3 frame.bas

At 100,000 rows: load 8.51 s (the loop's 0.41: `read_csv` infers each column
by testing every cell), then filter +1.6 s, group +2.6, top +3.1. At 10,000
rows `bycust` took **83 s** and `join` did not finish in **300 s** (pandas: 11 ms
and 12 ms). `summarize` scans every row once per group, and finds a group by
`contains` on a list of keys; `join` compares every pair of rows.

## 4. Where the time goes

`--profile` of the loop's group task at 100,000 rows: 85% loading (43%
`append`, 22% `csv.records`, 9% `number()`), 15% the grouping. `append` is not
slow in itself: a million appends to a local array take 0.48 s, through a
record field 0.53 s. The loader runs six statements a row where a loader of
records runs one, and loading the same file as an array of records takes 2.0 s
at 1M, against 4.3 s as columns (and 825 MB against 413). The existing bracket
filter, `rows[.region = "West" and .qty >= 10]`, is 0.18 s over those 1M
records, faster than the hand loop's 0.27 s.

So the cost is the interpreter's per-operation overhead, paid once per value:
roughly half a microsecond a statement or builtin call. The compiled lane
gains on the grouping loops (bycust 1.48 → 0.89 s, join 1.70 → 0.92, month
1.56 → 0.82) and loses on load (4.34 → 5.10, LLVM's own time and 70 MB), since
what it lowers is control flow and arithmetic on numbers in slots, while a row
here is record lookups, text and exact decimals, which stay runtime calls.

Pandas and polars pay that overhead once per COLUMN: one call sums a million
values in C. That, not a faster interpreter, is the difference to close.

## 5. What to build, in order

**A. frame.bas by hash (library; no grammar; any time). BUILT 2026-11-28**
(tests/run_frame.sh): at 10,000 rows `bycust` 83 s → 1.0 s, `join` past 300 s →
1.3 s, `month` 8.9 s → 1.2 s, each process mostly `read_csv`; at 25,000 →
100,000 rows in as many groups the time grows 3.8x where the scans grew 19x;
the old code answers the suite's 14 checks identically. As proposed: `summarize` keyed by
a record (`groups[sig]`, as loop.bas does) and `join` by an index of the right
frame's keys: both O(n). Expected from loop.bas's own times: `bycust` at 10,000
rows from 83 s to well under a second, and `join` from beyond 300 s to the
same. The smallest change with the largest effect, and it fixes the library
people are told to use.

**B. Reading a file into typed columns natively (builtin; no grammar).**
`csv.columns(f, shape)`: a record of columns, each cell converted by the
shape's field in C, one call for the file. Today's load is 55–94% of the loop's
time at 1M (94% for filter, 55% for top). Target: within a few times pandas' 0.46 s. frame.read_csv would
use it.

**C. A packed column kind (value kind; no grammar).** The vector kind
(docs/vectors_design.md) generalised: an immutable, shared, packed column of
one element type, int64, float64, exact decimal at a declared scale (an int64
of the scale's units, as money keeps minor units, so exact at C speed), date,
or text (dictionary-coded, as polars' categorical). Then, each a builtin or an
operator on the kind, the way vectors have them:

- elementwise `+ - * /` between columns and with a scalar (`qty * price`);
- `sum`, `mean`, `min`, `max`, `count` over a column in C (the names exist);
- a TABLE of columns answering the brackets and braces gBASIC already has for
  arrays of records and G-SQL paths: `t[.region = "West", .qty >= 10]` as one
  scan of the two columns, `t{qty, price}` as two shared columns;
- group-by and join by hash, as builtins taking column names and an
  aggregate per output (`{ total: ["sum", "amount"] }`), since gBASIC has no
  inline functions.

With C, the measured gap at 1M is mostly closed for filter, group and the
arithmetic; whether it reaches pandas is a measurement for its increment.

## 6. The grammar question

None of A–C needs new grammar:

- the condition and column forms, `t[.qty >= 10]` and `t{a, b}`, already parse
  and already mean "rows where" and "these columns" (arrays of records, G-SQL
  paths), so a table kind gives them a fast meaning, not a new one;
- elementwise arithmetic is an operator on a value kind, as vectors already do;
- aggregates and group-by are calls.

**One door to keep open at 1.0:** comparison. `col = 5` cannot be elementwise,
because `=` on compound values is a deep comparison everywhere (contains,
find, unique and watchers depend on it). Elementwise comparison therefore lives
only inside brackets, where `.qty >= 10` is already per row. If the 1.0
compatibility inventory says brackets with a `.column` condition are "an
array of records' filter", it should say "a filter of rows", so a table of
columns can answer them after 1.0 without changing what they mean.

So this can come before or after 1.0; that is Matthew's call. A could be done
at any time.

## 7. Observations on the way

- **G-SQL** holds its memory flat at 32 MB at every size, keeps the data, and
  answers a query on 10M rows in 6–16 s; its join, 37.8 s at 10M and 3.5 s at
  1M, is the slowest query. How its planner joins (hash, merge or nested with
  an index) was not looked at here, and is the next thing to look at there.
  A one-off question on a file is faster in the loop (4.6 s at 1M against a
  14.7 s build); a repeated one is faster in G-SQL after the build.
- **Three documentation gaps and a `join` question** were met writing this
  (DOGFOOD 2026-11-28; ledger rows 11 and 12): `--doc` has no `csv.records` and
  no `sort` options, statistics_design.md shows frame calls that do not parse,
  and `join` refuses numbers.
- **No sub-second clock** in gBASIC (by design), so every gBASIC time here is
  measured from outside the process.
