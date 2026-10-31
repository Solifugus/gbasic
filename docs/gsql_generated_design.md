# G-SQL generated values: `SERIAL` and `RANDOM`

**Status:** designed and built 2026-11-01, from Matthew's rulings the same day (`gen_*` in `src/gsql/sql_exec.c`, `gsql_serial_*` in `src/gsql/table.c`; `tests/run_gsql_sql.sh`, `run_gsql_table.sh`, `run_gsql_module.sh`). It settles `gsql_design.md` §10E (categorically scoped serials), which left the semantics open, and adds random keys, which he asked for in the same discussion.

## 1. What it is

```sql
create table customers (
    id integer serial primary key,               -- 1, 2, 3 ...
    name text
);
create table task_log (
    task_id integer,
    n integer serial of (task_id),               -- 1, 2, 3 ... for each task
    note text,
    primary key (task_id, n)
);
create table txn (
    business_date date,
    branch text,
    no integer serial of (business_date, lower(branch))
);
create table users (
    id text random primary key,                  -- 22 URL-safe characters
    pin text random(6, '0123456789'),
    shuffle integer random,                      -- a stable random order
    code integer random(8)                       -- eight digits
);
```

A column may carry one **generator**. The generator chooses the value when a row is inserted without one, and the value is then stored like any other. Nothing is recomputed afterwards.

- **`SERIAL`** counts. It belongs on an `INTEGER` column only.
- **`SERIAL OF (e, ...)`** counts separately for each value of its **scope**: one or more expressions over the inserted row's own columns.
  - *Found while building it:* §10E's own example, `SERIAL OF DATE(request_time)`, cannot be written yet. G-SQL has no function that takes a datetime to its date, and `CAST(t AS DATE)` refuses a datetime. A day's scope needs a `DATE` column until such a function exists.
- **`RANDOM`** draws a value. It belongs on a `TEXT` or an `INTEGER` column.
  - **`TEXT RANDOM`** draws 22 characters from the URL-safe alphabet (`A-Z a-z 0-9 - _`), about 132 bits.
  - **`TEXT RANDOM(n)`** draws `n` characters from that alphabet.
  - **`TEXT RANDOM(n, 'chars')`** draws `n` characters from the given alphabet.
  - **`INTEGER RANDOM`** draws a non-negative 63-bit integer.
  - **`INTEGER RANDOM(n)`** draws an integer of exactly `n` digits, `1 <= n <= 18`, so that it reads as `n` digits with no leading zero.
- **The arguments are literals,** so the declaration says exactly what the column holds. A text length is 1 to 1024 characters, and an alphabet holds 2 to 4096 characters.
- **Either generator makes its column `NOT NULL`,** as PostgreSQL's `SERIAL` does. A generated value is never absent, and an explicit `NULL` is refused as for any `NOT NULL` column.
- **A generator and a `DEFAULT` cannot be combined.** Both would answer the same question.
- **A generator makes nothing unique by itself.** A key is declared in the ordinary way (`PRIMARY KEY`, `UNIQUE`). This follows §10E ("do not imply the serial alone is a global primary key") and PostgreSQL. Without such a key, the only way a duplicate can appear is a value the program supplied (§2).

**It is not:**

- **Expressions as defaults** (`DEFAULT secure_token()`). Matthew, 2026-11-01: generality not needed. It would cost a call into the interpreter per row and admit non-deterministic functions into SQL. A program can always compute any value itself and bind it as a parameter.
- A sequence object shared between tables.
- A start or step other than 1.
- An integer range for `RANDOM` (`random(1, 100)`). It is easy to add once something asks for it.

## 2. Serials

**Numbers are gapless.** Matthew's ruling (1).

- **A counter is stored per (column, scope),** holding the highest number used so far.
- **The counter is written in the same transaction as the row, and only once the row is written.** So a rollback undoes both, a crash keeps both or neither, and a refused row costs no number.
- **The owner runs one writer at a time** (`gsql_owner_design.md`). So two writers in one scope can never be handed the same number, and no locking design is needed.
- This is a promise PostgreSQL's sequences cannot make. Invoice and transaction numbers often need it.

**A number is never reused.** The counter keeps the highest number ever issued, not one more than the current maximum. So deleting the newest row and inserting again cannot give a second record the same identifier.

**A late insert takes its own scope's next number.** A backdated row for 3 October gets 3 October's next number, because that scope's counter is still kept.

**A value the program supplies is accepted.** Matthew's ruling (2), MySQL's behaviour.

- If it is greater than its scope's counter, the counter is moved up to it. Importing existing data then cannot be followed by a collision.
- A value at or below the counter leaves the counter where it is. A key, if declared, is what refuses a duplicate.

**An `UPDATE` that moves a row to another scope is refused.** Matthew's ruling (3).

- Code **`SERIAL`**, naming the serial and both scopes.
- Moving within one scope is not a change: under `lower(branch)`, changing `North` to `NORTH` leaves the row where it was.
- **The way to make the move on purpose is to set the serial in the same `UPDATE`.** That is then a supplied value, and moves the new scope's counter up if it is higher. A number that silently changes, or that silently duplicates another, is worse than being told.
- **Setting the serial in an `UPDATE`,** with or without a scope change, also moves that scope's counter up if the value is higher.

**Two scopes are the same scope when `GROUP BY` would put them in one group.** The scope is compared through the index key encoding (`gsql_storage_design.md` §3.2), so:

- `3` and `3.0` are one scope;
- `NULL` is a scope of its own;
- a value with no order (a record) cannot be a scope, and its row is refused.

**What a scope may read:**

- only its own row's columns: no subquery and no other table, as for a `CHECK` (`gsql_check_design.md` §1);
- not the serial column itself, which would be circular.

**Other tables:** a scope cannot read one. Moving a customer to another region would then change the scope of rows already numbered, in a table whose `UPDATE` cannot sensibly be refused for it. To get the same effect:

- scope by the foreign-key column itself (`serial of (customer_id)`);
- or copy the value into the row, which also records which scope the row was numbered under.

**Overflow:** a scope whose counter has reached the largest integer refuses its next row, code `SERIAL`. This follows the ruling to refuse overflowing integers.

**The order of work for one row:**

1. defaults and generated values;
2. the kind checks, `CHECK` constraints and foreign keys, each seeing the generated value;
3. the row written;
4. the counter written.

## 3. Random values

- **Drawn from the system's cryptographic generator** (`gb_secure_random`, the source behind `secure_token`). It needs no optional library, so it works in the lean build too.
- **Unbiased.** A character is drawn from one byte when the alphabet has at most 256 characters, and from two otherwise. An integer is drawn from eight bytes. A draw that would land in the uneven tail of "draw mod size" is discarded and drawn again, so every character, and every `n`-digit integer, is equally likely.
- **An alphabet must hold at least two characters, none twice.** `'AAB'` would make `A` twice as likely as `B` without saying so. A character is a codepoint, so an alphabet may hold any text.
- **A collision is drawn again.** If the row is refused as a duplicate by the primary key or a unique index that contains a generated random column, the random columns are drawn afresh, up to 32 times.
  - The draws are bounded. A nearly full space, such as six digits on a unique column, then fails with a sentence instead of looping for ever. The insert is refused as a duplicate, saying how many values the column can hold.
  - A duplicate in a key with no generated random column is refused at once, as before, since drawing again cannot help it.
- **A value the program supplies is accepted,** which is how imported data keeps its keys.
- **An `UPDATE` never draws.** Setting a random column in an `UPDATE` is an ordinary write.

## 4. Storage

**The generator is part of its column's schema** (`gsql_table_design.md` §3). The column record gains an optional `generator` field:

- `{ kind: "serial", scope: [text, ...] }`, where each scope expression is held as its canonical print and is bound against the table each time it is used, as a `CHECK`'s text is;
- or `{ kind: "random", length, alphabet }`.

The field is written only for a generated column, so a table without one is stored exactly as before.

**Counters are a tree of the table's own:** `\0q` and the table id, beside its rows (`\0r`) and indexes (`\0i`).

- The key is the column's position as two big-endian bytes, then the scope's key encoding (empty for an unscoped serial).
- The value is the high-water mark, eight bytes little-endian.
- The tree is named by the table id, so it moves with `RENAME TO` and is dropped by `DROP TABLE`. The invariant check (`gsql_table_check`) knows it.

**`ALTER TABLE ... ADD COLUMN` with a generator is refused while the table has rows.** No ordering of the existing rows is obviously right, and rows already written would read the column as absent, which a `NOT NULL` generated column cannot hold.

**An older binary does not know the field** and so does not generate. This is the position foreign keys take (`gsql_fkey_design.md` §4): a database's rules are only as strong as the binaries that write to it.

## 5. How it is tested

**parse:**

- each spelling printed back canonically;
- refused declarations:
  - `SERIAL` on a non-integer column;
  - `RANDOM` on a column that is neither text nor integer;
  - an alphabet on an integer column;
  - a length out of range;
  - an alphabet of one character, or with a character twice;
  - a generator together with `DEFAULT`;
  - two generators on one column;
  - a scope reading the serial itself, another table, or a subquery.

**semantics, hand-written answers:**

- an unscoped serial counting 1, 2, 3;
- two scopes each starting at 1;
- a composite scope;
- `3` and `3.0` as one scope, and `NULL` as one scope;
- a rollback leaving no gap;
- a refused row taking no number;
- a deleted newest row not reused;
- a supplied value moving the counter up, and one below it leaving the counter alone;
- an `UPDATE` changing scope refused, with the same `UPDATE` setting the serial accepted;
- a move within one scope accepted;
- `INSERT ... SELECT` numbering each row;
- `RENAME TO` keeping the counters;
- `DROP TABLE` and a rebuild starting again at 1;
- overflow refused;
- `ADD COLUMN` with a generator refused over rows and accepted on an empty table.

**random:**

- each length and alphabet holding to its declaration;
- an `n`-digit integer having exactly `n` digits;
- a supplied value accepted;
- a two-character alphabet on a unique column of length 1: the third insert refused after its draws, naming the space;
- a duplicate in an unrelated unique column refused at once;
- the draws not biased. An alphabet of up to 256 characters is drawn a byte at a time, so with a 200-character alphabet "byte mod 200" would make the first 56 characters twice as likely as the rest. Some 20,000 draws must put those 56 at their fair share, within a band wide enough not to flake. A plain mod is far outside that band, so the band checks the rejection step itself.

**the table layer:**

- the counter tree dropped with its table;
- the schema field decoded independently by `tests/gsql/table_layout.py`;
- the invariant check reporting an orphaned counter tree.

**the code:** `SERIAL` reaches a program as `error.details.code`.
