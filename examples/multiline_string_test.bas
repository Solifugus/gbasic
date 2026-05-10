description = "You are in a stone hall.
Water drips from the ceiling.
A passage leads north."
print(description)

room = {
    name = "Hall",
    description = "A square room.
The walls are damp."
}
print(room.description)

lines = ["first
second", "third"]
print(lines[0])
print(lines[1])

prefix = "Header:
"
print(prefix + "body")

function echo(text)
    return text
end function

print(echo("Function line one.
Function line two."))

escaped = "escape still works:\nquote: \"ok\"\\done"
print(escaped)
