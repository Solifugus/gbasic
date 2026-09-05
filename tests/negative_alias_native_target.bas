' A built-in module cannot be aliased: `sqlite.` calls are intercepted by that
' name before user-function resolution ever runs, so `db.query(...)` would not
' reach the module. Refused where the alias is written rather than later at a
' call that quietly does not arrive.
load sqlite as db
print "unreachable"
