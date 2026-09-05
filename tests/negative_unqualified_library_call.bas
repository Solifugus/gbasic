' An unqualified call to a function a LOADED LIBRARY defines. It used to
' resolve, by scanning every imported function newest-first -- so which library
' a call meant was decided by load order, which the author of the call does not
' control and cannot see. The qualified form is exercised as a control in
' tests/scope_test.bas; nothing may run here before the refusal.
program main(args)
    load scope_alpha from "libs/scope_alpha.bas"
    print only_alpha()
end program
