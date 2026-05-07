i = 0
while true
    if i = 3 then
        break
    end if
    print(i)
    i = i + 1
end while

i = 0
while i < 5
    i = i + 1
    if i = 3 then
        continue
    end if
    print(i)
end while

i = 0
while true
    i = i + 1
    if i < 2 then
        continue
    else
        if i = 4 then
            break
        else
            print(i)
        end if
    end if
end while
