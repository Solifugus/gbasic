' A column's derivation, read out of the SQL that produces it.
load discovery

body = ("insert into warehouse.fact_volume (deal_id, avail_after_pvr) " +
        "select s.id, s.gross_vol_mmbtu * (1 - s.pvr_pct) from warehouse.stg_deal s")

for each wrote in discovery.derivations(body)
    print "target: " + wrote.target + "  (" + wrote.kind + ")"
    for each col in wrote.columns
        print "  " + col.output + " <- " + col.expression
        print "    from: " + join(sort(col.sources), ", ")
    end for
end for
