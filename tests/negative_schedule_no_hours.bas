' A calendar without an hours window cannot be scheduled into; refused by name.
program main(args)
    load dates from "../stdlib/dates.bas"
    load schedule from "../stdlib/schedule.bas"
    d {date}= "2026-08-17"
    print schedule.slots(d, { length: 20 minutes }, dates.calendar({}))
end program
