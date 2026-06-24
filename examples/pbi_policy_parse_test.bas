' PBI Phase 1: policy annotations parse and are inert at runtime.
' Every field is present with its colon value; policies affect only derivation
' (not yet implemented), so this behaves like a plain record today.
account = {
    owner (copy): "Ada",
    bank (link): "First Bank",
    balance (reset 0): 100,
    score (reset 3 + 4): 99,
    scratch (exclude): "temp",
    plain: 42
}

print account.owner
print account.bank
print account.balance
print account.score
print account.scratch
print account.plain
print count(keys(account))
