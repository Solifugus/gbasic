program main(args)
    ' NaN/infinity are not JSON numbers. The historical encode emitted bare
    ' `nan`/`inf`, which not even gBASIC's own decode accepts.
    print(json_encode({ x: number("nan") }))
end program
