function sink()
    x = receive()
end function

w = spawn sink()
send(w, "hi", "yes")
