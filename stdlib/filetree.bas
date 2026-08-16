' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' filetree.bas — a directory as a navigable TREE.
'
' Turns a directory into a tree of plain gBASIC values — a model, not widgets — so
' a GUI can render it, a CLI can print it, and a test can assert it headlessly. It
' treats files as files: no parsing, no symbols, no interpretation of contents.
'
' Expansion is caller-controlled, which is what makes it usable for a navigation
' pane: pass the set of directory paths that are open and only those are scanned,
' so the cost is bounded by what is visible and the state survives a refresh or a
' restart. Pass an empty set for a one-level listing.
'
'   nodes = filetree.scan("/home/u/proj", expanded)
'   for each row in filetree.flatten(nodes)
'       print repeat("  ", row.depth) + row.name
'   end for
'
' Design notes:
'   * Directory entries are read with the `list` builtin, which returns records
'     {name, type, path} and yields an EMPTY array for a missing/inaccessible
'     directory — so a deleted or unreadable project degrades to an empty subtree
'     rather than crashing.
'   * Order is deterministic: folders first, then files, each sorted by name.
'     (readdir order is not stable, so we sort explicitly.)
'   * Expansion is lazy and caller-controlled: only directories whose path is in the
'     `expanded` set are scanned recursively. This bounds the scan to what is
'     visible and lets expansion state persist across refresh/restart (STU-1).
'
' A tree node:  { name, path, kind: "file"|"dir", expanded: bool, children: [node] }
library filetree

    ' Build the sorted child nodes of `dir_path`, recursing into directories whose
    ' path is present in `expanded`. Missing/unreadable dir -> empty (no raise).
    function _entries(dir_path, expanded)
        folder_names = []
        file_names = []
        d(dir) = dir_path
        for each e in list(d)
            if e.type = "folder" then
                folder_names = append(folder_names, e.name)
            else
                file_names = append(file_names, e.name)
            end if
        end for
        folder_names = sort(folder_names)
        file_names = sort(file_names)

        nodes = []
        for each nm in folder_names
            childpath = dir_path + "/" + nm
            exp = contains(expanded, childpath)
            kids = []
            if exp then
                kids = filetree._entries(childpath, expanded)
            end if
            nodes = append(nodes, { name: nm, path: childpath, kind: "dir", expanded: exp, children: kids })
        end for
        for each nm in file_names
            childpath = dir_path + "/" + nm
            nodes = append(nodes, { name: nm, path: childpath, kind: "file", expanded: false, children: [] })
        end for
        return nodes
    end function

    ' Scan a directory path into the browser's top-level node list, honoring the
    ' `expanded` set of directory paths. Re-invoking this IS the refresh operation:
    ' it re-reads the filesystem, so created/deleted files appear on the next scan.
    function scan(path, expanded)
        return filetree._entries(path, expanded)
    end function

    ' ---- projections over a scanned tree ----------------------------------

    ' Flatten a node list into the visible rows in display order (pre-order; a
    ' directory's children appear only when it is expanded). Each row:
    '   { name, path, kind, depth, expanded }
    function flatten(nodes)
        return filetree._flatten_into(nodes, 0, [])
    end function

    function _flatten_into(nodes, depth, out)
        for each n in nodes
            out = append(out, { name: n.name, path: n.path, kind: n.kind, depth: depth, expanded: n.expanded })
            if n.kind = "dir" then
                if n.expanded then
                    out = filetree._flatten_into(n.children, depth + 1, out)
                end if
            end if
        end for
        return out
    end function

    ' Number of currently-visible rows (files + expanded-visible entries).
    function visible_count(nodes)
        rows = filetree.flatten(nodes)
        return count(rows)
    end function

    ' A deterministic, path-free text rendering of the visible tree. Markers:
    '   "v " expanded dir · "> " collapsed dir · "  " file. Two spaces per depth.
    function dump(nodes)
        rows = filetree.flatten(nodes)
        lines = []
        for each r in rows
            indent = ""
            i = 0
            while i < r.depth
                indent = indent + "  "
                i = i + 1
            end while
            marker = "  "
            if r.kind = "dir" then
                if r.expanded then
                    marker = "v "
                else
                    marker = "> "
                end if
            end if
            lines = append(lines, indent + marker + r.name)
        end for
        return join(lines, "\n")
    end function

end library
