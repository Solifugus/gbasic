# money fixtures

`v1_payload.hex` is a **real `SER_VERSION` 1 payload**, produced by the
phase-1 binary (before money carried its currency) and committed here as hex
because gBASIC's `write`/`read` truncate at the first NUL and cannot carry
binary intact.

It holds `[USD 1234.56, "tag", 42]` and exists so the v1 migration is tested
against bytes an older interpreter actually wrote, rather than against bytes
this version generated to look old. A migration nobody tested is a migration
that does not work.
