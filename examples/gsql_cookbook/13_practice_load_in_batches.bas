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
    create table reading (id integer primary key, sensor integer, value number);
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

make.exec()

batch = 1000
commits = 0
readings.begin()
for i = 1 to 2500
    id = i
    sensor = mod(i, 7)
    value = i / 4
    add.exec()
    if mod(i, batch) = 0 then
        readings.commit()
        commits = commits + 1
        readings.begin()
    end if
next
readings.commit()           ' the last, partial batch
commits = commits + 1

r = how_many.query()[0]
print string(r.n) + " rows in " + string(commits) + " commits"
print string(from_sensor_3.query()[0].n) + " readings from sensor 3"

readings.close()
remove_tree({dir}"cookbook_load.gsql")
