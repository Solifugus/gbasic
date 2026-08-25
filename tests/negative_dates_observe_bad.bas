' An unknown observation policy is refused by name.
program main(args)
    load dates from "../stdlib/dates.bas"
    d {date}= "2026-07-04"
    print dates.calendar({ holidays: [d], observe: "sideways" })
end program
