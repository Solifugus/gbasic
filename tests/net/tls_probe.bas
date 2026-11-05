' Does this build have TLS? run_net.sh reads which of two refusals this gives:
' "could not load certificate" (libssl is in) or "not available in this build".
load net
l = net.listen({ port: 0, tls: { cert: "/no/such.pem", key: "/no/such.key" } })
