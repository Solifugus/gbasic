' Two reports, both called total_volume, both correct. Why do they differ?
load discovery

gross = ("select effective_date, sum(gross_vol_mmbtu) as total_volume " +
         "from warehouse.fact_volume group by effective_date")
net = ("select effective_date, sum(avail_after_pvr) as total_volume " +
       "from warehouse.fact_volume where deal_status = 'ACTIVE' group by effective_date")

' `name` is the COLUMN being compared, not the module -- both reports call it
' total_volume, which is the whole difficulty.
d = discovery.explain({ name: "total_volume", body: gross },
                      { name: "total_volume", body: net })
print "shared ancestor: " + join(sort(d.shared), ", ")
if d.identical then
    print "the two derivations are identical"
else
    print "why they differ:"
    for each why in d.differences
        print "  - " + why
    end for
end if
