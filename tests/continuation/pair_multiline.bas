rows = [
    {
        id: 1,
        name: "ada",
        tags: [
            "x",
            "y"
        ]
    },
    {
        id: 2,
        name: "bob",
        tags: ["z"]
    }
]
function label(prefix,
               row,
               suffix)
    return prefix + string(row.id) + ":" + row.name + "(" + join(
        row.tags,
        "|"
    ) + ")" + suffix
end function
for each r in rows
    print label(
        "<",
        r,
        ">"
    )
next
print string(len(
    rows
))
