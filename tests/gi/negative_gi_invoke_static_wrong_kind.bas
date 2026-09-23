' A name that resolves to something real which cannot carry functions -- here a
' CONSTANT -- must say so rather than "unknown type", which would be false and
' would send the author to check a spelling that is correct.
'
' This case was found by a PERTURBATION, not by reading: with the enum branch
' disabled, `GLib.UnicodeScript.to_iso15924` reported "unknown type:
' GLib.UnicodeScript" about a type that plainly exists. The same wrong answer
' was reachable without any perturbation through a constant, which is what this
' pins -- the reports-the-wrong-cause class.
load gi
gi.require("GLib", "2.0")
print(gi.invoke("GLib.PRIORITY_DEFAULT.anything"))
