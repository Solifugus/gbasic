' persist.bas — crash-safe, versioned persistence for application state.
'
' A thin manager over the filesystem primitives, for any program that has to
' remember something across runs — settings, session state, a cache, a document
' index. It knows nothing about what it is storing.
'
'   * WRITE is atomic. `json_encode` produces strict RFC-8259 JSON (never the
'     lenient `encode` dialect — a file other tools may read must be real JSON).
'     The text goes to a `.tmp` sibling and is swapped in with `atomic_replace`
'     (a single rename(2)), so a crash mid-write never leaves a truncated file:
'     a reader sees either the whole old file or the whole new one.
'   * READ never raises. `decode` raises on malformed JSON and gBASIC cannot
'     catch a raise (docs/ai/ERRORS.md), so reads go through `try_decode` and
'     report one of three states as a VALUE — missing, corrupt (with the parser's
'     reason and position), or loaded. The caller owns the recovery policy.
'
'   persist.ensure_dir(home)
'   persist.write_atomic(home + "/settings.json", { schema_version: 1, theme: "dark" })
'
'   st = persist.read_status(home + "/settings.json")
'   if st.status = "loaded" then
'       settings = st.value
'   end if
'
' No `load` dependency of its own: `try_decode` and `atomic_replace` are
' unconditional builtins.
library persist

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
                    tail = persist._last(acc)
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
            error "persist: value is not JSON-encodable for " + path
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
    '   { status: "missing", value: nothing, message: "" }  — no file there
    '   { status: "corrupt", value: nothing, message: <why> } — unreadable
    '   { status: "loaded",  value: <record>, message: "" } — present, well-formed
    ' The caller (studio_model / studio) applies recovery policy.
    '
    ' ONE PASS, through the platform parser. This used to pre-validate with a
    ' pure-gBASIC JSON scanner and then decode -- two full passes, the first of
    ' them QUADRATIC, because `mid(s, i, 1)` is O(i) on codepoint-indexed strings
    ' and a per-character scan is therefore O(n^2). Measured on the scanner it
    ' replaced: 64 KB 16 s, 128 KB 69 s, 256 KB 291 s; opening a 116 KB results
    ' index took 92 s. `try_decode` (PLAT-JSON) reports failure as a value instead
    ' of raising, so the pre-pass has no reason to exist.
    '
    ' `message` is new and additive: a corrupt store can now say WHY -- which file
    ' failed and where -- instead of only that something did.
    '
    ' One deliberate classification change comes with this. The old pre-validator
    ' checked STRICT JSON while `decode` accepts the historical gBASIC dialect, so
    ' a file containing `nothing`, `unknown` or `+1` was reported corrupt even
    ' though the decoder could read it. The reader is now self-consistent: what the
    ' parser can read, it reads. Studio only ever WRITES strict JSON (json_encode),
    ' so this is reachable only by hand-editing a store.
    function read_status(path)
        ref(file) = path
        present = exists(ref)
        if not present then
            return { status: "missing", value: nothing, message: "" }
        end if
        text = read(ref)
        r = try_decode(text)
        if not r.ok then
            return { status: "corrupt", value: nothing,
                     message: r.message + " (line " + r.line + ", column " + r.column + ")" }
        end if
        return { status: "loaded", value: r.value, message: "" }
    end function

end library
