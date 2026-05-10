command = "look"
consider command
if "look" then
    print("look")
if "inventory" then
    print("inventory")
else
    print("unknown")
end consider

command = "inventory"
consider command
if "look" then
    print("look")
if "inventory" then
    print("inventory")
else
    print("unknown")
end consider

command = "dance"
consider command
if "look" then
    print("look")
if "inventory" then
    print("inventory")
else
    print("unknown")
end consider

command = "same"
consider command
if "same" then
    print("first")
if "same" then
    print("second")
else
    print("none")
end consider

command = "stop"
consider command
if "stop" then
    print("before break")
    break
    print("after break")
else
    print("not stop")
end consider
print("after consider")

i = 0
while i < 2
    consider i
    if 0 then
        print("zero")
        break
        print("after inner break")
    else
        print("other")
    end consider
    print("loop continues")
    i = i + 1
end while

choice = 2
consider choice
if 1 then
    print("one")
if 2 then
    print("two")
else
    print("number unknown")
end consider

flag = true
consider flag
if false then
    print("false")
if true then
    print("true")
end consider
