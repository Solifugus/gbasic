# gBASIC Tutorial

gBASIC is an experimental BASIC-family language for readable business-style programs. This tutorial teaches the current v0.1 implementation through small examples.

## Hello World

```basic
print("hello, world")
```

Run it:

```sh
./gbasic hello.bas
```

Programs may also be wrapped explicitly:

```basic
program demo(args)
    print("hello from program")
end program
```

If a file has no `program` block, the whole file is treated as an implicit program.

## Variables

Assign values with `=`.

```basic
name = "Ada"
age = 37
ready = true

print(name)
print(age + 1)
print(ready)
```

Identifiers are case-sensitive in storage. Keyword-like names such as `end` and `next` are accepted in the limited places where the parser explicitly allows them as identifiers.

Strings use double quotes and may contain literal newlines:

```basic
description = "You are in a stone hall.
Water drips from the ceiling."
print(description)
```

Escapes such as `\n`, `\t`, `\"`, and `\\` still work. Unknown escapes and strings that reach end-of-file before a closing quote are errors.

## Modifiers

Modifiers attach domain meaning to assignment or comparison.

Assignment modifiers:

```basic
price(USD)= 19.95
due(date)= "2026-05-15"

print(price)
print(due)
```

Comparison modifiers:

```basic
name = "Joe Barnes"

if name(caseless)= "joe barnes" then
    print("match")
end if
```

You can define modifiers:

```basic
modifier rounded(n) for compare
    return compare(round(left, n), operator, round(right, n))
end modifier

a = 1.234
b = 1.235

if a(rounded 2)= b then
    print("rounded match")
end if
```

Modifier bodies receive built-in variables:

- Assign modifiers receive `value`.
- Compare modifiers receive `left`, `right`, and `operator`.

Useful built-in assignment modifiers include:

```basic
command(trimmed)= input(">")
command(lowered)= command
code(uppered)= "abc"
words(split)= "apple banana orange"
line(join ", ")= words
n(length)= line
age(number)= "42"
text(string)= age
```

`input(...)` returns strings. Use `(number)` when you want numeric input. Arithmetic stays strict for `-`, `*`, and `/`, and `+` only does numeric addition when neither operand is a string. If either operand is a string, `+` converts both values with canonical string conversion and concatenates them.

## Arrays And Records

Arrays use brackets:

```basic
scores = [88, 91, 91, 74, 100]

print(scores[0])
print(len(scores))
print(mean(scores))
print(mode(scores))
```

Array helpers modify or return arrays:

```basic
items = ["banana"]
append(items, "orange")
prepend(items, "apple")
print(join(items, ", "))

if contains(items, "banana") then
    print("has banana")
end if

words = ["take", "brass", "key"]
print(join_from(words, 1, " "))
```

Use `remove_value(items, value)` to remove the first matching value. It returns the resulting array and also updates the array when the first argument is a variable. `first(items)` returns the first element or `nothing`; `rest(items)` returns a new array containing all but the first element. `find_by(records, field_name, value)` searches an array of records and returns the first matching index or `nothing`.

Use `encode(value)` and `decode(text)` to save and load plain gBASIC data:

```basic
project = {title = "The Lantern Room", items = items}
text = encode(project)
copy = decode(text)
print(copy.title)
```

The format is JSON-like and supports numbers, strings, booleans, `nothing`, `unknown`, arrays, and records.

Use `string(value)` or the `(string)` modifier when you want canonical display text:

```basic
line = "You were born in " + birth_year + "."
print(string(project))
```

Arrays and records use the same textual shape as `encode(value)`, but `string(value)` is display-oriented while `encode(value)` is serialization.

Use `quote(value)` when generating gBASIC source code that contains string literals:

```basic
description = "A room called \"Hall\".
Water drips from C:\\caves."

line = "description = " + quote(description)
print(line)
```

`quote` returns the surrounding double quotes and escapes quotes, backslashes, tabs, carriage returns, and newlines. Newlines are written as `\n`, which keeps generated source compact and still round-trips with `decode(quote(description))`. Non-string scalar values use the same scalar conversion as `string(value)`; arrays and records are still errors for `quote`.

Records use braces and `name = value` fields:

```basic
customer = {
    name = "Ada",
    age = 37,
    balance = 120.00
}

print(customer.name)

customer.name = "Grace"
print(customer.name)

field = "age"
print(customer[field])
print(customer["nickname"])
customer["nickname"] = "Amazing Grace"
print(customer.nickname)

items = [{name = "lamp", location = "cellar"}]
items[0].location = "inventory"
print(items[0].location)
```

`customer[field]` is dynamic record access: the key expression must produce a string. A missing dynamic field reads as `unknown`, while assignment creates the field.

## Calling Web APIs

When gBASIC is built with libcurl, load the WebClient module to make outgoing
HTTP and HTTPS requests:

```basic
load webclient

response = webclient.get("https://example.com")
print(response.status)
print(response.reason)
print(response.body)
```

The response record always has numeric `status`, string `reason`, record
`headers`, and string `body` fields. HTTP statuses such as 404 and 500 are
responses, not runtime errors.

`webclient.post` accepts only a string body. Encode records and arrays
explicitly when an API expects JSON:

```basic
payload = {name:"Ada", active:true}
response = webclient.post(
    "https://api.example.com/users",
    encode(payload)
)
```

For custom methods, headers, bodies, or timeouts, use
`webclient.request(record)`:

```basic
headers = {}
headers["Content-Type"] = "application/json"
headers["X-Client"] = "gbasic"

response = webclient.request({
    method:"POST",
    url:"https://api.example.com/users",
    headers:headers,
    body:encode({name:"Ada"}),
    timeout:15
})
```

When a response body is valid JSON, WebClient adds a decoded `json` field.
Otherwise the field is absent and the original body remains available:

```basic
response = webclient.get("https://api.example.com/users/1")

if not is_unknown(response["json"]) then
    print(response.json.name)
else
    print(response.body)
end if
```

Network, DNS, connection, TLS, malformed URL, and timeout failures are runtime
errors. libcurl is optional at build time, so `load webclient` reports a clear
error when support was not compiled in. WebClient only sends outgoing
requests; incoming requests use the separate WebServer module.

## Serving HTTP Requests

WebServer follows gBASIC's watcher model rather than registering callback
functions. Start a loopback server, watch its request queue, and append replies
to its response queue:

```basic
load webserver

server = webserver.listen(8080)

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)

        append(server.responses, {
            id:req.id,
            body:"Hello World"
        })
    end while
end watch
```

Run the program and request `http://127.0.0.1:8080/`. The watcher removes the
request from `server.requests`, then queues a response with the same request
ID. The request and response queues are ordinary watched arrays:
`take_first(server.requests)` and `append(server.responses, ...)` notify
watchers once after the stored queue mutation completes.

The live server record provides:

```basic
print(server.port)
print(server.running)
print(count(server.requests))
print(count(server.responses))
```

Port `0` asks the operating system to choose an available port:

```basic
server = webserver.listen(0)
print(server.port)
```

Request records provide method, path, query parameters, headers, body, and
client details:

```basic
print(req.method)
print(req.path)
print(req.query.search)
print(req.headers["content-type"])
print(req.remote_ip)
print(req.remote_port)
print(req.timestamp)
```

`req.query` is a record of percent-decoded string values. Header names are
lowercase. The request body is always a string.

When the body contains valid JSON, the request also has a decoded `json`
field:

```basic
if not is_unknown(req["json"]) then
    print(req.json.name)
else
    print(req.body)
end if
```

Build JSON responses explicitly with `encode()` and a content-type header:

```basic
headers = {}
headers["content-type"] = "application/json"

append(server.responses, {
    id:req.id,
    status:201,
    headers:headers,
    body:encode({accepted:true})
})
```

Only `id` is required. Status defaults to `200`, headers to `{}`, and body to
`""`. If no response is queued within 30 seconds, WebServer sends HTTP 504 and
continues running.

Stop the server explicitly:

```basic
webserver.close(server)
```

Setting `server.running = false` also requests shutdown. Phase 1 binds only to
`127.0.0.1`, handles one HTTP/1.1 request per connection, and requires
`Content-Length` for request bodies. Routing, TLS, WebSockets, streaming,
static files, middleware, sessions, and cookies are not included.

## Functions

Functions use parameters and `return`.

```basic
function add(a, b)
    return a + b
end function

function greet(name)
    return "Hello, " + name
end function

print(add(2, 3))
print(greet("Ada"))
```

Function variables are local. Global variables remain visible unless shadowed by locals.

Labels, `goto`, and `gosub` are currently supported inside functions:

```basic
function normalize(x)
    if x < 0 then goto negative
    return x

negative:
    return 0
end function

print(normalize(-3))
```

Function calls are expressions, so they can be compared:

```basic
words = []
if len(words) = 0 then
    print("empty")
end if
```

Function calls are not assignment targets. `len(words) = 0` is invalid.

## Control Flow

Use `if`/`else` for branches:

```basic
if ready then
    print("ready")
else
    print("waiting")
end if
```

For a short branch, place one statement on the same line as `then`:

```basic
if ready then print("ready")
```

An inline `else` also ends the conditional without `end if`:

```basic
if ready then print("ready")
else print("waiting")
```

The `then` branch may remain multiline when the final `else` branch is inline:

```basic
if ready then
    print("ready")
    start_work()
else print("waiting")
```

When the final branch begins on the next line, keep `end if`:

```basic
if ready then print("ready")
else
    print("waiting")
    check_again()
end if
```

Inline branches contain one non-block statement, such as an assignment,
`print`, function call, `return`, `goto`, `break`, or `continue`. Use the
multiline form for nested conditionals, loops, functions, watchers, and other
block statements. As with conventional conditional syntax, `else` belongs to
the nearest unmatched inline `if`.

Use `while` for loops:

```basic
i = 0
while i < 3
    print(i)
    i = i + 1
end while
```

`break` exits the nearest `while`; `continue` skips to the next iteration:

```basic
i = 0
while i < 5
    i = i + 1
    if i = 3 then
        continue
    end if
    print(i)
end while
```

Use `consider` for command or menu dispatch:

```basic
consider command
if "look" then
    look()
if "inventory" then
    show_inventory()
else
    print("I do not understand.")
end consider
```

The subject after `consider` is evaluated once. Each branch at the same indentation as `consider` compares the subject to the branch expression with `=`, and only the first match runs. `else` runs only if nothing matched. `break` exits the consider block; inside a loop, `continue` still applies to the loop.

## Nothing And Unknown

`nothing` is deliberate absence. `unknown` is a distinct value for information that exists but is not known yet.

```basic
x = nothing
y = unknown

if x != y then
    print("distinct")
end if
```

## Errors

Default behavior is `on error stop`. Runtime errors stop execution and return a nonzero process exit status.

Use `on error resume next` to inspect and clear error state:

```basic
on error resume next

print(missing_value)
if error then
    print(error.message)
    print(error.line)
    print(error.column)
    print(error.code)
    print(error.source)
    error.clear()
end if

on error stop
```

Use `on error goto label` for a single-use handler:

```basic
on error goto failed
error "explicit failure"
print("not reached")

failed:
print(error.message)
error.clear()
```

## Watchers

Watchers are immediate reactive blocks over stored paths. A watcher runs once
when it is registered, then runs synchronously after later storage-changing
mutations to matching paths.

```basic
a = 10
b = 20

watch(a, b)
    c = a + b
end watch

print(c)

a = 15
print(c)
```

Equal-value writes do not trigger watchers:

```basic
count = 0

watch(count)
    print(count)
end watch

count = 0   # no second run
count = 1   # runs
```

Matching is symmetric at dot boundaries. `watch(state)` sees changes to
`state.value`, and `watch(state.value)` sees replacement of `state`. Array
indexes are tracked at the containing array path.

During one active watcher cascade, a watcher that is already pending is not
queued again. It runs once and sees the latest state. Runaway watcher cascades
raise runtime error code `1005` from source `watcher`.

Collection mutators such as `append`, `prepend`, `insert`, `remove`,
`take_first`, `take_last`, `reverse`, `sort`, and `unique` notify watchers
once after the stored collection mutation completes.

Suppress watcher triggering with `without watchers`:

```basic
without watchers
    a = 100
end without
```

## Core Builtin Functions

gBASIC provides always-available core functions that don't require loading libraries. These functions maintain strict type checking and provide helpful error messages.

### Working with Types

Check what type a value is:

```basic
print(type(42))           # "number"
print(type("hello"))      # "string"  
print(type([1, 2, 3]))    # "array"
print(type({x:1}))        # "record"
print(type(true))         # "boolean"
print(type(nothing))      # "nothing"

if is_string(name) then
    print("name is a string")
end if

if is_array(scores) then
    print("scores has " + count(scores) + " elements")
end if
```

### Converting Values

Convert between types explicitly:

```basic
' Convert strings to numbers
age(number)= input("Age: ")   # Using modifier
age = number(input("Age: "))  # Using function

' Convert anything to string  
message = "You scored " + string(score) + " points"

' Decode JSON strings
data = array("[1, 2, 3]")     # Creates [1, 2, 3]
config = record("{\"debug\": true}")  # Creates {debug: true}

' Convert string flags to booleans
debug = boolean("true")       # true
verbose = boolean("false")    # false
```

The difference between conversion functions:
- `string(value)` - convert any value to its string representation
- `encode(value)` - serialize structured data to JSON
- `decode(text)` - parse JSON back into values
- `quote(value)` - create gBASIC source code with proper escaping

### String Processing

Work with text efficiently:

```basic
text = "hello world"

' Replace text (all occurrences)
fixed = replace(text, "world", "gBASIC")   # "hello gBASIC"

' Check prefixes and suffixes
if starts_with(filename, "temp_") then
    print("temporary file")
end if

if ends_with(filename, ".txt") then
    print("text file")
end if

' Repeat text
border = repeat("=", 40)   # "========================================"
print(border)
```

### Working with Records

Extract information from records:

```basic
person = {name: "Alice", age: 30, city: "Portland"}

' Get all keys and values
field_names = keys(person)      # ["name", "age", "city"]
field_values = values(person)   # ["Alice", 30, "Portland"]

' Check if a field exists
if has(person, "email") then
    print("Email: " + person.email)
else
    print("No email on file")
end if

' Create modified copies (immutable)
public_info = remove_key(person, "age")
# person is unchanged, public_info is {name: "Alice", city: "Portland"}
```

### Counting Things

Count elements in collections:

```basic
' String length
message = "Hello"
chars = count(message)     # 5

' Array elements
items = [1, 2, 3, 4, 5]
total = count(items)       # 5

' Record fields  
person = {name: "Bob", age: 25}
fields = count(person)     # 2

' Empty collections
print(count(""))           # 0
print(count([]))           # 0  
print(count({}))           # 0
```

### Error Handling

Core functions provide clear errors for invalid arguments:

```basic
' Type errors
count(42)                  # Error: count requires string, array, or record
number("hello")            # Error: invalid numeric string
boolean("maybe")           # Error: expected "true" or "false"

' Missing arguments
type()                     # Error: type expects one argument
has(person)                # Error: has expects two arguments
```

## Libraries And Load

Libraries collect functions and exported modifiers.

```basic
library math
    function add(a, b)
        return a + b
    end function
end library

program demo(args)
    load math
    print(add(2, 3))
end program
```

Only exported modifiers are imported:

```basic
library text
    export modifier shout for assign
        return upper(value)
    end modifier
end library

program demo(args)
    load text

    msg(shout)= "hello"
    print(msg)
end program
```

Load a library from another file:

```basic
program demo(args)
    load tools from "libs/tools.bas"
    print(double(21))
end program
```

Compatibility note: `use` still works for now, but new code should prefer `load`.

Qualified calls and modifiers bypass normal override lookup:

```basic
print(math.add(2, 3))

if name(text.caseless)= "joe" then
    print("match")
end if
```

## File Operations

Create a file reference with `(file)`.

```basic
f(file)= "examples/tmp_file_test.txt"

write(f, "line one\nline two\n")
print(exists(f))
print(bytes(f))
print(lines(f))
print(read(f))

append(f, "line three\n")
print(lines(f))
```

Use advisory locking:

```basic
with lock(f)
    write(f, "locked write\n")
    append(f, "still locked\n")
end with
```

`with lock` unlocks after the block finishes.

## Date/Time Lenses

Core date/time support is based on primitive lenses. Convenience calendar behavior belongs in libraries.

```basic
d(date)= "2026-05-15 14:30:20"

y(year)= d
print(y)

if d(month)= "2026-05" then
    print("same month")
end if
```

Primitive date/time modifiers:

- `date`
- `time`
- `datetime`
- `year`
- `month`
- `day`
- `hour`
- `minute`
- `second`

Durations are first-class values:

```basic
start(date)= "2026-05-15 13:10:00"
d1 = 1 hour 20 minutes

print(start + d1)
print(start + 2 days)
```

The standard library can build convenience modifiers from primitives:

```basic
program demo(args)
    load dates from "../stdlib/dates.bas"

    today(date)= "2026-05-15"
    end(end of month)= today
    print(end)
end program
```

## Money

`USD` creates a money value stored internally as cents.

```basic
price(USD)= 19.95
tax = price * 0.08
total = price + tax

print(price)
print(tax)
print(total)

if total > price then
    print("greater")
end if
```

Money supports money-to-money addition, subtraction, and comparisons. Multiplication and division are supported with numbers.

## Larger Example

See `examples/adventure/adventure.bas` for a small Infocom-style text adventure using `input(">")`, `print(...)`, `(trimmed)`, `(lowered)`, `(split)`, `find`, arrays, functions, `if`/`else`, `while`, and `break`.

The companion `examples/adventure/NOTES.md` records design-friction notes discovered while writing the example.
