program main(args)
    load ownership from "../stdlib/ownership.bas"
    ' A non-structured (pre-2025-style) 13D/G document has no headerData/
    ' submissionType. WP-OWN-4: stake must RAISE, not silently return empty.
    print(ownership.stake("examples/fixtures/edgar/sc13_non_structured_sample.xml", "2024-06-01"))
end program
