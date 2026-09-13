' What a statement touches -- no database required.
load discovery

sql = ("insert into warehouse.fact_volume (deal_id, gross_vol_mmbtu) " +
       "select d.id, d.gross_vol_mmbtu from warehouse.stg_deal d " +
       "join trading.ctp k on k.code = d.ctp_code where k.is_active = 1")

r = discovery.references(sql)
print "reads:  " + join(sort(r.reads), ", ")
print "writes: " + join(sort(r.writes), ", ")
print "gaps:   " + string(count(r.gaps))
