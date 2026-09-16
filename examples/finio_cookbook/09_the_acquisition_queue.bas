' Recipe 9 — The registry: what exists, what may be built, and what may not.
'
' §9 and §10. The point is stated in one line of the design: finding that a
' format exists is not the same as possessing enough legitimate information to
' implement it.
'
' So a registry entry records where the specification came from, when it was
' retrieved, and whether the rights permit implementing it. An entry claiming
' `implemented` with no specification source is REFUSED — that is exactly the
' claim a registry exists to keep honest.
'
' The acquisition classes say why a format is or is not reachable:
'
'   OPEN            published, free, implementable now
'   PUBLIC_VENDOR   a vendor publishes it openly
'   DE_FACTO        the authority does not publish, but consistent public
'                   implementation documentation exists
'   CONTROLLED      the specification is licensed; a person must buy it
'   HUMAN_REQUIRED  reachable, but not by an automated fetch
'   INSUFFICIENT    not enough public information to build from
'
' CONTROLLED is not a to-do. It is a queue item that needs a purchase order,
' and saying so is more useful than a guess built from reverse-engineering.
program main()
    load finio
    load finio_all
    load finio_registry

    entries = finio_registry.all(finio_all.adapters())

    print "-- what the queue holds"
    cov = finio_registry.coverage(entries)
    print ("  formats:   " + string(cov.entries))
    print ("  families:  " + join(cov.families_present, ", "))
    print ("  §14 names " + string(cov.design_domains) + " candidate domains")
    print ""

    print "-- by acquisition class"
    byclass = finio_registry.by_acquisition_class(entries)
    for each c in finio.acquisition_classes()
        ids = byclass[c]
        if count(ids) > 0 then
            print ("  " + c + ": " + join(ids, ", "))
        end if
    end for
    print ""

    print "-- what can be built from what we legitimately hold"
    print ("  implementable: " + string(count(finio_registry.implementable(entries))))
    print ("  blocked:       " + string(count(finio_registry.blocked(entries))))
    for each b in finio_registry.blocked(entries)
        print ("    " + b.id + " (" + b.acquisition_class + ")")
        print ("      " + b.blocked_by)
    end for
    print ""

    ' The evidence a shipped adapter carries. `finio.check_registry_entry`
    ' refuses an entry that claims more than it can show.
    print "-- the evidence behind one shipped adapter"
    for each e in entries
        if e.id = "bai2" then
            for each s in e.specification_sources
                print ("  " + s.source_type + ", retrieved " + s.date_retrieved)
            end for
            print ("  implementation allowed:       " + string(e.implementation_allowed))
            print ("  spec may be redistributed:    " + string(e.spec_redistribution_allowed))
            print ("  samples may be redistributed: " + string(e.sample_redistribution_allowed))
        end if
    end for
    print ""

    ' THE STATE AND THE PER-CAPABILITY STATUSES ARE DIFFERENT AXES, and keeping
    ' them apart is the whole reason for having both. `aba.nacha` has been run
    ' against ten ACH files this project did not write, so its recognition and
    ' reading are `verified` — but no specification was ever obtained (the
    ' Nacha Operating Rules are a paid publication), so the entry's own state
    ' stays `researched`. Collapsing the two would have to lie in one direction
    ' or the other.
    print "-- one entry, two axes"
    for each e in entries
        if e.id = "aba.nacha" then
            print ("  state              " + e.state)
            print ("  acquisition class  " + e.acquisition_class)
            print ("  recognition        " + e.recognition_status)
            print ("  read               " + e.read_status)
            print ("  validation         " + e.validation_status)
            print ("  write              " + e.write_status)
        end if
    end for
end program
