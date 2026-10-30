#!/usr/bin/env python3
"""An INDEPENDENT reader of G-SQL's table layout, written from
docs/gsql_table_design.md §2-§6 and not from src/gsql/table.c.

    table_layout.py < dump

The dump is `table_test layout DIR`'s: one `E tree key value` line (hex) per
entry of every tree. The database it describes is fixed (tests/gsql/
table_test.c, layout_run): a table `accounts` (id integer key, owner string
NOT NULL, balance money(USD) defaulting to 0), a descending non-unique index
`by_owner`, three rows, and a column `closed` added between the second row and
the third. Every expectation below is written here, by hand, from the design
text. Values are read with tests/serialize/decode_v3.py's Reader, which was
written from the value format's own text, so a layout our writer and our
reader agree on wrongly is still caught.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "serialize"))
from decode_v3 import Reader, same  # noqa: E402

problems = []
checks = 0


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        problems.append(msg)
        print("MISMATCH " + msg)


def value(b):
    r = Reader(b)
    v = r.value()
    if r.pos != len(b):
        raise ValueError("%d bytes after the value" % (len(b) - r.pos))
    return v


def s(text):
    return ("string", text.encode("utf-8"))


def ex(n):
    return ("exact", n)


USD0 = ("money", 0, 2, 840, "USD")
USD125 = ("money", 1250000, 2, 840, "USD")
UNKNOWN = ("unknown",)

trees = {}
for line in sys.stdin:
    parts = line.split()
    if not parts or parts[0] != "E":
        continue
    tree, key = bytes.fromhex(parts[1]), bytes.fromhex(parts[2])
    val = bytes.fromhex(parts[3]) if len(parts) > 3 else b""
    trees.setdefault(tree, []).append((key, val))


def tree(letter, ident):
    return b"\0" + letter + struct.pack(">Q", ident)


CATALOG, META = b"\0catalog", b"\0meta"
check(CATALOG in trees and META in trees, "the catalog and meta trees are there")

# §2: ids are shared by tables and indexes, from one counter. The table is 1,
# its index 2, so the next is 3; and a table with a primary key has no row-id
# entry of its own.
meta = dict(trees.get(META, []))
check(meta.get(b"next") == struct.pack("<Q", 3), "meta `next` is le64(3): %r" % meta.get(b"next"))
check(set(meta) == {b"next"}, "meta holds only `next` for a keyed table: %r" % sorted(meta))

# §3: the schema is one v3 record.
cat = dict(trees.get(CATALOG, []))
check(list(cat) == [b"accounts"], "the catalog holds one table, accounts: %r" % list(cat))
try:
    schema = value(cat[b"accounts"])
except Exception as e:  # noqa: BLE001
    schema = None
    check(False, "the schema does not decode: %s" % e)
if schema:
    want = ("record", {
        "format": ex(1),
        "id": ex(1),
        # created at 1; the index and the added column are two changes
        "version": ex(3),
        "columns": ("array", [
            ("record", {"name": s("id"), "kind": s("integer"), "currency": s(""),
                        "not_null": ("bool", True), "default": UNKNOWN}),
            ("record", {"name": s("owner"), "kind": s("string"), "currency": s(""),
                        "not_null": ("bool", True), "default": UNKNOWN}),
            ("record", {"name": s("balance"), "kind": s("money"), "currency": s("USD"),
                        "not_null": ("bool", False), "default": USD0}),
            ("record", {"name": s("closed"), "kind": s("date"), "currency": s(""),
                        "not_null": ("bool", False), "default": UNKNOWN}),
        ]),
        "key": ("array", [ex(0)]),
        "indexes": ("array", [
            ("record", {"name": s("by_owner"), "id": ex(2), "columns": ("array", [ex(1)]),
                        "descending": ("array", [("bool", True)]), "unique": ("bool", False)}),
        ]),
    })
    check(same(schema, want), "the schema is not the declaration:\n  got  %r\n  want %r" % (schema, want))


# §5: a format byte, a LEB128 count, then the values. The second row predates
# ADD COLUMN, so it holds THREE values and the others four; the omitted
# balance was stored as its default; the bound `nothing` was stored as unknown.
def row(b):
    r = Reader(b)
    fmt = r.u8()
    n = r.uleb()
    vals = [r.value() for _ in range(n)]
    if r.pos != len(b):
        raise ValueError("bytes after the last column")
    return fmt, vals


rows = trees.get(tree(b"r", 1), [])
check(len(rows) == 3, "the rows tree holds 3 rows: %d" % len(rows))
want_rows = [
    [ex(7), s("Ada"), USD125],
    [ex(9), s("Bob"), USD0],
    [ex(11), s("Ada"), UNKNOWN, UNKNOWN],
]
# The rows tree is in primary-key order (§4), so the dump's order is 7, 9, 11.
for i, (k, v) in enumerate(rows[:3]):
    try:
        fmt, vals = row(v)
    except Exception as e:  # noqa: BLE001
        check(False, "row %d does not decode: %s" % (i, e))
        continue
    check(fmt == 1, "row %d's format byte is %d" % (i, fmt))
    check(same(vals, want_rows[i]), "row %d is %r, want %r" % (i, vals, want_rows[i]))
keys = [k for k, _ in rows]
check(keys == sorted(keys) and len(set(keys)) == len(keys), "row keys are distinct and in order")

# §6: a non-unique index's entry is its columns, then the row key, with no
# value. `owner` is a string, keyed (gsql_storage_design.md §3.2) as 0x30, the
# bytes, then 00 00 -- and the index is DESCENDING, so every byte is inverted.
# Two rows say Ada and one Bob, so in index order Bob comes first.
entries = trees.get(tree(b"i", 2), [])
check(len(entries) == 3, "the index holds 3 entries: %d" % len(entries))
owners = []
for k, v in entries:
    check(v == b"", "a non-unique entry carries no value")
    plain = bytes(x ^ 0xFF for x in k)
    end = plain.find(b"\x00\x00", 1)
    check(plain[0] == 0x30 and end > 0, "an entry leads with an inverted string component")
    owners.append(plain[1:end].decode("utf-8", "replace"))
    rowkey = k[end + 2:]
    check(rowkey in keys, "an entry's tail is a row key that is there")
check(owners == ["Bob", "Ada", "Ada"], "the index orders owners descending: %r" % owners)

# Nothing else: no stray tree.
check(set(trees) <= {CATALOG, META, tree(b"r", 1), tree(b"i", 2)},
      "no tree beyond the four: %r" % sorted(trees))

print("ok %d checks" % checks if not problems else "FAILED %d of %d checks" % (len(problems), checks))
sys.exit(1 if problems else 0)
