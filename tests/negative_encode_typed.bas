program main(args)
    ' `encode` is lenient about `unknown`, which decode reads back, but it still
    ' refuses typed and live values -- emitting a lossy token would produce text
    ' that silently does not round-trip. This is the boundary PLAT-RENDER had to
    ' not cross: `string()` and `print` gained a TOTAL display mode over the same
    ' walker, and if that mode leaked into `encode` this test stops failing.
    d(date)= "2026-05-15"
    print(encode({ when: d }))
end program
