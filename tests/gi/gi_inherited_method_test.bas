' Regression: gi.call must resolve methods inherited from PARENT CLASSES, not
' just the object's own class + interfaces. Gio.MemoryInputStream constructs
' bare and inherits close() from its parent class Gio.InputStream — before the
' parent-chain walk this raised "unknown method: close". Headless.
load gi
gi.require("Gio", "2.0")

s = gi.new("Gio.MemoryInputStream")
print(gi.call(s, "close", nothing))
