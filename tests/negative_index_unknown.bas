' The OTHER absence, and the pair is the point: a missing record key reads back
' as `unknown`, not `nothing`, and the guard that catches one does not catch the
' other -- so the two messages must DIFFER. If they are ever collapsed back into
' one sentence, both goldens move together and say so.
rooms = ["hall", "cellar"]
where = { start: 0 }
print rooms[where["finish"]]
