# GUI addressability: the record tree is already the address space

**Status:** Proposed. Not implemented, nothing scheduled. Extends
[the declarative GUI design](gui_design.md), which is at Stage 6A.

**Provenance.** The measured claims here come from building gBASIC Studio — a
~25,000-line GTK application in gBASIC, with an assistant that can act under a
permission model and twenty display-tier tests that drive real widgets. Where
this document says "measured", it means Studio hit it. Where it says "propose",
it is an argument, not a finding.

---

## 1. The thesis

The instinct is to add *pathing* to widgets so an AI or a script can reach
every element of a UI. Under the declarative design, most of that already
exists:

- widgets are **records**
- containers hold children in **`contains`**
- **every widget must have an `id`**, and duplicate ids are illegal within a
  window
- `win.<id>` already resolves, and the backend already maintains the mapping
- **user interaction mutates the widget record**, and watchers observe it

So `win.name.value` is a path into a live tree today, and a script that sets it
is already indistinguishable from a user typing. The address space is built.

What is missing is not machinery. It is three things:

1. a **stable path grammar** across nesting
2. a **protocol surface** (MCP) that exposes read and write separately
3. a **safety model**, because "indistinguishable from a user" is the feature
   and the danger in one sentence

This document proposes those three, and argues for one addition —
**semantic actions beside paths** — which is the part Studio has the strongest
evidence about.

---

## 2. Paths

### 2.1 Grammar

Ids are unique within a window, so addressing can stay flat and does not need
to encode nesting:

```text
<window-id>/<widget-id>
```

`contains` still gives the tree for *enumeration*; it is not part of the
address. This matters: a path that encodes structure
(`main/box[0]/button[2]`) breaks the moment anyone reorders a layout, and
reordering a layout is not a behavioural change. A flat id path survives it.

### 2.2 Never indices, for anything durable

Index paths are acceptable as a **fallback for enumeration** of a widget with
no id, and unacceptable as anything a script or a model stores. If a widget is
worth addressing twice, it is worth an id.

### 2.3 Identity across a rebuild

**Measured, and this is the failure mode to design against.** Studio is
immediate-mode over GTK: it rebuilds its browser pane on every redraw and
reconciles its editor notebook by document id. Two consequences it had to work
around:

- a path into a rebuilt subtree is a dangling reference
- a popover whose parent was destroyed is a hard GTK critical, which
  `G_DEBUG=fatal-criticals` turns into a nonzero exit

The declarative design does not inherit this, and that is a genuine advantage
worth protecting: the **record is the identity**, and the backend widget is a
projection of it. A path resolves against the record tree, never against the
backend. Anything that resolves a path by walking GTK is reintroducing the
problem.

---

## 3. Read and act are different, and the protocol must say so

**Measured.** Studio's tool surface has two entry points and one gate:

- the read path (`call`) **refuses an act outright**
- `invoke` is the sole dispatch authority: it refuses a name not in the
  registry, decides permission **before** dispatching, and audits refusals as
  well as successes

The last one is not fussiness. A log of successes is a record of what worked,
not of what was attempted, and it makes an agent probing at a denied tier
invisible.

Proposed MCP surface:

| | |
|---|---|
| `gui.tree(window)` | the record tree, ids, kinds, labels, values |
| `gui.read(path)` | one widget record |
| `gui.set(path, field, value)` | mutate a field |
| `gui.act(path)` | invoke the widget's declared action |

`tree` and `read` are safe. `set` and `act` are not, and should never share a
permission decision with them.

---

## 4. Permission is about consequence, not about controls

**Measured.** Studio assigns permission tiers by **reversibility**, not by how
dangerous a name sounds: editing code is `local` because it is unsaved until
Save; deleting a file is `external` because it is not rewindable.

A widget cannot carry that. "Press the button at `main/go`" says nothing about
whether `go` sends an email. So one of two things must be true:

- the **action** carries the tier, and widgets name an action (§5), or
- every widget is annotated with a tier, which is the same work with a worse
  name

Proposed: a widget may declare `tier:` (`read` | `local` | `external`),
defaulting to `local` for anything that mutates and `read` for anything that
does not. `gui.act` on a widget with no declared tier is **refused**, not
assumed — Studio's rule that an odbc connection which has not said which
engine it reaches is answered `no-engine` rather than guessed at.

One more, from Studio: **scopes narrow and never widen.** If a per-window
policy could grant more than the application's, a window definition somebody
else wrote could raise its own authority.

---

## 5. Semantic actions, beside paths

This is the part to take most seriously, because it is where Studio changed its
mind after building the naive version.

Studio has a widget registry — sixteen stable names, each resolvable to a real
widget, with a tripwire asserting the registry and the shell agree. **The agent
does not use it to act.** It is only for *pointing* ("look here"). Every action
goes through one semantic layer that the widgets also go through.

Three reasons, all measured:

**Presentation churns, and paths are presentation.** Studio's toolbar became
two menus. `new_file_button` stopped existing; it is `file_menu` now. Every
stored path referring to it broke. The semantic action, `new-file`, did not
change at all.

**A widget cannot express intent.** Studio's Delete *arms* on the first press
and fires on the second. An agent driving the button presses once, observes
nothing, presses again — and has deleted a file while believing neither press
worked. The arming lives in the semantic layer, where a caller gets `armed`
back and can stop.

**Parity becomes structural rather than aspirational.** Because the agent and
the window call the same functions, "the agent can do what the user can do" is
a fact about the code rather than a promise, and a refusal reaches both by the
same name.

### Proposal

Let a widget **declare the action it invokes**:

```basic
{ id:"save", component:"button", label:"Save", action:"document.save" }
```

Then:

- `gui.act("main/save")` — fine, and resolves to `document.save`
- `gui.act("document.save")` — also fine, and **survives the button moving,
  being renamed, or becoming a menu item**
- a widget with no `action` is still reachable by path, for uninstrumented UIs

The path is the fallback; the action is the preferred handle. They are one
mechanism, not two competing ones, and the tier hangs off the action where it
belongs.

---

## 6. What this buys immediately: testing

The strongest near-term argument, and it is not about AI at all.

**Measured, from one day of work on Studio.** To drive a GTK UI in a test,
Studio had to hand-build: finding a list row by scanning `get_row_at_y` until
it answers the wanted index (pixel arithmetic was wrong — a row reported height
19 against an actual pitch of ~20); a helper that pops a menu open before
pressing an item, because **a `Gtk.Button` inside a popover that has never been
shown does not respond to `activate()`** — not an error, a press that silently
does nothing; and `set_state_flags(PRELIGHT)` to fake a pointer hover, because
a colour that only appears under the mouse cannot otherwise be looked at.

None of that is Studio being awkward. It is what driving a retained widget tree
from outside costs when addressability was not designed in.

With a record tree that is addressable by construction, a display test is:

```basic
gui.set("main/name", "value", "report.bas")
gui.act("main/save")
assert gui.read("main/status").value = "saved"
```

That is worth building even if no model ever connects.

---

## 7. Do AT-SPI as well, not instead

**AT-SPI already is this**, for the whole desktop: every widget exposed over
D-Bus with a role, a name and invokable actions, addressable by path. GTK and
Qt both implement it. An MCP server over AT-SPI drives every GTK and Qt
application on the machine, not only gBASIC ones.

Two hours reading it before designing a parallel mechanism is likely to be the
best-spent time in this whole effort.

They are complementary, not alternatives:

- **AT-SPI** is generic, works on uninstrumented third-party apps, and is what
  accessibility tooling already speaks. It knows nothing of intent.
- **The record tree** is semantic, knows the application's own vocabulary, and
  can be read without a display server at all.

A record tree can be **projected** to AT-SPI cheaply, since ids, roles and
labels are already present. Doing that makes every gBASIC GUI accessible to
screen readers as a side effect, which is worth having on its own terms.

---

## 8. Backend independence, which is the quiet prize

Because the record tree is the truth and the backend is a projection, the
addressability surface is **backend-agnostic**. If a second backend ever lands
— Qt, web, terminal — scripts, tests and any MCP client keep working
unchanged, because none of them ever addressed a GTK widget.

That is a stronger reason to put addressability in the record layer than any
AI argument, and it is an argument for doing it *before* a second backend
exists rather than after.

---

## 9. What to decide first

1. **Is `id` mandatory everywhere, or only where addressed?** `gui_design.md`
   already says every widget must have one. If that holds, §2 is nearly free.
2. **Does `action` go in now or later?** Retrofitting it after scripts exist
   means every stored path is already a presentation address.
3. **Read-only first?** `gui.tree` and `gui.read` have no safety model to argue
   about and would immediately serve testing and accessibility. `set` and `act`
   can follow once §4 is settled.
4. **Where does the MCP server live** — in the interpreter, or a gBASIC program
   using the existing server libraries? The second keeps the language out of
   the protocol business, which is the boundary gBASIC usually draws.

## 10. What is not proposed

- No evaluation of model-supplied text. Studio's rule: the dispatcher refuses
  a name that is not registered, and nothing anywhere evaluates what a model
  wrote.
- No widget-level scripting language. A widget declares an action name; it does
  not carry code.
- No remote surface by default. An MCP server that can press any button in any
  window is remote control, and should be opt-in, local, and off unless asked
  for.
