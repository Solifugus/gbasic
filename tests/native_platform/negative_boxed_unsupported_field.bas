' NAP-1 negative (retargeted in NAP-4): reading a struct field whose type the bridge
' cannot marshal is a clean error. Pango.Item.analysis is an embedded (inline) struct
' field, which gi_field_info_get_field cannot read into a GIArgument, so it stays
' unsupported. (The original fixture used an array field; NAP-4 added array support,
' so a NULL array field now reads as `nothing` — hence this retarget to a field type
' that is genuinely still unsupported.)
load gi
gi.require("Pango", "1.0")
g = gi.new_struct("Pango.Item")
print gi.struct_get(g, "analysis")
