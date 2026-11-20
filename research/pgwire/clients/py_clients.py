import sys
port=int(sys.argv[1]); which=sys.argv[2]
dsn=f"host=127.0.0.1 port={port} dbname=pgcap sslmode=disable"
if which=='psycopg3':
    import psycopg
    with psycopg.connect(dsn) as c:
        c.execute("select id, name, since from customer where id = %s",(1,)).fetchall()
        c.execute("select total from invoice").fetchall()
        with c.cursor() as cur:
            cur.execute("insert into customer(name) values (%s) returning id",("Cy",)); cur.fetchone()
        c.rollback()
elif which=='psycopg2':
    import psycopg2
    c=psycopg2.connect(dsn); cur=c.cursor()
    cur.execute("select id, name, since from customer where id = %s",(1,)); cur.fetchall()
    cur.execute("select total from invoice"); cur.fetchall(); c.rollback(); c.close()
elif which=='sqlalchemy':
    import sqlalchemy as sa
    e=sa.create_engine(f"postgresql+psycopg://@127.0.0.1:{port}/pgcap?sslmode=disable")
    i=sa.inspect(e)
    for t in i.get_table_names():
        i.get_columns(t); i.get_pk_constraint(t); i.get_foreign_keys(t); i.get_indexes(t)
    i.get_view_names(); i.get_schema_names()
elif which=='pandas':
    import pandas as pd, sqlalchemy as sa
    e=sa.create_engine(f"postgresql+psycopg://@127.0.0.1:{port}/pgcap?sslmode=disable")
    pd.read_sql("select * from invoice", e)
    pd.DataFrame({'a':[1,2],'b':['x','y']}).to_sql('pd_tmp', e, if_exists='replace', index=False)
    with e.begin() as c: c.exec_driver_sql("drop table pd_tmp")
