' Deliberately not a server: it exits at once, so mcp.connect must report
' that rather than waiting for a reply that will never come.
program main(args)
    print to error "not a server"
end program
