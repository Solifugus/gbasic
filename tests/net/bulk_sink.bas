' A WAITED READER BEHIND A FASTER PEER (run_net.sh tier 10): length-prefixed
' packets taken with read_exactly, timed with monotonic. Prints the bytes and
' the seconds. The runner compares two buffer sizes: a refill must cost what
' was consumed, not what the buffer holds.
'
' args: [ready file, packets, buffer bytes]
load net

program main(args)
    packets = number(args[1])
    l = net.listen({ port: 0, buffer: number(args[2]) })
    write(args[0], string(l.port))
    c = l.accept(30 seconds)
    got = 0
    t0 = monotonic()
    for i = 1 to packets
        n = unpack(">I", c.read_exactly(4, 30 seconds))[0]
        got = got + byte_count(c.read_exactly(n, 30 seconds))
    end for
    t1 = monotonic()
    c.close()
    l.close()
    print string(got) + " " + string(t1 - t0)
end program
