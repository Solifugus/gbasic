' Every bracket kind, in every position the grammar admits one, broken across
' lines. Nothing here is new syntax -- each of these is an ordinary construct
' with the newline moved.

' 1. call arguments
print join([
    "create table loans (",
    "  id integer primary key,",
    "  balance real",
    ")"
], " ")

' 2. array literal, nested
grid = [
    [1, 2,
     3],
    [4,
     5, 6]
]
print string(grid)

' 3. record literal, with a nested array literal
person = {
    name: "ada",
    age: 36,
    tags: [
        "one",
        "two"
    ]
}
print person.name + " " + string(person.age) + " " + person.tags[1]

' 4. function declaration parameter list
function add(a,
             b,
             c)
    return a + b + c
end function

' 5. call site
print string(add(1,
                 2,
                 3))

' 6. parenthesised expression, with a blank line and trailing comments inside
total = (1 +
         2 +

         3)          ' a comment ends this line, not the statement
print string(total)

' 7. index expression
print grid[0][
    2
]

' 8. condition
if (total > 5
    and total < 10) then
    print "in range"
end if

' 9. loop header
for each item in [
        "x",
        "y"
    ]
    print item
next

' 10. deep nesting across many lines
print string(add(add(1,
                     1,
                     1),
                 add(1,
                     1,
                     1),
                 3))
