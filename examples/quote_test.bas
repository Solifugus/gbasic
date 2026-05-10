print(quote("hello"))
print(quote("He said \"hi\""))
print(quote("C:\\tmp\\game.bas"))
print(quote(""))
print(quote("line one
line two"))
print(quote("tab	here"))
print(quote(decode("\"carriage\\rreturn\"")))
print(quote("already has \\n text"))
print(decode(quote("round
trip \"ok\" \\ done")))

text = "A room called \"Hall\".
Water drips from C:\\caves."
line = "description = " + quote(text)
print(line)
copy = decode(quote(text))
print(copy)

print(quote(42))
print(quote(true))
print(quote(nothing))
