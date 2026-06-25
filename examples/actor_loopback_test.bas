' Multiprocessing Phase 1: actor mailbox loopback (docs/multiprocessing_design.md §3-§4).
' One actor (the root) sends messages to its own mailbox and receives them back
' over a real socket, exercising the value <-> bytes <-> transport path.
print(type(self()))
print(self() = self())

send(self(), "hello")
print(receive())

send(self(), 41)
print(receive() + 1)

send(self(), {name: "Ada", tags: ["x", "y"]})
r = receive()
print(r["name"])
print(r["tags"][1])

' FIFO ordering from a single sender
send(self(), "one")
send(self(), "two")
send(self(), "three")
print(receive())
print(receive())
print(receive())

' a binary-safe message (interior NUL) survives the mailbox intact
send(self(), "a" + chr(0) + "b")
print(byte_count(receive()))
