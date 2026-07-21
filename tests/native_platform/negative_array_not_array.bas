' NAP-4 negative: a GStrv arg needs an array (or nothing); a scalar is a clean error.
load gi
gi.require("GLib", "2.0")
joined = gi.invoke("GLib.strjoinv", "-", 42)
