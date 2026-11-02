' Recipe 17: WITH RECURSIVE -- walking a hierarchy of unknown depth: a bill
' of materials exploded into total parts, and a cycle in the data caught and
' reported rather than run forever.
load gsql

if exists({dir}"cookbook_recursive.gsql") then
    remove_tree({dir}"cookbook_recursive.gsql")
end if

database plant( file: "cookbook_recursive.gsql", create: true )

sql make on plant
    create table bom (id integer primary key, assembly text, component text, qty integer);
    insert into bom values
        (1, 'robot-arm', 'joint', 3), (2, 'robot-arm', 'gripper', 1),
        (3, 'joint', 'motor', 1), (4, 'joint', 'bolt', 6),
        (5, 'gripper', 'motor', 2), (6, 'gripper', 'finger', 3),
        (7, 'finger', 'bolt', 2)
end sql

' every part of a robot arm and how many of each: the anchor is the arm's own
' components, and each round goes one level down, multiplying the quantities
sql explode on plant
    with recursive parts (part, qty, depth) as (
        select component, qty, 1 from bom where assembly = 'robot-arm'
        union all
        select b.component, p.qty * b.qty, p.depth + 1
        from parts p join bom b on b.assembly = p.part
    )
    select part, sum(qty) as total, max(depth) as deepest
    from parts
    group by part
    order by part
end sql

' a mistake in the data: a finger listed as made of a gripper
sql mistake on plant
    insert into bom values (8, 'finger', 'gripper', 1)
end sql

' CYCLE watches the part: a row whose part is already on its route is marked
' and goes no further, so the walk ends and the loop can be reported
sql loops on plant
    with recursive parts (part, depth) as (
        select component, 1 from bom where assembly = 'robot-arm'
        union all
        select b.component, p.depth + 1
        from parts p join bom b on b.assembly = p.part
    ) cycle part set looped using route
    select part, route from parts where looped
end sql

make.exec()

print "Parts in one robot arm:"
for each r in explode
    print "  " + r.part + "  x" + string(r.total) + "  (down to level " + string(r.deepest) + ")"
next

mistake.exec()
print "Loops in the bill of materials:"
for each r in loops
    print "  " + r.part + " is inside itself: " + join(r.route, " > ")
next

plant.close()
remove_tree({dir}"cookbook_recursive.gsql")
