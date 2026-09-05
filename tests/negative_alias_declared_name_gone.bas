' An alias REPLACES the declared name rather than adding to it: the library was
' loaded as `ta`, so `toolkit.` names a library nothing loaded. Two names for
' one import would be the ambiguity aliasing exists to remove.
load toolkit from "libs/vendor_a/toolkit.bas" as ta
print toolkit.describe()
