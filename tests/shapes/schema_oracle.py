#!/usr/bin/env python3
"""The oracle half of run_shapes.sh's schema tier (docs/shapes_design.md §6.2).

WHAT THE SCHEMA ADMITS, THE LOCK CONVERTS. A model told json_schema(s) answers
within it, and its answer must then go into the shape with nothing refused.
This generates documents for tests/shapes/schema_check.bas's `everything`
shape (valid values for each kind, and near misses: forms RFC 3339 allows that
gBASIC's locks do not, too many digits, units out of order, wrong JSON types,
fields missing and added), decides from the schema gBASIC printed which ones
it admits, and then holds gBASIC's verdict on each: every admitted document
must be "ok". The checker is written here from JSON Schema draft 2020-12 (type,
properties, required, additionalProperties, pattern) and RFC 3339 (the date
and date-time formats, asserted, calendar included), not from gBASIC.

    schema_oracle.py generate SCHEMA.json DOCS.jsonl [--formats-only]
    schema_oracle.py judge SCHEMA.json DOCS.jsonl VERDICTS.txt [--formats-only]

--formats-only is the control: patterns ignored, formats alone asserted, as a
schema without them would be read. It must find admitted documents gBASIC
refuses, or the patterns carry no weight and the tier proves nothing.
"""
import datetime
import json
import random
import re
import sys

COUNT = 3000
UNITS = ["year", "month", "week", "day", "hour", "minute", "second"]


# ---- JSON Schema, the subset json_schema writes --------------------------------

def is_type(v, t):
    if t == "object":
        return isinstance(v, dict)
    if t == "array":
        return isinstance(v, list)
    if t == "string":
        return isinstance(v, str)
    if t == "boolean":
        return isinstance(v, bool)
    if t == "number":
        return isinstance(v, (int, float)) and not isinstance(v, bool)
    if t == "integer":
        return isinstance(v, int) and not isinstance(v, bool)
    if t == "null":
        return v is None
    raise ValueError("no such JSON Schema type: %r" % t)


def full_date(text):
    m = re.fullmatch(r"(\d{4})-(\d{2})-(\d{2})", text)
    if not m:
        return False
    y, mo, d = (int(g) for g in m.groups())
    if not 1 <= mo <= 12:
        return False
    if y == 0:
        # RFC 3339 allows year 0000; Python's date does not, so check by hand
        leap = True
        days = [31, 29 if leap else 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][mo - 1]
        return 1 <= d <= days
    try:
        datetime.date(y, mo, d)
        return True
    except ValueError:
        return False


def date_time(text):
    # RFC 3339 §5.6: full-date "T" full-time, T and Z either case, a
    # fraction allowed, the offset required; second 60 for a leap second
    m = re.fullmatch(r"(\d{4}-\d{2}-\d{2})[Tt](\d{2}):(\d{2}):(\d{2})(\.\d+)?([Zz]|[+-](\d{2}):(\d{2}))", text)
    if not m or not full_date(m.group(1)):
        return False
    h, mi, s = int(m.group(2)), int(m.group(3)), int(m.group(4))
    if h > 23 or mi > 59 or s > 60:
        return False
    if m.group(7) is not None and (int(m.group(7)) > 23 or int(m.group(8)) > 59):
        return False
    return True


FORMATS = {"date": full_date, "date-time": date_time}


def admits(schema, v, formats_only=False):
    t = schema.get("type")
    if t is not None and not is_type(v, t):
        return False
    if "format" in schema and isinstance(v, str):
        if not FORMATS[schema["format"]](v):
            return False
    if "pattern" in schema and isinstance(v, str) and not formats_only:
        if not re.search(schema["pattern"], v):
            return False
    if isinstance(v, dict):
        props = schema.get("properties", {})
        for k in schema.get("required", []):
            if k not in v:
                return False
        for k, sub in v.items():
            if k in props:
                if not admits(props[k], sub, formats_only):
                    return False
            elif schema.get("additionalProperties") is False:
                return False
    known = {"type", "format", "pattern", "properties", "required", "additionalProperties"}
    extra = set(schema) - known
    if extra:
        raise ValueError("a keyword this checker does not know: %s" % sorted(extra))
    return True


# ---- the documents -------------------------------------------------------------

def digits(r, lo, hi):
    n = r.randint(lo, hi)
    return "".join(r.choice("0123456789") for _ in range(n))


def a_date(r):
    y = r.choice([0, 1, r.randint(1, 9999), 2024, 2023, 9999])
    mo = r.randint(1, 12)
    d = r.randint(1, 31)
    return "%04d-%02d-%02d" % (y, mo, d)


def an_offset(r):
    return r.choice(["Z", "%s%02d:%02d" % (r.choice("+-"), r.randint(0, 23), r.randint(0, 59))])


def a_value(r, kind, near_rate):
    """A value for a field of this kind: one the schema should admit, or, at
    near_rate, a near miss."""
    near = r.random() < near_rate
    if kind == "number":
        if near:
            return r.choice(["12", True, None, [1], {"x": 1}, ""])
        return r.choice([r.randint(-10**6, 10**6), r.uniform(-1e9, 1e9), 0, -0.0, 1e300, 2**53 + 1])
    if kind == "boolean":
        return r.choice(["true", 1, 0, None]) if near else r.choice([True, False])
    if kind == "date":
        if near:
            return r.choice(["2026-1-5", "2026-01-05T00:00:00Z", "20260105", "2026-02-30", "2026-13-01", 20260105])
        return a_date(r)
    if kind == "datetime":
        base = "%sT%02d:%02d:%02d" % (a_date(r), r.randint(0, 23), r.randint(0, 59), r.randint(0, 59))
        if near:
            return r.choice([base + ".5Z", base.replace("T", "t") + "z", base, base.replace("T", " ") + "Z",
                             base[:-2] + "60Z", base + "+24:00"])
        return base + an_offset(r)
    if kind == "time":
        base = "%02d:%02d" % (r.randint(0, 23), r.randint(0, 59))
        if near:
            return r.choice([base + ":00Z", base + ":00+02:00", base + ":00.5", "24:00:00", base[:-1], "7:05"])
        return base + r.choice(["", ":%02d" % r.randint(0, 59)])
    if kind == "duration":
        if near:
            return r.choice(["3days", "3 fortnights", "", "1.5 days", "2 hours 3 days", "1 day 1 day",
                             "P3D", 90, "3  days"])
        picked = sorted(r.sample(range(7), r.randint(1, 7)))
        parts = []
        for k in picked:
            n = r.choice([0, 1, r.randint(2, 999), r.randint(10**8, 999999999)])
            sign = "-" if r.random() < 0.2 else ""
            plural = "s" if r.random() < 0.6 else ""
            parts.append("%s%d %s%s" % (sign, n, UNITS[k], plural))
        return " ".join(parts)
    if kind.startswith("money"):
        whole, scale = {"money(USD)": (12, 6), "money(JPY)": (14, 4), "money(BHD)": (11, 7),
                        "money(CLF)": (10, 8)}[kind]
        if near:
            return r.choice([digits(r, whole + 1, whole + 3), "1." + digits(r, scale + 1, scale + 2), "1e3",
                             "12.", ".5", 12.5, "+1", "1,000"])
        text = (r.choice(["", "-"]) + digits(r, 1, whole))
        if r.random() < 0.7:
            text += "." + digits(r, 1, scale)
        return text
    if kind == "text":
        return r.choice([7, None]) if near else r.choice(["", "plain", "ü\u0000\"\\", "line\nbreak", "😀"])
    if kind == "anything":
        return r.choice([None, 1, "x", [1, {"a": 2}], {"b": [True]}, False])
    raise ValueError(kind)


FIELDS = [("n", "number"), ("b", "boolean"), ("d", "date"), ("dt", "datetime"), ("t", "time"),
          ("dur", "duration"), ("usd", "money(USD)"), ("jpy", "money(JPY)"), ("bhd", "money(BHD)"),
          ("clf", "money(CLF)"), ("s", "text"), ("anything", "anything"), ("later", "date")]


def a_document(r):
    # half the documents hold no near miss at all, so enough of them are
    # admitted whole; the rest miss at about one field in three
    near_rate = 0.0 if r.random() < 0.5 else 0.35
    doc = {}
    for name, kind in FIELDS:
        doc[name] = a_value(r, kind, near_rate)
    inner = {"label": a_value(r, "text", near_rate)}
    if r.random() < 0.5:
        inner["on_day"] = a_value(r, "date", near_rate)
    if r.random() < 0.05:
        inner["stray"] = 1
    doc["inner"] = inner if r.random() > 0.05 else "not an object"
    roll = r.random()
    if roll < 0.05:
        del doc[r.choice(["n", "d", "usd", "inner"])]          # required, missing
    elif roll < 0.10:
        del doc[r.choice(["anything", "later"])]               # a default covers it
    elif roll < 0.15:
        doc["extra"] = 1                                       # not declared
    return doc


def generate(schema_path, docs_path):
    r = random.Random(20261126)
    with open(docs_path, "w", encoding="utf-8") as out:
        for _ in range(COUNT):
            out.write(json.dumps(a_document(r), ensure_ascii=False) + "\n")


def judge(schema_path, docs_path, verdicts_path, formats_only):
    schema = json.load(open(schema_path, encoding="utf-8"))
    docs = [json.loads(line) for line in open(docs_path, encoding="utf-8")]
    verdicts = [line.rstrip("\n") for line in open(verdicts_path, encoding="utf-8")]
    if len(docs) != len(verdicts):
        print("FAIL: %d documents and %d verdicts" % (len(docs), len(verdicts)))
        return 1
    admitted = unsound = tolerated = 0
    per_field = {name: 0 for name, _ in FIELDS}
    shown = 0
    for doc, verdict in zip(docs, verdicts):
        if admits(schema, doc, formats_only):
            admitted += 1
            if verdict != "ok":
                unsound += 1
                if shown < 5:
                    print("  admitted but refused: %s -> %s" % (json.dumps(doc, ensure_ascii=False)[:300], verdict[:300]))
                    shown += 1
        elif verdict == "ok":
            tolerated += 1
        # how many documents carried a value of each field the field's own schema admits
        for name, _ in FIELDS:
            sub = schema["properties"][name]
            if name in doc and admits(sub, doc[name], formats_only):
                per_field[name] += 1
    print("documents %d, admitted %d, admitted but refused %d, refused by the schema but converted %d"
          % (len(docs), admitted, unsound, tolerated))
    print("admitted values per field: " + ", ".join("%s %d" % kv for kv in per_field.items()))
    if formats_only:
        return 0 if unsound > 0 else 1
    floor_ok = admitted >= 1000 and min(per_field.values()) >= 2000
    if not floor_ok:
        print("FAIL: too few admitted documents or values to say anything")
    return 0 if unsound == 0 and floor_ok else 1


if __name__ == "__main__":
    formats_only = "--formats-only" in sys.argv
    argv = [a for a in sys.argv[1:] if a != "--formats-only"]
    if argv[0] == "generate":
        generate(argv[1], argv[2])
        sys.exit(0)
    sys.exit(judge(argv[1], argv[2], argv[3], formats_only))
