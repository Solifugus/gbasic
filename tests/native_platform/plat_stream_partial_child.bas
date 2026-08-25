' PLAT-STREAM child fixture for the partial-line case.
'
' `print` always terminates its line, so the ONLY way this runtime emits a partial
' line is an `input` prompt -- which the interpreter already fflushes explicitly
' (src/eval.c, the `input` builtin). This fixture exercises exactly that, and ends
' its output with an unterminated line so the total is byte-checked without a
' trailing newline.
'
' MUST be run with stdin at EOF (`< /dev/null`); `input` then returns "" at once.
program main(args)
    ready{file} = args[0]
    gate{file} = args[1]

    print "LINE"
    answer = input("PROMPT>")
    write(ready, "")

    while not exists(gate)
        sleep(0.01)
    end while

    print "ANSWER=<" + answer + ">"
    tail = input("TAIL>")
end program
