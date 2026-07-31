' sourceeditor.bas — a reusable source/text editor over GtkSourceView 5.
'
' General-purpose: nothing here is named after or specific to any application.
' It wraps the awkward multi-call GtkSource sequences (language discovery, iter
' out-parameters, marks, child anchors) and the gBASIC-specific `gbasic.lang`
' setup, while exposing the underlying GtkSourceBuffer/GtkSourceView so callers
' reach the raw `gi` bridge for anything not wrapped.
'
' Design: an editor is a plain gBASIC record holding its GtkSourceBuffer plus
' this-bound methods. The GtkSourceView WIDGET is created lazily on first
' `.view()` (or `.scroll_to`/`.add_inline`), so text/language/marks/cursor/
' highlight all work headlessly (a GtkSourceBuffer is not a widget); only the
' view-dependent operations need an initialized GTK and a display.
'
' Requires the GtkSource 5 typelib at runtime (gir1.2-gtksource-5), loaded
' through `gi` (not linked). The gBASIC syntax file `gbasic.lang` ships in the
' stdlib under gtksourceview/ and is located via the language-manager search
' path (see language_manager below).
library sourceeditor


    ' Dependencies, declared rather than assumed. A library that calls into
    ' another must load it: relying on the caller to have done so turns a
    ' missing load into a runtime failure deep inside a call, and it stops
    ' working entirely once these libraries live in separate projects.
    load gi
    ' ---- internal helpers (not attached to the editor record) --------------

    ' GtkTextBuffer iter getters come in two introspected shapes: void+out (the
    ' iter is returned directly, a boxed GtkTextIter) and gboolean+out (a record
    ' {result, iter}). Normalize both to the boxed iter.
    function _iter(x)
        if type(x) = "record" then
            return x.iter
        end if
        return x
    end function

    ' Build a GtkSourceLanguageManager whose search path includes the gBASIC
    ' stdlib's gtksourceview/ directory, so get_language("gbasic") resolves
    ' `gbasic.lang`. Development trees set GBASIC_PATH; installed trees fall back
    ' to the documented install location. Nonexistent dirs are harmless (the
    ' manager ignores them).
    function language_manager()
        gi.require("GtkSource", "5")
        lm = gi.new("GtkSource.LanguageManager")
        dirs = []
        cur = lm.get_search_path()
        for each p in cur
            dirs = append(dirs, p)
        end for
        gp = env("GBASIC_PATH")
        if gp != unknown then
            for each seg in split(gp, ":")
                dirs = append(dirs, seg + "/gtksourceview")
            end for
        end if
        dirs = append(dirs, "/usr/local/share/gbasic/stdlib/gtksourceview")
        lm.set_search_path(dirs)
        return lm
    end function

    ' Resolve a GtkSourceLanguage by id (e.g. "gbasic"), or `nothing` if the
    ' language definition is not found on the search path.
    function language(id)
        lm = sourceeditor.language_manager()
        return lm.get_language(id)
    end function

    ' ---- editor methods (this = the editor record) -------------------------

    function _get_text()
        return this.buffer.text
    end function

    function _set_text(s)
        this.buffer.text = s
    end function

    ' Assign a GtkSourceLanguage by id (e.g. "gbasic"). Raises if unknown.
    ' Resolves through the editor's OWN language manager (this._lm), which the
    ' editor record keeps alive: GtkSourceBuffer's highlight engine calls back
    ' into the manager that produced the language, so a transient manager that
    ' finalized here would trigger a GtkSourceView critical.
    function _set_language(id)
        lm = this._lm
        lang = lm.get_language(id)
        if lang = nothing then
            error "sourceeditor: unknown language: " + id
        end if
        buf = this.buffer
        buf.set_language(lang)
    end function

    ' Current cursor position as a record {line, column} (both 0-based).
    function _cursor()
        buf = this.buffer
        ins = buf.get_insert()
        it = sourceeditor._iter(buf.get_iter_at_mark(ins))
        return { line: it.get_line(), column: it.get_line_offset() }
    end function

    ' Move the cursor to a 0-based line and column.
    function _set_cursor(line, column)
        buf = this.buffer
        it = sourceeditor._iter(buf.get_iter_at_line_offset(line, column))
        buf.place_cursor(it)
    end function

    ' Place a GtkSourceMark of the given category at the start of a 0-based line
    ' and return it. Category is a caller-chosen string (e.g. "bookmark",
    ' "diagnostic", "breakpoint") — the meaning is the caller's, not fixed here.
    function _mark(line, category)
        buf = this.buffer
        it = sourceeditor._iter(buf.get_iter_at_line(line))
        return buf.create_source_mark(nothing, category, it)
    end function

    ' Apply a background highlight (a GtkTextTag) over whole lines
    ' [start_line, end_line] (0-based, inclusive) and return the tag, which can
    ' be passed to unhighlight() to remove it. `color` is any CSS color string.
    function _highlight(start_line, end_line, color)
        buf = this.buffer
        tag = gi.new("Gtk.TextTag", "background", color)
        tab = buf.get_tag_table()
        tab.add(tag)
        s = sourceeditor._iter(buf.get_iter_at_line(start_line))
        e = sourceeditor._iter(buf.get_iter_at_line(end_line))
        e.forward_to_line_end()
        buf.apply_tag(tag, s, e)
        return tag
    end function

    ' Remove a highlight tag previously returned by highlight().
    function _unhighlight(tag)
        buf = this.buffer
        s = buf.get_start_iter()
        e = buf.get_end_iter()
        buf.remove_tag(tag, s, e)
    end function

    ' Connect a handler to buffer "changed"; the handler fires on every edit.
    function _on_change(fn)
        gi.connect(this.buffer, "changed", fn)
    end function

    ' Lazily create and return the GtkSourceView widget. Requires an initialized
    ' GTK (a running gtk.application, or gtk.init()) and a display.
    '
    ' NOTE: the ensure-widget step is inlined here AND in _scroll_to/_add_inline
    ' rather than shared, because a method cannot call a sibling method on its own
    ' receiver (`this.view()` does not dispatch — method-call resolution binds the
    ' receiver by variable name, and `this` is not an ordinary variable), and a
    ' plain helper cannot mutate the caller's record (records are copy-on-write),
    ' so the widget must be cached by assigning `this._widget` in each method.
    function _view()
        if this._widget = nothing then
            v = gi.new("GtkSource.View", "buffer", this.buffer)
            v.set_show_line_numbers(true)
            v.set_monospace(true)
            this._widget = v
        end if
        return this._widget
    end function

    ' Scroll the view so a 0-based line is visible. Creates the view if needed.
    function _scroll_to(line)
        if this._widget = nothing then
            v = gi.new("GtkSource.View", "buffer", this.buffer)
            v.set_show_line_numbers(true)
            v.set_monospace(true)
            this._widget = v
        end if
        v = this._widget
        buf = this.buffer
        it = sourceeditor._iter(buf.get_iter_at_line(line))
        v.scroll_to_iter(it, 0.0, true, 0.0, 0.5)
    end function

    ' Insert a GTK widget anchored at the start of a 0-based line (a generic
    ' GtkTextChildAnchor child) and return the anchor. Not tied to any specific
    ' use — annotations, inline controls, code lenses, etc. Creates the view if
    ' needed; requires a display.
    function _add_inline(line, widget)
        if this._widget = nothing then
            v = gi.new("GtkSource.View", "buffer", this.buffer)
            v.set_show_line_numbers(true)
            v.set_monospace(true)
            this._widget = v
        end if
        v = this._widget
        buf = this.buffer
        it = sourceeditor._iter(buf.get_iter_at_line(line))
        anchor = buf.create_child_anchor(it)
        v.add_child_at_anchor(widget, anchor)
        return anchor
    end function

    ' ---- constructor -------------------------------------------------------

    ' Create a new editor. The GtkSourceBuffer exists immediately (headless);
    ' the GtkSourceView is created on first view()/scroll_to/add_inline.
    ' (Named `create`, not `new`, because `new` is a reserved gBASIC keyword.)
    function create()
        gi.require("GtkSource", "5")
        ed = {}
        ed.buffer = gi.new("GtkSource.Buffer")
        ed._widget = nothing
        ' Kept alive for the editor's lifetime: the buffer's highlight engine
        ' calls back into the manager that resolved its language.
        ed._lm = sourceeditor.language_manager()

        ed.get_text = _get_text
        ed.set_text = _set_text
        ed.set_language = _set_language
        ed.cursor = _cursor
        ed.set_cursor = _set_cursor
        ed.mark = _mark
        ed.highlight = _highlight
        ed.unhighlight = _unhighlight
        ed.on_change = _on_change
        ed.view = _view
        ed.scroll_to = _scroll_to
        ed.add_inline = _add_inline
        return ed
    end function

end library
