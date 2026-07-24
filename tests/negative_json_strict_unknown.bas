program main(args)
    ' `unknown` is gBASIC's NA; JSON has no such value, so strict encoding must
    ' REFUSE it rather than invent a token or silently coerce it to null.
    print(json_encode({ a: 1, b: unknown }))
end program
