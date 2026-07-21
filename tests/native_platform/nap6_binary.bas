' NAP-6: binary-safe capture. The child emits A, NUL, B. byte_count must be 3 (a
' strlen-based capture would report 1), and the interior NUL must survive.
r = process.run({ command: "tests/native_platform/helpers/binary_out.sh" })
print byte_count(r.stdout)
print byte_at(r.stdout, 0)
print byte_at(r.stdout, 1)
print byte_at(r.stdout, 2)
