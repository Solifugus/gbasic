' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' For each file named on the command line, one JSON line: the comments
' source_comments answers, and source_outline's answer before and after every
' one of those comments is replaced by spaces of the same length (so offsets
' do not move). tests/source_comments/check_corpus.py holds them to the file.

program main(args)
    for each path in args
        f {file}= path
        text = read(f)
        r = source_comments(text)
        ' where each line starts, in bytes
        starts = [0]
        at = byte_find(text, "\n")
        while not is_nothing(at)
            append(starts, at + 1)
            at = byte_find(text, "\n", at + 1)
        end while
        ' A byte-order mark is skipped by the lexer, so line 1's columns
        ' count from after it, as every diagnostic's do.
        bom = 0
        if byte_slice(text, 0, 3) = from_bytes([0xEF, 0xBB, 0xBF]) then
            bom = 3
        end if
        pieces = []
        from = 0
        for each c in r.comments
            for each piece, k in split(c.raw, "\n")
                o = starts[c.start_line - 1 + k] + c.start_column - 1
                if c.start_line + k = 1 then
                    o = o + bom
                end if
                append(pieces, byte_slice(text, from, o - from))
                append(pieces, repeat(" ", byte_count(piece)))
                from = o + byte_count(piece)
            next
        next
        append(pieces, byte_slice(text, from))
        blanked = join(pieces, "")
        before = source_outline(text)
        after = source_outline(blanked)
        print json_encode({ file: path, ok: r.ok, comments: r.comments,
                            same_outline: string(before.nodes) = string(after.nodes) and before.ok = after.ok,
                            outline_ok: before.ok, still_has_bytes: byte_count(blanked) = byte_count(text) })
    next
end program
