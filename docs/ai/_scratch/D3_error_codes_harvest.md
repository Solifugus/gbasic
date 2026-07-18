# Retired numeric error codes — harvested from stale negative fixtures (D0.5, 2026-07-18)

These `.err` fixtures used a retired 3-line format carrying numeric `Error code:` values.
Captured verbatim before rebaselining to the current single-line format. Seed for D3 ERRORS.md.

| fixture | message (line 1) | Action | Error code |
|---|---|---|---|
| negative_array_conversion | array conversion failed: decoded value is not an array | invalid conversion | 1003 |
| negative_boolean_conversion | boolean conversion failed: expected "true" or "false" | invalid conversion | 1003 |
| negative_count_arity_two | count expects one argument | invalid function call | 1003 |
| negative_count_arity_zero | count expects one argument | invalid function call | 1003 |
| negative_count_boolean_false | count: argument must be a string, array, or record | invalid argument type | 1003 |
| negative_count_boolean_true | count: argument must be a string, array, or record | invalid argument type | 1003 |
| negative_count_nothing | count: argument must be a string, array, or record | invalid argument type | 1003 |
| negative_count_number | count: argument must be a string, array, or record | invalid argument type | 1003 |
| negative_count_unknown | count: argument must be a string, array, or record | invalid argument type | 1003 |
| negative_ends_with_type | ends_with: second argument must be a string | invalid argument type | 1003 |
| negative_has_key_type | has: second argument must be a string | invalid argument type | 1003 |
| negative_has_record_type | has: first argument must be a record | invalid argument type | 1003 |
| negative_keys_type | keys: argument must be a record | invalid argument type | 1003 |
| negative_number_conversion | number conversion failed: invalid numeric string | invalid conversion | 1003 |
| negative_record_conversion | record conversion failed: decoded value is not a record | invalid conversion | 1003 |
| negative_remove_key_key_type | remove_key: second argument must be a string | invalid argument type | 1003 |
| negative_remove_key_record_type | remove_key: first argument must be a record | invalid argument type | 1003 |
| negative_repeat_fractional | repeat: count must be an integer | invalid argument | 1003 |
| negative_repeat_negative | repeat: count must be non-negative | invalid argument | 1003 |
| negative_repeat_type | repeat: second argument must be a number | invalid argument type | 1003 |
| negative_replace_empty_search | replace: search string cannot be empty | invalid argument | 1003 |
| negative_replace_type | replace: first argument must be a string | invalid argument type | 1003 |
| negative_starts_with_type | starts_with: first argument must be a string | invalid argument type | 1003 |
| negative_values_type | values: argument must be a record | invalid argument type | 1003 |
