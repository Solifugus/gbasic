' NAP-2 negative: INOUT is supported only for by-value scalar/enum types. A
' pointer-shaped INOUT (here GLib.base64_decode_inplace's inout `text`, a C byte
' array) has ambiguous ownership of the replaced input and must be refused cleanly
' rather than guessed at. This exercises the INOUT-rejection path with a stable,
' encoding-independent error (unlike the internal grefcount encoding of a working
' INOUT scalar), so it is suitable as a byte-exact golden.
load gi
gi.require("GLib", "2.0")
x = gi.invoke("GLib.base64_decode_inplace", "AAAA", 0)
