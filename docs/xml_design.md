# XML Library — Design

Status: **design proposal; nothing built.** A general-purpose XML module for
gBASIC: parse small documents into plain record trees, stream arbitrarily
large documents through a pull cursor in constant memory, encode records back
to XML, and (bonus, same engine) leniently parse HTML to a tree. First
customers: `insiders.bas` (Form 4), `holdings.bas` (13F), `ownership.bas`
(13D/G), and `mdna.bas` (HTML extraction) from `edgar_design.md` — but the
module is deliberately general and knows nothing about the SEC.

---

## 1. Why this is a C module (the earn-it case, crossed)

The statistics library's "compositions in gBASIC" rule was upheld because its
candidates (eigensolvers, CDF engines) were self-contained numeric kernels. XML
is the opposite case on every axis:

1. **Streaming requirement.** The stated goal is windowing through files too
   large to load — XBRL instance documents run to hundreds of MB. A pull
   lexer over chunked reads in a tree-walking interpreter would be both slow
   and the largest pure-gBASIC program in the project, all to reimplement a
   solved problem.
2. **Correctness surface.** Namespaces, the five predefined entities plus
   character references, encodings, CDATA, DOCTYPE skipping, and the
   *security* failure modes (§7) are exactly the "mature necessary
   complexity" a battle-tested C library encodes.
3. **Precedent.** sqlite, pg, webclient, and gui already establish the
   pattern: native module over a system library, compiled in when available,
   clean runtime error otherwise.

**Engine: libxml2.** Chosen over expat because libxml2 provides all three
surfaces this design needs from one dependency — `xmlReader` (a *pull* parser,
which maps directly onto a gBASIC cursor with no callback contortions), the
tree API (small-document parsing), and `htmlRead*` (lenient HTML → tree, which
`mdna.bas` needs anyway). expat is leaner but is SAX-only and XML-only; the
pull adapter and tree builder would have to be hand-written, and HTML would
need a second dependency. libxml2 is ubiquitous (every Linux distro, macOS
system library).

---

## 2. Value mapping: nodes are records

No new value kind. An element is a record:

```basic
{
    name:     "infoTable",          ' local name (namespace prefix stripped)
    qname:    "ns1:infoTable",      ' qualified name as written
    ns:       "http://...",         ' namespace URI, or nothing
    attrs:    { cik: "0000320193" } ' attribute record (values are strings)
    children: [ ... ]               ' ordered: element records and strings
}
```

- **Children are an ordered list** whose entries are element records or plain
  strings (text). This represents mixed content honestly while keeping
  data-oriented XML trivial to walk.
- **Text handling:** adjacent text/CDATA runs are coalesced into one string;
  entity and character references arrive already decoded. By default,
  whitespace-only text between elements is **dropped** (right for data XML —
  Form 4, 13F, config files); `xml.parse(text, keep_space)` retains it for
  document-oriented XML.
- Comments and processing instructions are dropped. The DOCTYPE is skipped,
  never processed (§7).
- All strings are UTF-8: libxml2 transcodes from the document's declared
  encoding, so gBASIC never sees UTF-16 bytes. Conveniently, XML 1.0 forbids
  the NUL character outright, so gBASIC's NUL-terminated-string caveat cannot
  bite on well-formed input.

---

## 3. Small-document API (tree)

```basic
load xml

doc = xml.parse(text)               ' string -> root element record
doc = xml.parse_file(path)          ' convenience; whole file
```

Navigation helpers (all pure functions over the record shape, so they also
work on hand-built records):

```basic
n  = xml.find(doc, "reportingOwner/reportingOwnerId/rptOwnerCik")
ns = xml.find_all(doc, "nonDerivativeTable/nonDerivativeTransaction")
s  = xml.text(n)                    ' concatenated descendant text, trimmed
v  = xml.attr(n, "id")              ' attribute or unknown
v  = xml.attr(n, "id", "none")      ' attribute or default
```

**Path syntax is deliberately tiny and is not XPath:** slash-separated steps,
each a local name or `*`, matched against element children only, descending
from the given node. First match for `find`, all matches at the final step for
`find_all`. Anything fancier (predicates, `//`, axes) is a non-goal — user
code loops.

Matching is on **local names** by default, which is the right ergonomic for
SEC documents where prefixes vary by filer. Exact qualified matching is
available by matching against `qname` in user code.

---

## 4. Streaming API (pull cursor)

The large-file surface. A reader is an opaque handle over `xmlReader`;
memory use is bounded by the current element's depth, not file size.

```basic
r = xml.reader(path)                ' open; raises on unreadable/not-XML

ev = xml.read(r)     ' next event record, or nothing at end-of-document
                     ' { kind: "element"|"end"|"text",
                     '   name, qname, ns, attrs,      (element)
                     '   text,                        (text)
                     '   depth, line }

ok = xml.skip_to(r, "infoTable")    ' advance to next start-element with this
                                    ' local name; false at end-of-document

node = xml.subtree(r)               ' materialize the current element (cursor
                                    ' must be on a start-element) into a §2
                                    ' record and advance past it

xml.close(r)                        ' idempotent; readers also close on scope
                                    ' cleanup like other handles
```

### The windowing pattern

`skip_to` + `subtree` is the intended idiom and the reason the two surfaces
share one node representation — stream at the file level, records at the
logical level:

```basic
r = xml.reader("form13f_infotable.xml")
while xml.skip_to(r, "infoTable")
    t = xml.subtree(r)              ' one holding, as an ordinary record tree
    append(rows, {
        issuer: xml.text(xml.find(t, "nameOfIssuer")),
        cusip:  xml.text(xml.find(t, "cusip")),
        value:  number(xml.text(xml.find(t, "value")))
    })
end while
xml.close(r)
```

Ten thousand positions flow past a constant-memory cursor as ten thousand
small record trees. The same idiom iterates facts out of a 300 MB XBRL
instance document.

---

## 5. Encoding (writing XML)

```basic
s = xml.encode(node)                ' record tree -> XML string
s = xml.encode(node, pretty)        ' indented
```

- Escapes text and attribute values correctly; writes `qname` as given (the
  caller owns namespace declarations — round-tripping a parsed tree preserves
  them since `xmlns` attributes live in `attrs` like any other).
- Validation at entry: an element record must have string `name` and
  list `children`; anything else raises. Attribute values are stringified
  via canonical string conversion.
- A **streaming writer** (constant-memory output of huge documents) is
  **(future)** — no current customer needs it.

---

## 6. Lenient HTML (same engine, separate entry point)

```basic
doc = xml.parse_html(text)          ' tag-soup tolerant; always yields a tree
s   = xml.text(doc)                 ' -> whole-document visible text
```

libxml2's HTML parser never rejects input — it repairs. The output is the
same §2 record shape, so `find`/`find_all`/`text` work unchanged. This is
what `mdna.bas` builds section extraction on. Scope note: this is an HTML
*parser*, not a browser — no scripts, no CSS, no layout; `xml.text` order is
document order.

---

## 7. Security and resource limits (non-negotiable defaults)

XML parsing has famous failure modes; the module closes them **permanently
rather than configurably**:

- **No external entities, ever** (XXE). No DTD loading, no network access
  from the parser (`NONET`), external entity references resolve to nothing.
- **Entity-expansion cap** (billion-laughs): only the five predefined
  entities and character references are expanded; DTD-defined entities are
  not processed at all.
- **Depth cap** (default 256, generous) against pathological nesting;
  exceeding it is a structured error, not a crash.

There is no option to relax these. A user who needs DTD processing needs a
different tool.

---

## 8. Errors

Structured runtime errors, source **`xml`**, carrying the libxml2 message
plus **line and column** where available: not-well-formed input, encoding
failures, depth-cap violations, reader use-after-close, `subtree` when the
cursor is not on a start-element. `find`/`attr`/`text` never raise on
absence — they return `unknown` (find/attr) or `""` (text on empty), keeping
lookup code linear.

---

## 9. Non-goals

XPath (the §3 mini-path is final), XSLT, schema/DTD *validation*, XInclude,
XML 1.1, canonicalization, DOM mutation helpers (records are already mutable
by ordinary means), streaming writer (future), and any SEC-specific
knowledge.

---

## 10. Open questions

1. Whitespace default: drop-between-elements is right for the known
   customers; confirm against one document-oriented use before freezing.
2. Should `skip_to` accept a path (`"table/row"`) instead of a bare name?
   Start bare; a path form is compatible later.
3. Attribute ordering on encode (as-inserted vs sorted) — matters only for
   byte-exact golden tests; proposal: as-inserted.
4. Whether `parse_html` belongs here long-term or in a future `html` module;
   staying here until a second HTML customer appears.

---

## 11. Roadmap

Golden-file convention throughout; fixtures include real SEC documents
(a Form 4, a 13F information table) checked into `examples/fixtures/`.

### Phase 1 — Tree
`parse`, `parse_file`, node shape, whitespace policy, `find`/`find_all`/
`text`/`attr`, `encode` (+ pretty), errors with line/column, security
defaults. Verified: round-trip goldens; a Form 4 parsed and field-checked
against hand-read values.

### Phase 2 — Reader
`reader`/`read`/`skip_to`/`subtree`/`close`, scope-cleanup close, depth cap.
Verified: 13F windowing golden; a large generated document (≥100 MB,
synthesized by a fixture script, not checked in) streamed with memory
observed flat — asserted via `/proc/self/status` VmHWM sampling in the test
harness rather than eyeballing.

### Phase 3 — HTML
`parse_html` + `xml.text` extraction quality pass on three real 10-K HTML
documents (the `mdna.bas` acceptance case).

---

End of XML library design.
