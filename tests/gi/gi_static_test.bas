' CLASS STATICS THROUGH gi.invoke -- `Namespace.Type.function`, the shape the
' bridge could not reach at all (DOGFOOD 19).
'
' A GI namespace has two kinds of receiver-less function: an entry on the
' namespace (GLib.markup_escape_text) and a static on a class
' (Gdk.Display.get_default). gi.invoke resolved only the first, because
' gi_repository_find_by_name takes ONE name and `Display.get_default` is two --
' so display and monitor lookup, Gtk.StyleContext.add_provider_for_display and
' the GLib/Gio statics were unreachable, and gBASIC Studio installs a CSS
' provider per widget for want of the display-wide call.
'
' TESTED ON Gio/GLib RATHER THAN GTK, and the difference is the machine not the
' mechanism: the resolution is identical, and Gio/GLib need no display, so this
' tier runs in the ordinary headless gate rather than behind a display gate
' where nobody would see it fail.
'
' SELF-CHECKING, and the expected values are FACTS ABOUT GLib rather than about
' us: SHA-256 is 32 bytes and MD5 is 16, and ISO 15924 for Latin is the four
' bytes "Latn", which is 0x4C61746E = 1281455214. A golden would have recorded
' whatever number came back.
load gi
gi.require("GLib", "2.0")
gi.require("Gio", "2.0")

bad = 0

' STRUCT static: g_checksum_type_get_length, on the GLib.Checksum struct.
got = gi.invoke("GLib.Checksum.type_get_length", 2)
if got = 32 then
    print "  ok   struct static: sha256 digest is 32 bytes"
else
    bad = bad + 1
    print "  FAIL struct static: got " + string(got) + ", want 32"
end if
got = gi.invoke("GLib.Checksum.type_get_length", 0)
if got = 16 then
    print "  ok   struct static: md5 digest is 16 bytes"
else
    bad = bad + 1
    print "  FAIL struct static: got " + string(got) + ", want 16"
end if

' ENUM static, both directions. An enum carrying functions is the branch a
' struct/object-only lookup would miss, and it is easy to miss because enums
' with methods are rare.
got = gi.invoke("GLib.UnicodeScript.to_iso15924", 25)
if got = 1281455214 then
    print "  ok   enum static: LATIN is the four bytes Latn"
else
    bad = bad + 1
    print "  FAIL enum static: got " + string(got) + ", want 1281455214"
end if
got = gi.invoke("GLib.UnicodeScript.from_iso15924", 1281455214)
if got = 25 then
    print "  ok   enum static: Latn comes back as LATIN"
else
    bad = bad + 1
    print "  FAIL enum static: got " + string(got) + ", want 25"
end if

' STRUCT static returning a boxed value -- g_main_context_default.
ctx = gi.invoke("GLib.MainContext.default")
if type(ctx) = "gboxed" then
    print "  ok   struct static returns a boxed value"
else
    bad = bad + 1
    print "  FAIL struct static return: got " + type(ctx) + ", want gboxed"
end if

' INTERFACE static returning an object -- g_file_parse_name lives on Gio.File,
' which is an INTERFACE, not a class. That is a third lookup path.
f = gi.invoke("Gio.File.parse_name", "/etc/hostname")
if type(f) = "gobject" then
    print "  ok   interface static returns an object"
else
    bad = bad + 1
    print "  FAIL interface static return: got " + type(f) + ", want gobject"
end if

' AND THE OBJECT PATH, which is the shape the ledger entry names:
' Gio.Application.get_default is Gdk.Display.get_default with no display
' attached. Nothing has set a default application, so the answer is `nothing`
' -- which is the correct answer and not a failure to resolve, and the two are
' told apart by the refusals below: an unresolved name RAISES.
app = gi.invoke("Gio.Application.get_default")
if is_nothing(app) then
    print "  ok   object static answers (no default application is set)"
else
    bad = bad + 1
    print "  FAIL object static: got " + type(app)
end if

' THE CONTROL. A two-part namespace function must still resolve exactly as
' before -- the new path is an addition, not a replacement, and without this
' "statics work" is satisfied by a change that broke the case that already did.
esc = gi.invoke("GLib.markup_escape_text", "<hi>", -1)
if esc = "&lt;hi&gt;" then
    print "  ok   control: a namespace function still resolves"
else
    bad = bad + 1
    print "  FAIL control: got " + esc
end if

print "failed: " + string(bad)
