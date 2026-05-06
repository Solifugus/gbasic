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
    load text
    load stricttext

    name = "Joe"

    if name(text.caseless)= "joe" then
        print("text match")
    end if

    if name(stricttext.caseless)= "joe" then
        print("strict match")
    end if
end program
