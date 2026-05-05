library private_mods
    modifier whisper for assign
        return lower(value)
    end modifier
end library

library exported_mods
    export modifier shout for assign
        return upper(value)
    end modifier
end library

library imported_base
    export modifier marker for assign
        return upper(value)
    end modifier
end library

library first_mods
    export modifier pick for assign
        return "first"
    end modifier
end library

library second_mods
    export modifier pick for assign
        return "second"
    end modifier
end library

library text
    export modifier caseless for compare
        return compare(lower(left), operator, lower(right))
    end modifier
end library

library stricttext
    export modifier caseless for compare
        return left = right
    end modifier
end library

program demo(args)
    use private_mods
    use exported_mods
    use imported_base
    use first_mods
    use second_mods
    use text
    use stricttext

    on error resume next
    hidden(whisper)= "HELLO"
    if error then
        print "private modifier unavailable"
        error.clear()
    end if
    on error stop

    loud(shout)= "hello"
    print loud

    modifier marker for assign
        return lower(value)
    end modifier

    local(marker)= "LOCAL"
    print local

    selected(pick)= "ignored"
    print selected

    name = "Joe"
    if name(text.caseless)= "joe" then
        print "qualified text match"
    end if
    if name(stricttext.caseless)= "joe" then
        print "qualified strict match"
    end if
end program
