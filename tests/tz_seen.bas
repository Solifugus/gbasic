' Prints the TZ this interpreter was actually given, for suites whose premise is
' that a zone REACHES the binary. Under MSYS2 an Area/City zone does not: MSYS2
' removes TZ from a native program's environment when the value is not one the
' C runtime can parse (UTC passes, Australia/Sydney does not -- measured). A
' suite asks this first and skips by name, rather than reading a zone that never
' arrived as the binary getting local time wrong.
print(string(env("TZ")))
