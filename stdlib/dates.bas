library dates
    function _next_day_name(name)
        if name = "Monday" then
            return "Tuesday"
        end if
        if name = "Tuesday" then
            return "Wednesday"
        end if
        if name = "Wednesday" then
            return "Thursday"
        end if
        if name = "Thursday" then
            return "Friday"
        end if
        if name = "Friday" then
            return "Saturday"
        end if
        if name = "Saturday" then
            return "Sunday"
        end if
        return "Monday"
    end function

    function _previous_day_name(name)
        if name = "Monday" then
            return "Sunday"
        end if
        if name = "Tuesday" then
            return "Monday"
        end if
        if name = "Wednesday" then
            return "Tuesday"
        end if
        if name = "Thursday" then
            return "Wednesday"
        end if
        if name = "Friday" then
            return "Thursday"
        end if
        if name = "Saturday" then
            return "Friday"
        end if
        return "Saturday"
    end function

    function dayname(d)
        cursor(day)= "2026-05-11"
        name = "Monday"

    forward:
        if cursor = d then
            return name
        end if
        if cursor > d then goto backward
        cursor = cursor + 1 day
        name = _next_day_name(name)
        goto forward

    backward:
        if cursor = d then
            return name
        end if
        cursor = cursor - 1 day
        name = _previous_day_name(name)
        goto backward
    end function

    function days_between(a, b)
        start_date(day)= a
        end_date(day)= b
        count = 0

        if start_date = end_date then
            return 0
        end if
        if start_date > end_date then goto backward

    forward:
        if start_date = end_date then
            return count
        end if
        start_date = start_date + 1 day
        count = count + 1
        goto forward

    backward:
        if start_date = end_date then
            return -count
        end if
        start_date = start_date - 1 day
        count = count + 1
        goto backward
    end function

    function _end_of_month(d)
        current(day)= d

    loop:
        candidate = current + 1 day
        if candidate(month)!= current then
            return current
        end if
        current = candidate
        goto loop
    end function

    function _start_of_month(d)
        current(day)= d

    loop:
        candidate = current - 1 day
        if candidate(month)!= current then
            return current
        end if
        current = candidate
        goto loop
    end function

    function _next_weekday(d, target)
        current(day)= d
        current = current + 1 day

    loop:
        current_name = dayname(current)
        if current_name = target then
            return current
        end if
        current = current + 1 day
        goto loop
    end function

    function _previous_weekday(d, target)
        current(day)= d
        current = current - 1 day

    loop:
        current_name = dayname(current)
        if current_name = target then
            return current
        end if
        current = current - 1 day
        goto loop
    end function

    export modifier end of month for assign
        return _end_of_month(value)
    end modifier

    export modifier start of month for assign
        return _start_of_month(value)
    end modifier

    export modifier next monday for assign
        return _next_weekday(value, "Monday")
    end modifier

    export modifier next tuesday for assign
        return _next_weekday(value, "Tuesday")
    end modifier

    export modifier next wednesday for assign
        return _next_weekday(value, "Wednesday")
    end modifier

    export modifier next thursday for assign
        return _next_weekday(value, "Thursday")
    end modifier

    export modifier next friday for assign
        return _next_weekday(value, "Friday")
    end modifier

    export modifier next saturday for assign
        return _next_weekday(value, "Saturday")
    end modifier

    export modifier next sunday for assign
        return _next_weekday(value, "Sunday")
    end modifier

    export modifier previous monday for assign
        return _previous_weekday(value, "Monday")
    end modifier

    export modifier previous tuesday for assign
        return _previous_weekday(value, "Tuesday")
    end modifier

    export modifier previous wednesday for assign
        return _previous_weekday(value, "Wednesday")
    end modifier

    export modifier previous thursday for assign
        return _previous_weekday(value, "Thursday")
    end modifier

    export modifier previous friday for assign
        return _previous_weekday(value, "Friday")
    end modifier

    export modifier previous saturday for assign
        return _previous_weekday(value, "Saturday")
    end modifier

    export modifier previous sunday for assign
        return _previous_weekday(value, "Sunday")
    end modifier
end library
