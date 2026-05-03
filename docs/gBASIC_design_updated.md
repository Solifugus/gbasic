# gBASIC v0.1 Core Design (Updated)

## Error Handling

gBASIC uses a BASIC-style error handling model with simple control flow and a persistent error object.

### Default Behavior

By default:

on error stop

Any runtime error:
- prints a diagnostic message
- stops execution

---

### Error Control Statements

on error goto label  
on error resume next  
on error stop  

#### on error goto

Single-use handler.

Behavior:
1. Error occurs
2. Error state is set
3. Control jumps to label
4. Handler is cleared automatically

Example:

on error goto failed

x = 1 / 0

failed:
print error.message
error.clear()

---

#### on error resume next

Execution continues after an error.

on error resume next

x = 1 / 0

print "still running"

---

#### on error stop

Restores default behavior.

---

### Raising Errors

Errors can be raised explicitly:

error "Something went wrong"

Behavior:
- sets error state
- triggers handler or resume behavior

---

### Error Object

The current error is exposed as a built-in object:

if error then
    print error.message
    print error.line
    print error.column
    print error.code
end if

#### Properties

error.message  
error.line  
error.column  
error.code  
error.source  

---

### Clearing Errors

Errors persist until cleared:

error.clear()

---

### Propagation

If a function exits while an error is active, the error propagates to the caller.

Example:

function risky()
    error "failure"
end function

on error resume next

x = risky()

if error then
    print error.message
end if

---

### Replacement Rule

If a new error occurs while an error is already active:

- the new error replaces the old error

---

### Interaction with Other Features

#### Watchers

- Errors inside watchers:
  - set error state
  - respect current error mode

#### File Locks

- All locks must be released even if an error occurs
- with lock(...) guarantees unlock

#### Functions

- Handlers are scoped to current function or program block
- Errors propagate unless handled

---

## Library System (Hybrid Model)

### Library Definition

library dates
    function endofmonth(d)
        ...
    end function
end library

Multiple libraries may exist in a single file.

---

### Using Libraries

use dates  
use logging from "libs/logging.bas"

---

### Auto-Discovery

If an undefined function is encountered:

Search order:
1. Current file
2. Explicit use libraries
3. Current folder
4. Subfolders
5. GBASIC_PATH

---

### Ambiguity

If multiple matches exist:

- First match wins
- Compiler emits warning

---

### Compiler Assistance

gbasic --add-uses myprog.bas

Behavior:
- scans unresolved symbols
- inserts required use statements
- warns on ambiguity

---

## File Lock Safety

- All locks are tracked in a runtime registry
- unlock() removes entries
- with lock guarantees unlock
- All locks released on program exit
- atexit() cleanup
- signal handlers for SIGINT, SIGTERM, SIGHUP

---

End of Updated Design
