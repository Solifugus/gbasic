' web.trust_proxy: rewrite only when the DIRECT peer is a named proxy, and
' take the RIGHTMOST X-Forwarded-For address that is not itself trusted.
' The leftmost value is whatever the CLIENT wrote -- each hop appends the peer
' it saw -- so "first hop" (what the design draft said) is the spoof, and this
' fixture pins the correction.
program main(args)
  load web
  base = { remote_ip: "127.0.0.1", scheme: "http", headers: {} }
  base.headers["x-forwarded-for"] = "6.6.6.6, 203.0.113.9, 127.0.0.1"
  base.headers["x-forwarded-proto"] = "https"
  r = web.trust_proxy(base, ["127.0.0.1"])
  print "client: " + r.remote_ip
  print "scheme: " + r.scheme
  print "proxy_ip: " + r.proxy_ip
  print "forwarded: " + string(r.forwarded)
  print "leftmost spoof ignored: " + string(r.remote_ip != "6.6.6.6")

  direct = { remote_ip: "203.0.113.50", scheme: "http", headers: {} }
  direct.headers["x-forwarded-for"] = "1.2.3.4"
  d = web.trust_proxy(direct, ["127.0.0.1"])
  print "untrusted peer untouched: " + string(d.remote_ip = "203.0.113.50" and not has(d, "forwarded"))

  own = { remote_ip: "127.0.0.1", scheme: "http", headers: {} }
  own.headers["x-forwarded-for"] = "127.0.0.1, 127.0.0.1"
  o = web.trust_proxy(own, ["127.0.0.1"])
  print "all-trusted chain untouched: " + string(o.remote_ip = "127.0.0.1" and not has(o, "forwarded"))

  none = { remote_ip: "127.0.0.1", scheme: "http", headers: {} }
  n = web.trust_proxy(none, ["127.0.0.1"])
  print "no header untouched: " + string(n.remote_ip = "127.0.0.1" and not has(n, "forwarded"))
end program
