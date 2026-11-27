' Recall and speed of G-SQL's HNSW index (docs/gsql_vector_design.md §9),
' judged by the exact search over the same rows (`plain`, no index) and by
' nearest() in memory, never by the index itself. Prints one line per
' measure for tests/run_gsql_vector.sh to hold:
'   latent RECALL         embedding-like data (clusters in a 16-dimensional
'                         latent space, projected), the default beam
'   hard LOW HIGH         near-orthogonal clusters, ef_search 1 and 200: the
'                         attribute must reach the search, so LOW < HIGH
'   speed EXACT_MS ANN_MS the same queries, exact against the index
'   agree N               how many top-10s nearest() and the exact SQL agree on
' Arguments: rows, width. Run from inside an empty directory.
load gsql

program main(args)
    seed(20261127)   ' the same rows every run, so a measure is a fact, not a draw
    n = number(args[0])
    width = number(args[1])
    nq = 30
    db = gsql.connect("recall.gsql", { create: true })
    proj = []
    for j = 0 to 15
        append(proj, rnd_vec(width))
    next
    centres = []
    for c = 0 to 99
        append(centres, array(rnd_vec(16) * 4))
    next
    vs = []
    for i = 0 to n - 1
        append(vs, embed(proj, centres[mod(i * 7, 100)]))
    next
    queries = []
    for qi = 0 to nq - 1
        append(queries, embed(proj, centres[mod(qi * 13 + 1, 100)]))
    next
    load_table(db, "idx", vs, width, true)
    load_table(db, "plain", vs, width, false)
    hit = 0
    agree = 0
    t_exact = 0
    t_ann = 0
    for each q, qi in queries
        sql = "select id from TBL order by e <=> ? limit 10"
        t0 = monotonic()
        exact = db.query(replace(sql, "TBL", "plain"), [q])
        t1 = monotonic()
        got = db.query(replace(sql, "TBL", "idx"), [q])
        t2 = monotonic()
        t_exact = t_exact + t1 - t0
        t_ann = t_ann + t2 - t1
        hit = hit + overlap(got, exact)
        truth = []
        for each h in nearest(q, vs, 10, "cosine")
            append(truth, { id: h.index })
        next
        if overlap(truth, exact) = 10 then
            agree = agree + 1
        end if
    next
    print "latent " + string(hit / (nq * 10))
    print "speed " + string(1000 * t_exact / nq) + " " + string(1000 * t_ann / nq)
    print "agree " + string(agree)
    ' near-orthogonal clusters, where a greedy descent has little to follow
    bases = []
    for b = 0 to 199
        append(bases, rnd_vec(width))
    next
    hard = []
    for i = 0 to n - 1
        append(hard, bases[mod(i, 200)] + bases[mod(i * 7 + 3, 200)] * (mod(i, 97) / 97))
    next
    db.exec("drop table idx")
    db.exec("drop table plain")
    load_table(db, "idx", hard, width, true)
    rates = []
    for each ef in [1, 200]
        db.ef_search = ef
        h = 0
        for qi = 0 to nq - 1
            q = bases[mod(qi * 31 + 7, 200)] * 0.7 + bases[mod(qi * 17 + 2, 200)] * 0.4
            truth = []
            for each t in nearest(q, hard, 10, "cosine")
                append(truth, { id: t.index })
            next
            h = h + overlap(db.query("select id from idx order by e <=> ? limit 10", [q]), truth)
        next
        append(rates, string(h / (nq * 10)))
    next
    print "hard " + join(rates, " ")
end program

function rnd_vec(w)
    xs = []
    for i = 0 to w - 1
        append(xs, random() - 0.5)
    next
    return vector(xs)
end function

function embed(proj, z)
    v = proj[0] * (z[0] + (random() - 0.5) * 0.8)
    for j = 1 to len(z) - 1
        v = v + proj[j] * (z[j] + (random() - 0.5) * 0.8)
    next
    return v
end function

function load_table(db, name, vs, width, indexed)
    db.exec("create table " + name + " (id integer primary key, e vector(" + string(width) + "))")
    if indexed then
        db.exec("create index " + name + "_e on " + name + " using hnsw (e vector_cosine_ops)")
    end if
    batch = []
    for each v, i in vs
        append(batch, { id: i, e: v })
        if len(batch) = 500 then
            gsql.insert(db, name, batch)
            batch = []
        end if
    next
    if len(batch) > 0 then
        gsql.insert(db, name, batch)
    end if
    return nothing
end function

function overlap(a, b)
    n = 0
    for each x in a
        for each y in b
            if x.id = y.id then
                n = n + 1
            end if
        next
    next
    return n
end function
