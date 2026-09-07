' `llm.embed` -- batch embeddings.
' Self-checking; run by tests/run_llm_transcript.sh.
'
' BATCH IS THE PRIMITIVE and one text is the special case: an embedding API
' charges and rate-limits per request, so a chunked document embedded one chunk
' at a time is the same work at many times the cost.
'
' THE LOAD-BEARING TIER IS ORDER. The API returns a `data` array whose entries
' carry an `index`, and nothing in the protocol promises they arrive sorted.
' Read positionally, every chunk is paired with ANOTHER CHUNK'S VECTOR -- and
' the result still looks like a list of vectors, the store still fills, and
' retrieval returns the wrong documents forever. Nothing raises, ever. So the
' fixture feeds a DELIBERATELY SHUFFLED response, which is the only way to tell
' a placement by index from a placement by arrival.

load llm

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' Rows OUT OF ORDER, which the protocol permits.
function shuffled(m, req)
    body = { data: [
        { index: 2, embedding: [0.3, 0.3] },
        { index: 0, embedding: [0.1, 0.1] },
        { index: 1, embedding: [0.2, 0.2] }
    ] }
    return { status: 200, headers: {}, body: json_encode(body) }
end function

' In order, so the two runs differ only in the response's ordering.
function ordered(m, req)
    body = { data: [
        { index: 0, embedding: [0.1, 0.1] },
        { index: 1, embedding: [0.2, 0.2] },
        { index: 2, embedding: [0.3, 0.3] }
    ] }
    return { status: 200, headers: {}, body: json_encode(body) }
end function

function short_batch(m, req)
    return { status: 200, headers: {},
             body: json_encode({ data: [ { index: 0, embedding: [0.1] } ] }) }
end function

function no_index(m, req)
    return { status: 200, headers: {},
             body: json_encode({ data: [ { embedding: [0.1] } ] }) }
end function

function out_of_range(m, req)
    return { status: 200, headers: {},
             body: json_encode({ data: [ { index: 7, embedding: [0.1] } ] }) }
end function

base = llm.with_embed_model(llm.openai("gpt-x", "k"), "text-embedding-3-small")
texts = ["first", "second", "third"]

' --- THE ORDER --------------------------------------------------------------
v = llm.embed(llm.with_transport(base, shuffled), texts)
check("a shuffled response is placed BY INDEX, not by arrival", v[0][0], 0.1)
check("the middle one too", v[1][0], 0.2)
check("and the last", v[2][0], 0.3)
' The control: the same three vectors arriving in order give the same answer.
' Without it, "placed by index" is satisfied by a reversal that happens to undo
' this particular shuffle.
w = llm.embed(llm.with_transport(base, ordered), texts)
check("an ordered response gives the SAME vectors", string(v), string(w))
check("one vector per text", count(v), 3)

' --- refusals, each beside its nearest legal neighbour -----------------------
on error goto next

x = llm.embed(llm.with_embed_model(llm.anthropic("c", "k"), "e"), texts)
if error then
    check("Anthropic has no embeddings endpoint, and is told so by name",
          contains(error.message, "no embeddings endpoint"), true)
    error.clear()
end if

x = llm.embed(llm.openai("gpt-x", "k"), texts)
if error then
    check("an embedding model is a DIFFERENT model, and must be named",
          contains(error.message, "different model from the chat one"), true)
    error.clear()
end if

x = llm.embed(base, "not an array")
if error then
    check("batch is the primitive: embed takes an array",
          contains(error.message, "array of texts"), true)
    error.clear()
end if

x = llm.embed(base, [])
if error then
    check("an empty batch is refused rather than answered with nothing",
          contains(error.message, "nothing to embed"), true)
    error.clear()
end if

x = llm.embed(base, [1, 2])
if error then
    check("and the texts must be strings", contains(error.message, "expects strings"), true)
    error.clear()
end if

x = llm.embed(llm.with_transport(base, short_batch), texts)
if error then
    check("a short batch is refused, naming both counts",
          contains(error.message, "asked for 3 embeddings and got 1"), true)
    error.clear()
end if

x = llm.embed(llm.with_transport(base, no_index), ["one"])
if error then
    check("a row with no index is refused -- its text cannot be identified",
          contains(error.message, "no index"), true)
    error.clear()
end if

x = llm.embed(llm.with_transport(base, out_of_range), ["one"])
if error then
    check("and an index outside the batch is refused",
          contains(error.message, "outside the batch"), true)
    error.clear()
end if

' The control: the same call, well formed, still works.
ok = llm.embed(llm.with_transport(base, ordered), texts)
check("the control: a well-formed batch still answers", count(ok), 3)
check("with vectors, not placeholders", is_array(ok[0]), true)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
