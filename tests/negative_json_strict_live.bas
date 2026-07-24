program main(args)
    ' Live/typed values have no faithful JSON form: refuse, never emit a
    ' pointer-like or gBASIC-token representation.
    print(json_encode({ when: now() }))
end program
