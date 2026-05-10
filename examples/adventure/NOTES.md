# The Lantern Room Notes

This example uses only currently implemented gBASIC features. It is intended as a small language pressure test, not just a game demo.

Useful features used:

- `input(">")`
- `print(...)`
- assignment modifiers: `(trimmed)`, `(lowered)`, and `(split)`
- arrays of records for room and item data
- arrays with `append`, `remove`, `join`, and `find`
- record field access for room exits, item descriptions, and command parse results
- functions
- `if ... else ... end if`
- `while` loops
- `break` and `continue`
- string concatenation with `+`
- `nothing` checks through `find(...)`

Game coverage:

- Rooms: cellar, hall, library, kitchen, garden, study, tunnel
- Items: lamp, brass key, note
- Commands: look, compass directions, go direction, take, drop, read, inventory, help, quit
- A locked garden gate requires the brass key.
- The note hints at the brass key solution.
- The library and study demonstrate dark-room behavior when the lamp is not carried.

Data-driven structure:

- `build_rooms()` returns an array of room records.
- `build_items()` returns an array of item records.
- Room records contain `name`, `description`, `dark`, `north`, `south`, `east`, `west`, and `needs_light`.
- Item records contain `name`, `location`, `description`, `read_text`, and `needs_light`.
- `parse_command()` returns a command record with `verb`, `noun`, and `direction`.
- `move_player()` returns a record containing the new `location`, updated `gate_unlocked`, and whether movement happened.
- `take_item()` and `drop_item()` return records containing updated `items` and `inventory` arrays.

Places where gBASIC felt clean:

- `command(trimmed)= input(">")` followed by `command(lowered)= command` reads well.
- `words(split)= command` is a compact parser for simple command text.
- `find(inventory, item) != nothing` is readable for inventory checks.
- `append(inventory, item)` and `remove(inventory, index)` make simple inventory mutation direct.
- Arrays of records are good enough to express a small room graph without compiler changes.
- Returning records from helpers is a workable way to simulate multiple return values.
- Constructor-style helper functions such as `room(...)` and `item(...)` make record literals much easier to scan.
- `while true`, `continue`, and `break` make the command loop much clearer than the earlier label/goto version.
- `if`/`else` makes movement and item handling noticeably easier to read than parallel flag checks.

Language friction:

- There are no maps/dictionaries yet, so room and item lookup requires hand-written linear scans over arrays of records.
- Record syntax is readable for small values, but large inline record literals are noisy. Constructor helper functions helped a lot.
- Nested assignment such as `items[i].location = "inventory"` is now supported. Earlier versions rebuilt arrays of records manually for this.
- Function arguments are value-like for this use case, so helpers that change more than one thing return records containing the updated values.
- There is no `contains(array, value)` helper, so the example repeats `find(...) != nothing`.
- There is no `remove_value(array, value)` helper, so dropping an item requires `item_index = find(...)` followed by `remove(...)`.
- There is no `find_by(records, field, value)` helper, so `find_room_index(...)` and `find_item_index(...)` duplicate the same loop pattern.
- Command parsing is manual. A helper for verb/noun parsing, or a standard command parser library, would reduce boilerplate.
- Multi-word items are handled only up to two words with `noun = noun + " " + words[2]`. A `join(mid(words, 1, ...), " ")` style would be nicer once array slicing is broader and more ergonomic.
- A standard `join_from(words, start_index, separator)` helper would fit this command parser well.
- There is no `consider`/`select case` block, so command dispatch and movement logic become long chains of nested `if` statements.
- There is no `elseif`, so nested `if` blocks are used heavily.
- There are no multiline strings, so long room descriptions are multiple `print(...)` calls or long single lines.
- Generic data-driven rules still need escape hatches. The locked garden gate is handled in `move_player(...)` as special logic.
- There is no built-in data file/resource loading pattern for story content yet.

Earlier design-pressure notes:

- An earlier version used a function-local `loop:` label and `goto`, which exposed the need for a general `while` loop.
- An earlier version used separate `if` blocks for false paths, which exposed the need for `else`.
- Lowercase normalization is now supported, so commands may be typed in mixed case.
