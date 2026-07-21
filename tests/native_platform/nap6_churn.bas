' NAP-6: run many short children to expose fd leaks / zombies / buffer leaks.
i = 0
ok = 0
while i < 100
    r = process.run({ command: "true" })
    if r.exit_code = 0 then
        ok = ok + 1
    end if
    i = i + 1
end while
print ok
