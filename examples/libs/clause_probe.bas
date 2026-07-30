' Fixture for examples/clause_recognition_test.bas (PLAT-CLAUSE).
'
' These functions exist in a SEPARATE FILE on purpose. The clause lookahead's
' function check (`source_declares_function`, src/parser.y) re-scans only the
' file being parsed, so a function declared here is invisible to it — which is
' precisely the class B defect. A same-file stand-in would not reproduce it.
library clause_probe
    function kind(x)
        return "record"
    end function

    function one(x)
        return x
    end function
end library
