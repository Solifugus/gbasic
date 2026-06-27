function sink()
    x = receive()
end function

w = spawn sink()
rec = { box (link): "shared" }
send(w, rec, true)
