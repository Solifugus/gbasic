' The same, where TWO loaded libraries define the name. The message names both
' and offers both, because there is nothing here for the language to choose on.
program main(args)
    load scope_alpha from "libs/scope_alpha.bas"
    load scope_beta from "libs/scope_beta.bas"
    print shared()
end program
