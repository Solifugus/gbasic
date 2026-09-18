' ari_discover -- THE VALGRIND FIXTURE, and it is a different job from
' discover_test.bas.
'
' WHY IT EXISTS. The semantics fixture scores the library against the corpus's
' answer key, so it grows with the corpus -- 24 branch sources, 12 null sources
' and now 5 teller journals, each profiled several times. That is the right
' shape for a measurement and the wrong shape for valgrind, which costs 20-40x:
' at 45 seconds native the valgrind tier passed ten MINUTES, and a tier whose
' runtime grows with the fixture will eventually blow the gate's per-suite cap
' however fast the interpreter gets.
'
' WHAT VALGRIND IS ACTUALLY FOR HERE, and it is not coverage of the corpus. The
' library is pure gBASIC: it introduces no value kind and no allocation of its
' own, so what a memory tier can catch is the INTERPRETER mishandling the paths
' this library drives -- deep record and array copies, copy-on-write detaches,
' regex values, string slicing. Those are exercised by ONE pass through each
' public function, not by the twenty-fourth source.
'
' So this fixture is PATH-COMPLETE AND SMALL: every public function once, both
' recommendation branches of `variants`, and the refusal paths, over eight
' sources rather than forty-one. It asserts almost nothing -- that is the other
' fixture's job -- but it must still RUN CLEAN, so a raise here fails the tier
' as loudly as a leak would.
'
' THE TRIPWIRE THAT KEEPS THEM IN STEP lives in the runner: every public
' function of the library must be called here by name. Without it this fixture
' silently stops covering whatever was added last, and a valgrind tier that has
' stopped covering the new code is exactly the kind of green line this project
' distrusts.

load ari_discover

function slurp(path)
    f {file}= path
    return read(f)
end function

dir = "examples/fixtures/ari_discover/"

branches = []
bi = 1
while bi <= 3
    n = replace(mid(string(100 + bi), 1, 2), " ", "0")
    append(branches, { id: n + "_branch",
                       text: slurp(dir + n + "_branch_activity.rpt") })
    bi = bi + 1
end while

tellers = []
ti = 1
while ti <= 3
    n = "t" + replace(mid(string(100 + ti), 1, 2), " ", "0")
    append(tellers, { id: n, text: slurp(dir + "variants/" + n + "_teller_session.rpt") })
    ti = ti + 1
end while

nulls = []
ni = 1
while ni <= 2
    n = replace(mid(string(100 + ni), 1, 2), " ", "0")
    append(nulls, { id: "null_" + n, text: slurp(dir + "null/null_" + n + "_noise.rpt") })
    ni = ni + 1
end while

' --- the small pure functions ---------------------------------------------
o = ari_discover.default_options()
print ("options " + string(count(ari_discover.option_names())))
print ("kinds   " + string(count(ari_discover.token_kinds())))
line = "  00147454    REYES, YUKI              03/19/2026         947.08"
print ("spans   " + string(count(ari_discover.spans(line))))
print ("sig     " + ari_discover.signature(line))
print ("shape   " + ari_discover.shape(line))

' --- one source, every structural pass ------------------------------------
g = ari_discover.grid(branches[0].id, branches[0].text)
ps = ari_discover.page_starts(g)
f = ari_discover.furniture(g)
fams = ari_discover.families(g, f.lines)
print ("grid    " + string(count(g)) + " rows, " + string(count(fams)) + " families")
print ("pages   " + string(count(ps.starts)) + " starts, " + ps.evidence)
print ("furn    " + string(count(ari_discover.furniture_directive(f))) + " directive lines")

p1 = ari_discover.profile(branches[0].text)
p2 = ari_discover.profile_source(branches[0])
print ("profile " + string(p2.content_lines) + " content lines")
print (ari_discover.explain(p2))

c = ari_discover.profile_corpus(branches)
print ("corpus  " + string(c.sources) + " sources, "
       + string(count(c.shared_signatures)) + " shared signatures")
cf = ari_discover.corpus_furniture(c.profiles)
print ("cfurn   " + string(count(cf.directive)) + " lines")

' The dominant family, then everything hung off it.
det = unknown
for each fm in c.profiles[0].families
    if is_unknown(det) then
        det = fm
    else
        if fm.count > det.count then
            det = fm
        end if
    end if
end for
gut = ari_discover.gutters(g, det.lines)
inf = ari_discover.infer_fields(g, det, f.lines)
sc2 = ari_discover.sections(g, c.profiles[0].families, det, f.lines)
cl = ari_discover.corpus_labels(c.profiles, det.signature)
print ("gutters " + string(count(gut)) + "   fields " + string(count(inf.fields)))
print ("labels  " + ari_discover.label_token(cl.headings))
flat = ari_discover.spec_text(det, inf)
print ("flat    " + string(len(flat)) + " chars")

' --- inference, validation, scoring ---------------------------------------
prop = ari_discover.infer(branches)
print ("infer   ok=" + string(prop.ok) + "  fields=" + string(count(prop.fields))
       + "  questions=" + string(count(prop.questions)))
print (ari_discover.explain(prop))
v = ari_discover.validate(branches, prop.spec)
rc = ari_discover.region_coverage(branches, prop.spec, prop.family.signature)
st = ari_discover.anchor_stability(branches, prop.family.signature)
print ("scored  parsed=" + string(v.parsed) + "  regions=" + string(rc.agreeing)
       + "  layouts=" + string(st.layouts))

hp = ari_discover.infer(branches, { holdout: 1 })
print ("holdout " + string(hp.holdout.sources))

' --- §13 variants: both recommendation branches and both refusals ----------
mixed = []
for each s in branches
    append(mixed, s)
end for
for each s in tellers
    append(mixed, s)
end for

print ("grammar " + ari_discover.grammar_key(c.profiles[0]))
vb = ari_discover.variants(branches)
vm = ari_discover.variants(mixed)
vs = ari_discover.variants(mixed, { minimum_variant_sources: 4 })
vn = ari_discover.variants(nulls)
print ("variants " + vb.recommendation + " / " + vm.recommendation + " / "
       + vs.recommendation + " / " + vn.recommendation)

' --- §10 decisions and refinement -----------------------------------------
print ("dkinds  " + join(ari_discover.decision_kinds(), ", "))
print ("dfields " + join(ari_discover.decision_fields("rename_field"), ", "))
print ("conv    " + join(ari_discover.conversion_names(), ", "))
ds = ari_discover.check_decisions([ { decision: "rename_field",
                                      field: prop.fields[0].name,
                                      to: "renamed" } ])
r1 = ari_discover.refine(branches, prop, ds)
r2 = ari_discover.refine(branches, prop, decode(encode(ds)))
print ("refine  same=" + string(r1.spec = r2.spec)
       + "  fields=" + string(count(r1.fields))
       + "  alts=" + string(count(r1.alternatives)))
r3 = ari_discover.refine(branches, prop,
                         [ { decision: "drop_field", field: prop.fields[0].name },
                           { decision: "field_type", field: prop.fields[1].name,
                             as: "text" } ])
print ("refine2 fields=" + string(count(r3.fields)))
r4 = ari_discover.refine(branches, prop,
                         [ { decision: "variant",
                             sources: [ branches[0].id, branches[1].id ] } ])
print ("variant trained_on=" + string(r4.trained_on))
nest = ari_discover.spec_text_nested(det, inf, unknown, unknown)
print ("nested  " + string(len(nest)) + " chars")

' --- refusal paths --------------------------------------------------------
on error goto next
bad = ari_discover.infer(branches, { minimum_suport: 0.9 })
if error then
    print ("refused " + mid(error.message, 0, 30))
    error.clear()
end if
bad2 = ari_discover.variants("not an array")
if error then
    print ("refused " + mid(error.message, 0, 30))
    error.clear()
end if
bad3 = ari_discover.infer(branches, { holdout: 99 })
if error then
    print ("refused " + mid(error.message, 0, 30))
    error.clear()
end if
bad4 = ari_discover.refine(branches, prop,
                           [ { decision: "rename_field", field: "nope", to: "x" } ])
if error then
    print ("refused " + mid(error.message, 0, 34))
    error.clear()
end if
bad5 = ari_discover.check_decisions([ { decision: "nope" } ])
if error then
    print ("refused " + mid(error.message, 0, 30))
    error.clear()
end if
on error stop

nr = ari_discover.infer(nulls)
print ("null    ok=" + string(nr.ok))
print "done"
