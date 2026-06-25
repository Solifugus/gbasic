' Phase 1: binary-safe strings, codepoint chr/code, byte builtins.
print(chr(128512))
print(code(chr(128512)))
print(code("é"))
print(chr(233))
print(byte_count("café"))
print(byte_at("café", 5))
print(from_bytes([72, 105]))
nul = "a" + chr(0) + "b"
print(byte_count(nul))
print(byte_at(nul, 2))
round = decode(encode(nul))
print(byte_count(round))
print(round = nul)
