' PLAT-STDERR: strict source-order alternation across the two streams.
'
' Run with both streams pointed at ONE destination, the interleaving this produces
' is the evidence for how the streams' buffering differs: stderr is unbuffered, so
' its lines leave immediately, while stdout on a pipe is block-buffered until the
' flag says otherwise.
i = 1
while i <= 4
    print "OUT-" + i
    print to error "ERR-" + i
    i = i + 1
end while
