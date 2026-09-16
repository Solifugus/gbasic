' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio_registry -- the FORMAT REGISTRY as §9 describes it: not a list of what
' has been built, but a record of what EXISTS, how its specification can
' lawfully be obtained, and therefore what could be built next.
'
' "The registry should include formats even when implementation is presently
' impossible. Such entries become a research and acquisition queue." (§9)
'
' WHY THIS IS A SEPARATE THING FROM THE ADAPTERS. Until this file, the registry
' held exactly two entries and both lived inside the adapter that implemented
' them, so it recorded only what had already been done -- which is the one
' thing a registry is not for. An entry here is a format NOBODY HAS BUILT, and
' its value is that it says whether building it is possible and what is in the
' way.
'
' NO DUPLICATION, STRUCTURALLY. An implemented format's entry lives with its
' adapter and NOT here; `all(adapters)` merges the two and REFUSES an id that
' appears in both. Two copies of one fact drift -- that is the defect `tools`
' exists to prevent one library over and the one `web.configure`'s tripwire
' guards -- so the merge is written so the duplication cannot be created rather
' than tested for afterwards.
'
' EVERY ENTRY CARRIES ITS EVIDENCE AND THE DATE IT WAS RETRIEVED. §14 asks a
' discovery process to "record exactly what evidence was used", and a URL with
' no date is a claim about a page as it is today. An entry whose acquisition
' class is INSUFFICIENT or HUMAN_REQUIRED must also say what is missing, which
' `finio.check_registry_entry` enforces -- §10's "clear stopping point", so
' that `Format discovered. Implementation blocked. Human acquisition required.`
' is a statement with content rather than a shrug.
'
' HOW COMPLETE IS THIS? NOT VERY, AND THAT IS STATED RATHER THAN IMPLIED. §14
' names twenty-two candidate domains; the entries below are a first tranche
' concentrated in payments, cash management and statements -- the domains
' `finio` already reaches -- plus one each from cards, securities, healthcare
' remittance and consumer aggregation, chosen because they sit in DIFFERENT
' acquisition classes and so make the classification mean something. Formats
' named in §14 and absent here are absent because nobody has looked yet, not
' because they were ruled out. `coverage()` says which domains are touched and
' which are not, so the gap is a value a caller can read rather than something
' to be inferred from a list's length.

library finio_registry

load finio from "finio.bas"

' A date every entry shares: the day this tranche was researched. Kept in one
' place so a later tranche cannot silently inherit it.
function _researched_on()
    return "2026-09-15"
end function

function _iso20022_catalogue()
    return { source_type: "standards_body",
             source_url_or_reference: "ISO 20022 message definitions catalogue: https://www.iso20022.org/iso-20022-message-definitions",
             date_retrieved: "2026-09-15",
             note: "the catalogue confirms message definitions and XSDs are published without charge and lists the versions of each message. The CATALOGUE was retrieved; individual message XSDs were not downloaded." }
end function

function queue()
    out = []

    ' --- cash management and statements ------------------------------------

    ' BAI2 IS NOT IN THE QUEUE ANY MORE: it has an adapter, and an implemented
    ' format's entry lives WITH its adapter. `all(adapters)` refuses an id that
    ' appears in both, which is what made this removal compulsory rather than
    ' tidy -- the merge would not run until it was done.

    append(out, { id: "swift.mt940",
        name: "SWIFT MT940 customer statement message",
        family: "cash management",
        domain: "bank account statements, the pre-ISO 20022 incumbent camt.053 replaces",
        authority: "Swift",
        description: "Tag-and-value text message: a statement header, an opening balance, one :61:/:86: pair per movement, and a closing balance.",
        representation: "tag/value text, ISO 15022",
        transport: "Swift network or file",
        known_revisions: [],
        specification_sources: [
          { source_type: "implementation_guide",
            source_url_or_reference: "Bank and vendor field references, e.g. Huntington developer portal (https://developer.huntington.com/enterprisepayments/docs/swift-mt-940) and published MT940 format overviews",
            date_retrieved: "2026-09-15",
            note: "the AUTHORITATIVE definition is Swift's Category 9 Message Reference Guide, which is not freely published. What is public is a large body of consistent implementation documentation." } ],
        acquisition_class: "DE_FACTO",
        spec_public: false,
        spec_acquisition_method: "The authoritative Category 9 Message Reference Guide comes from Swift and is not a free download. Public bank and vendor field references are abundant and consistent.",
        implementation_allowed: true,
        spec_redistribution_allowed: false,
        sample_redistribution_allowed: false,
        state: "discovered",
        recognition_status: "not_started",
        read_status: "not_started",
        write_status: "not_started",
        validation_status: "not_started",
        test_vectors: [],
        known_variants: [ ":86: narrative is structured differently by country and by bank, and that is where most real parsing effort goes" ],
        known_extensions: [],
        maintenance_priority: "periodic",
        watch_sources: [
          { kind: "document",
            reference: "https://developer.huntington.com/enterprisepayments/docs/swift-mt-940",
            watching: "a change to the :61:/:86: field definitions, which is where MT940 parsing effort lives" } ],
        last_reviewed: "2026-09-15",
        next_review_due: "2027-09-15" })

    ' --- payments ----------------------------------------------------------

    ' pain.001 IS NOT IN THE QUEUE ANY MORE: it has an adapter. The merge
    ' refuses an id in both places, and `finio_all`'s tripwire caught the other
    ' half of the move the moment the adapter landed -- which is the first time
    ' that guard has fired on a real occasion, one day after it was built
    ' because the same omission had happened three times.

    append(out, { id: "x12.820",
        name: "ASC X12 820 payment order / remittance advice",
        family: "payments",
        domain: "US EDI remittance detail accompanying a payment",
        authority: "Accredited Standards Committee X12",
        description: "EDI transaction set carrying payment and remittance detail, commonly paired with an ACH CTX entry that carries it as addenda.",
        representation: "EDI segments and elements",
        transport: "EDI interchange, or embedded in an ACH CTX addenda record",
        known_revisions: [],
        specification_sources: [
          { source_type: "standards_body",
            source_url_or_reference: "X12 licensing programme: https://x12.org/products/licensing-program",
            date_retrieved: "2026-09-15",
            note: "X12 operates a multi-tier licensing programme (Commercial, Internal, Developer). The transaction-set specifications are LICENSED, not freely published; X12 does publish example documents." } ],
        acquisition_class: "CONTROLLED",
        spec_public: false,
        spec_acquisition_method: "Licence from X12 under one of its tiers. Pricing is not published and requires a quote.",
        implementation_allowed: true,
        spec_redistribution_allowed: false,
        sample_redistribution_allowed: false,
        state: "discovered",
        recognition_status: "not_started",
        read_status: "not_started",
        write_status: "not_started",
        validation_status: "not_started",
        test_vectors: [],
        known_variants: [],
        known_extensions: [],
        blocked_by: "the transaction-set specification is licensed rather than published; a licence must be acquired by a person before an adapter can be written from the specification rather than from guesswork",
        maintenance_priority: "periodic",
        watch_sources: [
          { kind: "registry_page",
            reference: "https://x12.org/products/licensing-program",
            watching: "the licensing terms changing, which is the only thing that would unblock this format" } ],
        last_reviewed: "2026-09-15",
        next_review_due: "2027-09-15" })

    ' --- cards -------------------------------------------------------------

    append(out, { id: "iso8583",
        name: "ISO 8583 financial transaction card-originated messages",
        family: "cards",
        domain: "card authorisation, settlement, ATM and POS interchange",
        authority: "ISO",
        description: "Binary or text message with a bitmap indicating which of a numbered set of data elements are present.",
        representation: "bitmap-indexed message, binary or text encodings",
        transport: "network interchange between acquirer, switch and issuer",
        known_revisions: [ "1987", "1993", "2003" ],
        specification_sources: [],
        acquisition_class: "CONTROLLED",
        spec_public: false,
        spec_acquisition_method: "Purchase from ISO or a national standards body.",
        implementation_allowed: true,
        spec_redistribution_allowed: false,
        sample_redistribution_allowed: false,
        state: "discovered",
        recognition_status: "not_started",
        read_status: "not_started",
        write_status: "not_started",
        validation_status: "not_started",
        test_vectors: [],
        known_variants: [ "the standard defines the frame and every scheme defines its own data elements, so an ISO 8583 adapter is really one adapter per scheme" ],
        known_extensions: [],
        blocked_by: "the standard must be purchased, and beyond it each card scheme's own element definitions are issued under agreement to participants -- so even a purchased copy does not make a usable adapter",
        ' DORMANT IS A DECLARATION, NOT NEGLECT. The revisions are 1987,
        ' 1993 and 2003; a format whose last revision is over twenty years
        ' old does not warrant a monthly question, and §13 says so. An entry
        ' nobody has classified is UNREVIEWED rather than dormant, and only a
        ' named value tells the two apart.
        maintenance_priority: "dormant",
        watch_sources: [],
        last_reviewed: "2026-09-15",
        next_review_due: "2027-09-15" })

    ' --- securities --------------------------------------------------------

    append(out, { id: "fix",
        name: "FIX (Financial Information eXchange) protocol",
        family: "securities",
        domain: "order routing, execution reporting and market data between trading counterparties",
        authority: "FIX Trading Community",
        description: "Tag=value messages over a session protocol, with a large and extensible field dictionary.",
        representation: "tag/value stream, with later XML and binary encodings",
        transport: "session over TCP, or file for drop copies",
        known_revisions: [ "4.2", "4.4", "5.0 SP2" ],
        specification_sources: [
          { source_type: "standards_body",
            source_url_or_reference: "FIX Trading Community: https://fixtrading.org/standards/fix-protocol/",
            date_retrieved: "2026-09-15",
            note: "FIX is a free and open standard maintained by an independent non-profit; the specifications are downloadable without charge. The SITE was retrieved; no specification document was downloaded." } ],
        acquisition_class: "OPEN",
        spec_public: true,
        spec_acquisition_method: "Free download from fixtrading.org.",
        implementation_allowed: true,
        spec_redistribution_allowed: false,
        sample_redistribution_allowed: false,
        state: "discovered",
        recognition_status: "not_started",
        read_status: "not_started",
        write_status: "not_started",
        validation_status: "not_started",
        test_vectors: [],
        known_variants: [ "counterparties negotiate which fields are used, so a FIX adapter is a session and a dictionary rather than a fixed layout" ],
        known_extensions: [],
        maintenance_priority: "periodic",
        watch_sources: [
          { kind: "changelog",
            reference: "https://fixtrading.org/standards/fix-protocol/",
            watching: "a new FIX version or extension pack" } ],
        last_reviewed: "2026-09-15",
        next_review_due: "2027-09-15" })

    ' --- healthcare remittance ---------------------------------------------

    append(out, { id: "x12.835",
        name: "ASC X12 835 health care claim payment / advice",
        family: "healthcare remittance",
        domain: "US electronic remittance advice from payers to providers",
        authority: "Accredited Standards Committee X12",
        description: "EDI transaction set carrying the adjudication detail behind a healthcare payment.",
        representation: "EDI segments and elements",
        transport: "EDI interchange",
        known_revisions: [ "005010X221" ],
        specification_sources: [
          { source_type: "standards_body",
            source_url_or_reference: "X12 licensing programme (https://x12.org/products/licensing-program) and X12's published examples (https://x12.org/examples/005010x221)",
            date_retrieved: "2026-09-15",
            note: "X12 publishes EXAMPLE documents freely while licensing the specifications. Examples are enough to recognise a file and nowhere near enough to interpret one." } ],
        acquisition_class: "CONTROLLED",
        spec_public: false,
        spec_acquisition_method: "Licence from X12. HIPAA mandates the transaction, which does not make its specification free.",
        implementation_allowed: true,
        spec_redistribution_allowed: false,
        sample_redistribution_allowed: false,
        state: "discovered",
        recognition_status: "not_started",
        read_status: "not_started",
        write_status: "not_started",
        validation_status: "not_started",
        test_vectors: [],
        known_variants: [],
        known_extensions: [],
        blocked_by: "the implementation guide is licensed; free examples permit recognition but not interpretation",
        maintenance_priority: "periodic",
        watch_sources: [
          { kind: "registry_page",
            reference: "https://x12.org/products/licensing-program",
            watching: "the licensing terms changing, which is the only thing that would unblock this format" } ],
        last_reviewed: "2026-09-15",
        next_review_due: "2027-09-15" })

    ' --- consumer aggregation ----------------------------------------------

    ' OFX IS NOT IN THE QUEUE ANY MORE: it has an adapter, and an implemented
    ' format's entry lives WITH its adapter. The merge refuses an id in both
    ' places, which is what made this removal compulsory rather than tidy.

    return out
end function

' --- the merged view -------------------------------------------------------
'
' An implemented format's entry lives WITH ITS ADAPTER and not in the queue
' above, so there is exactly one copy of each. The merge refuses a duplicate
' rather than resolving it: two records of one format is the state where a
' registry starts lying, and it is cheaper to make it unrepresentable than to
' test for it later.

function all(adapters)
    out = []
    ids = []
    for each e in queue()
        checked = finio.check_registry_entry(e)
        if contains(ids, e.id) then
            error "finio_registry: '" + e.id + "' appears twice in the queue"
        end if
        append(ids, e.id)
        append(out, e)
    end for
    for each a in adapters
        e = a.registry_entry
        if contains(ids, e.id) then
            error ("finio_registry: '" + e.id + "' is in the queue AND has an adapter -- an implemented format's entry belongs with its adapter, and two copies drift")
        end if
        append(ids, e.id)
        append(out, e)
    end for
    return out
end function

function by_acquisition_class(entries)
    out = {}
    for each c in finio.acquisition_classes()
        out[c] = []
    end for
    for each e in entries
        append(out[e.acquisition_class], e.id)
    end for
    return out
end function

' WHAT COULD BE BUILT WITHOUT ASKING ANYONE FOR ANYTHING. This is the question
' the registry exists to answer, and it is not "what is public" -- a format can
' be publicly documented and still be one nobody may implement (Axiom 12), and
' one whose specification costs money may be perfectly lawful to implement from
' de-facto evidence.
function implementable(entries)
    out = []
    for each e in entries
        if e.implementation_allowed = true then
            if contains([ "OPEN", "PUBLIC_VENDOR", "DE_FACTO" ], e.acquisition_class) then
                append(out, e.id)
            end if
        end if
    end for
    return out
end function

' AND WHAT A PERSON WOULD HAVE TO GO AND DO. §10's stopping point, as a list.
function blocked(entries)
    out = []
    for each e in entries
        if contains([ "CONTROLLED", "HUMAN_REQUIRED", "INSUFFICIENT" ], e.acquisition_class) then
            why = ""
            if has(e, "blocked_by") then
                why = e.blocked_by
            end if
            append(out, { id: e.id, acquisition_class: e.acquisition_class,
                          blocked_by: why })
        end if
    end for
    return out
end function

' --- how much of the industry this covers ----------------------------------
'
' §14 NAMES TWENTY-TWO CANDIDATE DOMAINS and this tranche touches a handful of
' them. Reporting the gap as a VALUE rather than leaving a reader to infer it
' from a list's length is the difference between a registry that knows what it
' does not know and one that merely looks short.

function design_domains()
    return [ "ACH and clearing", "wires", "ISO 20022 message families", "SWIFT",
             "checks and image exchange", "cards", "account statements and cash management",
             "corporate treasury", "lending and loan servicing", "mortgage",
             "securities and trading", "market data", "regulatory reporting",
             "tax reporting", "credit reporting", "identity KYC and fraud",
             "insurance", "accounting interchange", "open banking",
             "legacy and archival banking", "vendor interchange", "account aggregation" ]
end function

function coverage(entries)
    fams = []
    for each e in entries
        if not contains(fams, e.family) then
            append(fams, e.family)
        end if
    end for
    return { entries: count(entries),
             families_present: fams,
             design_domains: count(design_domains()),
             note: ("§14 names " + string(count(design_domains())) + " candidate domains. This registry holds "
                    + string(count(entries)) + " formats across " + string(count(fams))
                    + " families. Formats named in §14 and absent here are absent because nobody has looked, not because they were ruled out.") }
end function
end library
