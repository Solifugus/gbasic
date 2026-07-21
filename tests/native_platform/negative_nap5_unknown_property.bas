' NAP-5 negative: reading an unknown property through the sugar is a clean error.
load gi
gi.require("Gio", "2.0")
app = gi.new("Gio.Application")
print app.no_such_property
