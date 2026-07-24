' datagrid.bas — a general, reusable virtualized data grid for GTK 4.
'
' A DataGrid displays large tabular datasets in a GtkColumnView without building
' one widget per row or per cell: GTK realizes cell widgets only for the rows
' actually on screen (plus a little recycling slack), so a 1,000,000-row logical
' model costs a few hundred widgets, not a million. It is a GENERAL component —
' banking ledgers, admin tables, database browsers, ETL monitors, scientific
' data — not tied to any application.
'
' Two source modes:
'   * ARRAY-BACKED   — a gBASIC array of records, of arrays, or of scalars. COW
'                      arrays make row access O(1), so the array is never copied
'                      into native storage; the grid reads elements on demand.
'   * VIRTUAL        — a count function and a cell function. Nothing is
'                      materialized; rows are computed lazily on bind. This is
'                      the path for database cursors, generated data, or datasets
'                      too large to hold in memory.
'
' Architecture: a small fixed native GListModel adapter (`rowmodel.*`, C) supplies
' the row COUNT and lazy index-carrying row proxies to GtkColumnView — the one
' capability the generic `gi` bridge cannot provide, because GListModel is a
' GObject interface (implementing it from gBASIC would need runtime subclassing,
' which the platform deliberately avoids). EVERYTHING else — columns, factories,
' formatting, selection, refresh — is here in gBASIC over `gi`. The native model
' never calls back into the interpreter; the only gBASIC re-entry is the
' GtkSignalListItemFactory "bind" signal, which runs on the GTK main-loop /
' interpreter thread through the established, error-contained `gi.connect` path.
'
' ---------------------------------------------------------------------------
' Required setup (ONE line, at program top level):
'
'     load datagrid
'     _DATAGRID = datagrid.new_registry()
'
' The grid's per-grid state (data source, columns) has to be reachable from the
' factory "bind" signal handler, which GTK invokes with only (factory, item).
' gBASIC has no closures, so the handler finds its grid through a program-global
' registry named `_DATAGRID`. It must be created at program scope (a library
' function cannot create a program global visible to other functions); every
' datagrid call then reads and updates it.
' ---------------------------------------------------------------------------
'
' API:
'   datagrid.new_registry()                     -> the registry (assign to _DATAGRID)
'   datagrid.create(rows)                        -> handle   (array-backed)
'   datagrid.create_virtual(count_fn, cell_fn)   -> handle   (virtual/generated)
'   datagrid.add_column(handle, spec)            -> handle   (chainable)
'       spec: { title, field | index, resizable, format }
'         title      : column header string
'         field      : record field name (array-of-records source)
'         index      : element index (array-of-arrays source); omit both for a
'                      scalar-array source
'         resizable  : bool (default true)
'         format     : optional function value (value) -> string
'   datagrid.widget(handle)         -> the GtkColumnView (embed / customize)
'   datagrid.selection(handle)      -> the GtkSingleSelection model
'   datagrid.selected(handle)       -> selected logical row index, or -1 if none
'   datagrid.row_count(handle)      -> current logical row count
'   datagrid.set_rows(handle, rows) -> replace an array-backed source, refresh
'   datagrid.refresh(handle)        -> re-read the source count and reload rows
'   datagrid.set_count(handle, n)   -> set a virtual source's logical row count
'   datagrid.destroy(handle)        -> release the grid's retained widgets/callbacks
'   datagrid.destroyed(handle)      -> true once destroyed
'   datagrid.accesses()             -> total cell binds so far (instrumentation)
'   datagrid.setups()               -> total cell-widget setups so far
'   datagrid.reset_accesses()       -> zero both counters
'
' OWNERSHIP: a grid handle is all you need to keep. The registry entry holds the
' view, selection, native model, and every column's factory and format function
' for as long as the grid lives, so factories may stay entirely internal — you
' never need to hold one in a variable of your own to make rendering work.
'
' MEASURING BINDS: GtkColumnView realizes and binds its visible rows when the view
' is first given a size — i.e. when it is added to a window — NOT when the window
' is presented or when the main loop runs. Reset the counters BEFORE building the
' widget tree; resetting after present zeroes work that has already happened and
' makes a perfectly healthy grid look like it never bound.
'
' Value semantics (array-backed): datagrid.create takes a COW SNAPSHOT of the
' array you pass — it shares the backing store until either side mutates. Later
' mutation of your variable does NOT change what the grid shows, and mutating
' through the grid never touches your variable. To show new data, call
' datagrid.set_rows(handle, newrows) (or refresh). This is a deliberate,
' predictable snapshot rule, not accidental aliasing.
'
' Requires the GTK 4 typelib and `load gi` + GTK initialized (gtk.init or a
' running application), exactly like gtkui.bas.

library datagrid

    ' ---- registry ----------------------------------------------------------

    ' Build the empty registry. Assign the result to the program global
    ' `_DATAGRID` once, before creating any grid.
    function new_registry()
        return { grids: [], next_id: 0, accesses: 0, setups: 0 }
    end function

    ' ---- small helpers -----------------------------------------------------

    function _get(rec, field, dflt)
        if has(rec, field) then return rec[field]
        return dflt
    end function

    function _find_col(grid, factory)
        for each c in grid.columns
            if c.factory = factory then return c
        end for
        return nothing
    end function

    ' Logical row count of a grid's current source.
    function _source_count(grid)
        if grid.kind = "virtual" then
            f = grid.count_fn
            return f()
        end if
        return count(grid.rows)
    end function

    ' The value at (row index, column) for a grid. Never raises: an out-of-range
    ' row, a missing field, or a wrong-shaped element yields `nothing`, which the
    ' formatter renders as empty — a failed lookup must not unwind through GTK.
    function _cell(grid, idx, col)
        if grid.kind = "virtual" then
            f = grid.cell_fn
            return f(idx, col.ordinal)
        end if
        if idx < 0 then return nothing
        if idx >= count(grid.rows) then return nothing
        rec = grid.rows[idx]
        reckind = reflect.kind(rec)
        if has(col, "field") then
            if reckind = "record" then
                if has(rec, col.field) then return rec[col.field]
            end if
            return nothing
        end if
        if has(col, "index") then
            if reckind = "array" then
                if col.index >= 0 then
                    if col.index < count(rec) then return rec[col.index]
                end if
                return nothing
            end if
            return rec
        end if
        return rec
    end function

    function _format(value, col)
        if has(col, "format") then
            f = col.format
            return f(value)
        end if
        if value = nothing then return ""
        return string(value)
    end function

    ' ---- factory signal handlers (run on the GTK/interpreter main thread) ---

    function _setup(factory, item)
        _DATAGRID.setups = _DATAGRID.setups + 1
        label = gi.new("Gtk.Label")
        label.set_xalign(0.0)
        item.set_child(label)
    end function

    function _bind(factory, item)
        _DATAGRID.accesses = _DATAGRID.accesses + 1
        row = item.get_item()
        gid = rowmodel.row_grid(row)
        idx = rowmodel.row_index(row)
        grid = _DATAGRID.grids[gid]
        ' A destroyed grid keeps a tombstone slot (see destroy). GTK should have
        ' dropped its widgets by then, but a late in-flight bind must render blank
        ' rather than unwind through the toolkit.
        if grid.kind = "destroyed" then
            label = item.get_child()
            label.set_label("")
            return nothing
        end if
        col = _find_col(grid, factory)
        value = _cell(grid, idx, col)
        label = item.get_child()
        label.set_label(_format(value, col))
    end function

    ' ---- grid construction shared by both source modes ---------------------

    function _new_grid(kind)
        gid = _DATAGRID.next_id
        _DATAGRID.next_id = gid + 1
        model = rowmodel.new(gid)
        sel = gi.new("Gtk.SingleSelection")
        sel.set_model(model)
        view = gi.new("Gtk.ColumnView")
        view.set_model(sel)
        return { id: gid, kind: kind, model: model, selection: sel, view: view, columns: [] }
    end function

    ' ---- public: sources ---------------------------------------------------

    function create(rows)
        grid = _new_grid("array")
        grid.rows = rows
        _DATAGRID.grids = append(_DATAGRID.grids, grid)
        rowmodel.set_count(grid.model, count(rows))
        return { id: grid.id }
    end function

    function create_virtual(count_fn, cell_fn)
        grid = _new_grid("virtual")
        grid.count_fn = count_fn
        grid.cell_fn = cell_fn
        _DATAGRID.grids = append(_DATAGRID.grids, grid)
        n = count_fn()
        rowmodel.set_count(grid.model, n)
        return { id: grid.id }
    end function

    ' ---- public: columns ---------------------------------------------------

    function add_column(handle, spec)
        grid = _DATAGRID.grids[handle.id]
        fac = gi.new("Gtk.SignalListItemFactory")
        gi.connect(fac, "setup", _setup)
        gi.connect(fac, "bind", _bind)
        gcol = gi.new("Gtk.ColumnViewColumn")
        gcol.set_title(_get(spec, "title", ""))
        gcol.set_factory(fac)
        gcol.set_resizable(_get(spec, "resizable", true))
        col = { ordinal: count(grid.columns), factory: fac, gcolumn: gcol }
        if has(spec, "field") then col.field = spec.field
        if has(spec, "index") then col.index = spec.index
        if has(spec, "format") then col.format = spec.format
        grid.columns = append(grid.columns, col)
        _DATAGRID.grids[handle.id] = grid
        grid.view.append_column(gcol)
        return handle
    end function

    ' ---- public: accessors -------------------------------------------------

    function widget(handle)
        return _DATAGRID.grids[handle.id].view
    end function

    function selection(handle)
        return _DATAGRID.grids[handle.id].selection
    end function

    ' The native GbRowModel (a GListModel). Mainly for instrumentation, e.g.
    ' rowmodel.item_requests(datagrid.model(h)) — how many rows GTK realized.
    function model(handle)
        return _DATAGRID.grids[handle.id].model
    end function

    ' The exact string a cell would display for (logical row, column ordinal) —
    ' the same _cell + _format path the bind handler runs. Lets callers read grid
    ' values programmatically and lets tests assert displayed content
    ' deterministically without a rendered window. Out-of-range / missing yields
    ' "" (never raises for an array-backed source).
    function cell(handle, row, col_ordinal)
        grid = _DATAGRID.grids[handle.id]
        col = grid.columns[col_ordinal]
        value = _cell(grid, row, col)
        return _format(value, col)
    end function

    function row_count(handle)
        return rowmodel.count(_DATAGRID.grids[handle.id].model)
    end function

    ' Selected logical row index, or -1 when nothing is selected (GTK reports an
    ' out-of-range sentinel for "no selection").
    function selected(handle)
        grid = _DATAGRID.grids[handle.id]
        pos = grid.selection.get_selected()
        n = rowmodel.count(grid.model)
        if pos >= n then return -1
        return pos
    end function

    ' ---- public: updates ---------------------------------------------------

    ' Reload the model from the current source count, emitting one items-changed
    ' so GtkColumnView re-binds visible rows without rebuilding widgets.
    function refresh(handle)
        grid = _DATAGRID.grids[handle.id]
        rowmodel.set_count(grid.model, _source_count(grid))
        return handle
    end function

    ' Replace an array-backed source and refresh.
    function set_rows(handle, rows)
        grid = _DATAGRID.grids[handle.id]
        grid.rows = rows
        _DATAGRID.grids[handle.id] = grid
        rowmodel.set_count(grid.model, count(rows))
        return handle
    end function

    ' Set a virtual source's logical row count directly.
    function set_count(handle, n)
        grid = _DATAGRID.grids[handle.id]
        rowmodel.set_count(grid.model, n)
        return handle
    end function

    ' ---- public: disposal --------------------------------------------------

    ' Release everything the grid retains — its GtkColumnView, selection, native
    ' model, columns, factories, and the callback-reachable source/format function
    ' values. Call it when a grid is finished with; destroy the containing window
    ' separately (GTK owns the widgets that are still parented).
    '
    ' The registry SLOT is tombstoned, not removed: the factory bind handler finds
    ' its grid by id (an index into `grids`), so removing an element would shift
    ' every later grid's id. The tombstone is a constant-size record, so repeated
    ' create/destroy cycles do not accumulate retained factories or callbacks.
    function destroy(handle)
        grid = _DATAGRID.grids[handle.id]
        if grid.kind = "destroyed" then return handle
        _DATAGRID.grids[handle.id] = { id: handle.id, kind: "destroyed", columns: [] }
        return handle
    end function

    ' True once destroy has released this grid's retained state.
    function destroyed(handle)
        grid = _DATAGRID.grids[handle.id]
        return grid.kind = "destroyed"
    end function

    ' ---- instrumentation ---------------------------------------------------

    function accesses()
        return _DATAGRID.accesses
    end function

    ' Total factory "setup" calls so far — one per cell widget GTK realized (as
    ' opposed to `accesses`, which counts re-bindable cell fills).
    function setups()
        return _DATAGRID.setups
    end function

    function reset_accesses()
        _DATAGRID.accesses = 0
        _DATAGRID.setups = 0
    end function

end library
