' Phase 3: ASCII-only case folding; non-ASCII passes through unchanged.
print(upper("hello"))
print(lower("HELLO"))
print(upper("café"))
print(lower("CAFÉ"))
print(upper("Ω greek ß"))
name = "Bob"
if name {caseless}= "bob" then
    print("caseless ascii match")
end if
if "café" {caseless}= "CAFÉ" then
    print("non-ascii folded (should NOT print)")
end if
if "café" {caseless}= "CAFé" then
    print("ascii part folds, non-ascii exact")
end if
