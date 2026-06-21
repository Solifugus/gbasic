token = secure_token(32)
allowed = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
valid = len(token) = 32
i = 0
while i < len(token)
    if find(allowed, mid(token, i, 1)) = nothing then
        valid = false
    end if
    i = i + 1
end while

print(valid)
print(len(secure_token(1)))
print(len(secure_token(64)))
