' Reaches the publishers. Run only via FINIO_RATES_LIVE=1; asserts that the
' feeds ANSWER, never what they say, since tomorrow's rate is not today's.
load finio_rates
program main( args )
    for each id in [ "nyfed_sofr", "treasury_avg_interest" ]
        s = finio_rates.fetch(id, { limit: 5 })
        if not s.ok then
            error "finio_rates live: " + id + " did not answer"
        end if
        if count(s.observations) = 0 then
            error "finio_rates live: " + id + " answered with no observations"
        end if
        print id + " ok, " + string(count(s.observations)) + " observations"
    end for
end program
