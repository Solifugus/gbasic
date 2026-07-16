' Enum/flags resolution by fully-qualified name -> integer.
load gi
gi.require("Gio", "2.0")

print(gi.enum("Gio.ApplicationFlags.IS_SERVICE"))
print(gi.enum("Gio.ApplicationFlags.IS_LAUNCHER"))
