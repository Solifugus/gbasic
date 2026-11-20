load pg
c = pg.connect({ host: "127.0.0.1", port: number(env("PGCAP_PORT")), database: "pgcap", sslmode: "disable" })
r = pg.query(c, "select id, name, since from customer where id = $1", [1])
print r
pg.close(c)
