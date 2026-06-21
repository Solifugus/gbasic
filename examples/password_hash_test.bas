password = "correct horse battery staple"
hash = password_hash(password)
second_hash = password_hash(password)

print(starts_with(hash, "$"))
print(password_verify(password, hash))
print(password_verify("wrong password", hash))
print(hash != second_hash)
