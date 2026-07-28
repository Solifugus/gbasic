' studio_store.bas — crash-safe, versioned persistence for gBASIC Studio.
'
' A thin persistence MANAGER over the platform filesystem primitives. It knows
' nothing about Studio's schema (that is studio_model.bas); it only reads and
' writes gBASIC records as standards-compliant JSON, safely:
'
'   * WRITE is atomic. `json_encode` produces strict RFC-8259 JSON (never the
'     lenient `encode` dialect — external files must be real JSON, per the plan).
'     The text is written to a `.tmp` sibling and swapped in with `atomic_replace`
'     (a single rename(2)), so a crash mid-write never leaves a truncated store:
'     a reader sees either the whole old file or the whole new one.
'   * READ never raises on a bad file. `decode` raises on malformed JSON and
'     gBASIC cannot catch a raise (docs/ai/UNLEARN.md), so reads PRE-VALIDATE with
'     studio_json.valid and report status instead of crashing.
'
' Requires studio_json to be loaded by the program (loads live inside the
' `program` block — a top-level `load` does not execute when a program block is
' present).
library studio_store

    ' Last char of a string ("" when empty). Bound out to avoid the
    ' `call(...) = x` modifier-lexer collision on inline comparisons.
    function _last(s)
        n = len(s)
        if n = 0 then
            return ""
        end if
        return mid(s, n - 1, 1)
    end function

    ' Create `path` and any missing parent directories. Idempotent: existing
    ' directories are left alone (make_dir raises on an existing directory, so
    ' each segment is guarded by an existence check via a file reference — the
    ' `exists` builtin wants a file reference even for a directory path).
    function ensure_dir(path)
        acc = ""
        for each seg in split(path, "/")
            if seg = "" then
                acc = acc + "/"
            else
                if acc = "" then
                    acc = seg
                else
                    tail = studio_store._last(acc)
                    if tail = "/" then
                        acc = acc + seg
                    else
                        acc = acc + "/" + seg
                    end if
                end if
                probe(file) = acc
                present = exists(probe)
                if not present then
                    target(dir) = acc
                    make_dir(target)
                end if
            end if
        end for
    end function

    ' Atomically persist `record` to `path` as strict JSON. Pre-validates that the
    ' value is representable as standards JSON and raises a clear error if not —
    ' rather than silently emitting the non-standard `encode` dialect.
    function write_atomic(path, record)
        encodable = json_encodable(record)
        if not encodable then
            error "studio_store: value is not JSON-encodable for " + path
        end if
        text = json_encode(record)
        tmp = path + ".tmp"
        scratch(file) = tmp
        write(scratch, text)
        dest(file) = path
        atomic_replace(scratch, dest)
    end function

    ' Atomically persist raw TEXT (not JSON) to `path`, through the same temp-then-
    ' rename dance as write_atomic. Used for artifacts that are source files rather
    ' than stores -- STU-4's materialized execution prefixes -- where json_encode
    ' would be exactly wrong. Crash-safety matters for the same reason: a reader
    ' (here, a freshly exec'd interpreter) must see the whole file or none of it,
    ' never a half-written one.
    function write_text_atomic(path, text)
        tmp = path + ".tmp"
        scratch(file) = tmp
        write(scratch, text)
        dest(file) = path
        atomic_replace(scratch, dest)
    end function

    ' Read `path` and report one of three states without ever raising:
    '   { status: "missing", value: nothing }  — no file there
    '   { status: "corrupt", value: nothing }  — present but not valid JSON
    '   { status: "loaded",  value: <record> } — present and well-formed
    ' The caller (studio_model / studio) applies recovery policy.
    function read_status(path)
        ref(file) = path
        present = exists(ref)
        if not present then
            return { status: "missing", value: nothing }
        end if
        text = read(ref)
        ok = studio_json.valid(text)
        if not ok then
            return { status: "corrupt", value: nothing }
        end if
        parsed = decode(text)
        return { status: "loaded", value: parsed }
    end function

end library
