' Multiprocessing Phase 0: serialize/deserialize round-trips (docs/multiprocessing_design.md §5).
print(deserialize(serialize(42)) = 42)
print(deserialize(serialize("hello")) = "hello")
print(deserialize(serialize(true)) = true)
print(deserialize(serialize(nothing)) = nothing)
print(is_unknown(deserialize(serialize(unknown))))

' binary-safe: an interior NUL survives the round-trip
s = "a" + chr(0) + "b"
print(deserialize(serialize(s)) = s)
print(byte_count(deserialize(serialize(s))))

' array and record, including nesting
a = deserialize(serialize([10, 20, 30]))
print(a[0] + a[1] + a[2])
r = deserialize(serialize({name: "Ada", age: 36}))
print(r["name"])
print(r["age"])
n = deserialize(serialize({tags: ["x", "y"], inner: {k: 7}}))
print(n["tags"][1])
print(n["inner"]["k"])

' typed values
d{date}= "2026-06-25"
print(deserialize(serialize(d)) = d)
m{USD}= 19.95
print(deserialize(serialize(m)) = m)

' the serialized form is itself a binary-safe string
print(is_string(serialize({a: 1})))
