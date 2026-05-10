# BAG Notes

BAG is a pressure test for practical, data-driven gBASIC programs.

What worked well:

- Arrays of records are expressive enough for area, item, and trigger tables.
- Constructor helpers such as `make_area(...)` and `make_item(...)` make records readable.
- Returning records from helper functions is a workable substitute for multiple return values.
- `(trimmed)`, `(lowered)`, and `(number)` make menu input code direct.
- `(file)`, `write(...)`, and `append(...)` are enough to generate a source file.
- `while true`, `break`, and nested `if` blocks are enough for a simple menu system.

Friction found:

- Nested assignment such as `areas[i].north = 2` and `items[i].location = -1` is now supported. BAG still contains some older rebuild-style generation code, which can be simplified further.
- There is no map/dictionary type, so finding areas and items requires repeated linear search helpers.
- `find_by(records, field, value)`, `contains(array, value)`, and `join_from(words, start_index, separator)` now cover common BAG lookup and command parsing cases.
- `consider` blocks keep menu dispatch readable without deeply nested `if` statements.
- There are no multiline strings or heredocs, so source generation requires many `append(...)` calls.
- `quote(text)` now escapes quotes, backslashes, tabs, carriage returns, and newlines for generated gBASIC string literals.
- `encode(project)` and `decode(text)` now back BAG save/load. A BAG project is stored as `{title = title, areas = areas, items = items, triggers = triggers}`, written with `write(file, encode(project))`, and loaded with `decode(read(file))`.
- BAG uses `on error resume next` around file reads, writes, and decoding so it can return to the menu after common save/load failures. The loaded project shape is still only lightly validated.
- Symbolic direction constants would reduce stringly typed code around `north`, `south`, `east`, `west`, `up`, and `down`.
- File output works, but source generation would benefit from a small standard library for quoting strings and emitting lines.

Possible standard library helpers suggested by BAG:

- `writeln(file, text)`
- `find_by(records, field, value)`
- `contains(array, value)`
- `remove_value(array, value)`
- `join_from(array, start, separator)`
- `menu(prompt, options)`

## Post-save/load development observations

BAG is now usable as a small project editor, but the source still shows where gBASIC needs more library polish before it needs more syntax.

Still awkward:

- Source generation is still the roughest area. `emit_engine(...)` and `emit_data(...)` are long runs of `write(...)` and `append(...)`, even though generated string literals now use `quote(...)`.
- Area exits are still modeled as six parallel fields in many places. Dynamic record access makes `area[direction]` possible, but older helper code still copies `north`, `south`, `east`, `west`, `up`, and `down` by hand.
- Record table updates often rebuild arrays instead of using nested lvalue assignment directly. This is visible in generated helpers such as `set_exit(...)` and `set_item_location(...)`.
- The generated adventure repeats lookup and command parsing helpers that BAG itself also needs. There is no shared BAG runtime library yet.
- Load validates only by touching expected fields. It can reject malformed serialized text, but it does not deeply validate that `areas`, `items`, and `triggers` have the expected record shapes.

Repeated patterns:

- Linear search over arrays of records by `id` or `name`.
- Converting numbers and booleans to strings for display or generated source.
- Joining command words into nouns.
- Checking inventory membership and removing carried item ids.
- Rebuilding records after changing one field.
- Printing simple menu options and dispatching by a string choice.

Good library candidates:

- `writeln(file, text)` would simplify generated source output and avoid repeated `"\n"` appends.
- A small record-table helper set would fit the current language: `find_by(...)` already helps; future helpers could include `update_by(records, field, value, patch)` or narrower BAG-specific helpers in a library file.
- A BAG engine/runtime library would reduce generated adventure size. BAG could generate only data plus `use`/`load` of shared engine code.
- Input helpers such as `yes_no(prompt)` and `number_or_default(prompt, default)` would make menu code less repetitive without changing syntax.

Possible core language pressure:

- `quote(...)` covers safe string literals; the next source-generation need is line-oriented output rather than more string syntax.
- A clearer way to package and reuse runtime libraries would help generated programs avoid copying large engine strings.
- Error handling works, but combining function-level `on error` with callers that also use `on error resume next` is easy to get wrong. This may need better documentation or a small runtime convention before any core redesign.
- Record update ergonomics could improve if the language eventually grows copy/update helpers, but nested lvalue assignment already covers many practical cases.

Defer for now:

- Do not add maps/dictionaries just for BAG; records with dynamic keys are enough for current project data.
- Do not add a second serialization format. Keep `.bag` files as `encode(...)` output until interoperability becomes a real requirement.
- Do not add heredocs yet. Multiline strings and `quote(...)`/`writeln(...)` should be tested first.
- Do not build a full BAG validation schema system until the project format stabilizes.
- Do not redesign command parsing yet; use helper libraries first, then reassess after generated adventure code shrinks.
