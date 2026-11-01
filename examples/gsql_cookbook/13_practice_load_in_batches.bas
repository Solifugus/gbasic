' Recipe 13 (a practice): load in batches. Each commit costs one sync to disk,
' and on slower storage the sync is most of the cost: measured at 10 ms a sync,
' one row per commit wrote 93 rows a second, and 1 000 rows per commit 67 739
' (docs/gsql_scale_measurements.md). So group rows into transactions of a few
' thousand -- big enough to share the sync, small enough to stay light.
load gsql

if exists({dir}"cookbook_load.gsql") then
    remove_tree({dir}"cookbook_load.gsql")
end if

database readings( file: "cookbook_load.gsql", create: true )

sql make on readings
    create table reading (id integer primary key, sensor integer, value number)
end sql

sql index_sensor on readings
    create index reading_sensor on reading (sensor)
end sql

sql add on readings
    insert into reading values (:id, :sensor, :value)
end sql

sql how_many on readings
    select count(*) as n from reading
end sql

sql from_sensor_3 on readings
    select count(*) as n from reading where sensor = 3
end sql

gsql.exec(make)
gsql.exec(index_sensor)

batch = 1000
commits = 0
gsql.begin(readings)
for i = 1 to 2500
    id = i
    sensor = mod(i, 7)
    value = i / 4
    gsql.exec(add)
    if mod(i, batch) = 0 then
        gsql.commit(readings)
        commits = commits + 1
        gsql.begin(readings)
    end if
next
gsql.commit(readings)           ' the last, partial batch
commits = commits + 1

r = gsql.query(how_many)[0]
print string(r.n) + " rows in " + string(commits) + " commits"
print string(gsql.query(from_sensor_3)[0].n) + " readings from sensor 3"

gsql.close(readings)
remove_tree({dir}"cookbook_load.gsql")
