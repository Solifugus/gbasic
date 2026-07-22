' NAP-7 headless: the SourceEditor buffer surface (no GtkSourceView widget, so no
' display needed). Exercises gbasic.lang discovery, text, language assignment
' (must not fire a GtkSource critical — the language manager is kept alive),
' cursor get/set, a source mark, and a highlight tag.
load gi
load sourceeditor

' language discovery via the search-path'd manager
lang = sourceeditor.language("gbasic")
print lang.get_name()
print lang.get_id()
missing = sourceeditor.language("no_such_lang_xyz")
print missing = nothing

ed = sourceeditor.create()
ed.set_text("print(\"hi\")\nfor i in [1, 2, 3]\n  print(i)\nend for\n")
print ed.get_text()
ed.set_language("gbasic")
print "language-set"

ed.set_cursor(2, 4)
c = ed.cursor()
print "cursor " + c.line + ":" + c.column

mk = ed.mark(1, "diagnostic")
print mk.get_category()

tag = ed.highlight(0, 1, "#ffcc00")
print gi.type_name(tag)
ed.unhighlight(tag)
print "done"
