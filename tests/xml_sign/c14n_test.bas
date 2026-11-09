' xml.canonicalize (Transward M31 §10.1): canonical bytes as XML-DSig signs
' them. Every expected answer is written by hand from the specifications'
' rules, never from libxml2 (or lxml, which is libxml2): Exclusive XML
' Canonicalization 1.0 (https://www.w3.org/TR/xml-exc-c14n/) §2.2's two
' documents, whose outputs that section gives, and Canonical XML 1.0's rules
' for what the rest exercise. Run by tests/run_xml_sign.sh, which adds a
' signature made by openssl over hand-written canonical bytes.
load xml

checks = { n: 0, bad: 0 }

function check(label, got, want)
    checks.n = checks.n + 1
    if type(got) = type(want) then
        if got = want then
            return nothing
        end if
    end if
    checks.bad = checks.bad + 1
    print "MISMATCH " + label + ": got " + type(got) + " [" + string(got) + "], want " + type(want) + " [" + string(want) + "]"
    return nothing
end function

' the refusal's message, or "accepted"
function refusal(text, opts)
    on error goto next
    v = xml.canonicalize(text, opts)
    if error then
        return error.message
    end if
    return "accepted"
end function

' --- exc-c14n §2.2: the same element under two different parents ---------
' (an ID added to elem2, to select it; an unprefixed attribute sorts before
' xml:lang, whose namespace is the xml one)
doc1 = "<n0:local xmlns:n0=\"foo:bar\" xmlns:n3=\"ftp://example.org\"><n1:elem2 xmlns:n1=\"http://example.net\" xml:lang=\"en\" ID=\"e2\"><n3:stuff xmlns:n3=\"ftp://example.org\"/></n1:elem2></n0:local>"
doc2 = "<n2:pdu xmlns:n1=\"http://example.com\" xmlns:n2=\"http://foo.example\" xml:lang=\"fr\" xml:space=\"retain\"><n1:elem2 xmlns:n1=\"http://example.net\" xml:lang=\"en\" ID=\"e2\"><n3:stuff xmlns:n3=\"ftp://example.org\"/></n1:elem2></n2:pdu>"
exc = "<n1:elem2 xmlns:n1=\"http://example.net\" ID=\"e2\" xml:lang=\"en\"><n3:stuff xmlns:n3=\"ftp://example.org\"></n3:stuff></n1:elem2>"
check("exc-c14n 2.2, first document", xml.canonicalize(doc1, { id: "e2" }), exc)
check("exc-c14n 2.2, second document: the same bytes", xml.canonicalize(doc2, { id: "e2" }), exc)
' inclusive: every namespace in scope, and the xml: attributes inherited
check("c14n 1.0 of elem2, first document", xml.canonicalize(doc1, { id: "e2", exclusive: false }), "<n1:elem2 xmlns:n0=\"foo:bar\" xmlns:n1=\"http://example.net\" xmlns:n3=\"ftp://example.org\" ID=\"e2\" xml:lang=\"en\"><n3:stuff></n3:stuff></n1:elem2>")
check("c14n 1.0 of elem2, second document", xml.canonicalize(doc2, { id: "e2", exclusive: false }), "<n1:elem2 xmlns:n1=\"http://example.net\" xmlns:n2=\"http://foo.example\" ID=\"e2\" xml:lang=\"en\" xml:space=\"retain\"><n3:stuff xmlns:n3=\"ftp://example.org\"></n3:stuff></n1:elem2>")

' --- the whole document: declaration gone, attributes sorted, escaping ----
whole = "<?xml version=\"1.0\"?>" + "\n" + "<r b=\"2\" a=\"1&amp;&lt;&quot;\" xmlns=\"urn:d\"><e/>t&gt;&#xD;</r>"
check("whole document", xml.canonicalize(whole), "<r xmlns=\"urn:d\" a=\"1&amp;&lt;&quot;\" b=\"2\"><e></e>t&gt;&#xD;</r>")
check("whole document, inclusive", xml.canonicalize(whole, { exclusive: false }), "<r xmlns=\"urn:d\" a=\"1&amp;&lt;&quot;\" b=\"2\"><e></e>t&gt;&#xD;</r>")
check("CDATA is its characters", xml.canonicalize("<r><![CDATA[a<b&c]]></r>"), "<r>a&lt;b&amp;c</r>")
check("space inside tags goes", xml.canonicalize("<r   a = '1'   ></r   >"), "<r a=\"1\"></r>")
check("tab and newline in an attribute", xml.canonicalize("<r a=\"x&#9;y&#10;z\"/>"), "<r a=\"x&#x9;y&#xA;z\"></r>")
check("a literal newline in an attribute is a space", xml.canonicalize("<r a=\"x" + "\n" + "y\"/>"), "<r a=\"x y\"></r>")
check("a literal > in an attribute stays", xml.canonicalize("<r a=\"x>y\"/>"), "<r a=\"x>y\"></r>")
check("whitespace between elements kept", xml.canonicalize("<r>" + "\n" + "  <e/>" + "\n" + "</r>"), "<r>" + "\n" + "  <e></e>" + "\n" + "</r>")
check("a redundant declaration goes", xml.canonicalize("<r xmlns:p=\"urn:p\"><p:e xmlns:p=\"urn:p\"/></r>", { exclusive: false }), "<r xmlns:p=\"urn:p\"><p:e></p:e></r>")

' --- comments --------------------------------------------------------------
check("comments left out by default", xml.canonicalize("<r><!--c-->x</r>"), "<r>x</r>")
check("comments kept when asked", xml.canonicalize("<r><!--c-->x</r>", { comments: true }), "<r><!--c-->x</r>")

' --- exclusive: what is visibly used, and InclusiveNamespaces ---------------
pre = "<r xmlns:p=\"urn:p\" xmlns:q=\"urn:q\"><e ID=\"1\" q:a=\"v\"/></r>"
check("exclusive: only the prefix an attribute uses", xml.canonicalize(pre, { id: "1" }), "<e xmlns:q=\"urn:q\" ID=\"1\" q:a=\"v\"></e>")
check("inclusive_prefixes as an array", xml.canonicalize(pre, { id: "1", inclusive_prefixes: ["p"] }), "<e xmlns:p=\"urn:p\" xmlns:q=\"urn:q\" ID=\"1\" q:a=\"v\"></e>")
check("inclusive_prefixes as PrefixList text", xml.canonicalize(pre, { id: "1", inclusive_prefixes: " p  q " }), "<e xmlns:p=\"urn:p\" xmlns:q=\"urn:q\" ID=\"1\" q:a=\"v\"></e>")
def = "<r xmlns=\"urn:d\"><p:e xmlns:p=\"urn:p\" ID=\"1\"/></r>"
check("exclusive: an unused default namespace goes", xml.canonicalize(def, { id: "1" }), "<p:e xmlns:p=\"urn:p\" ID=\"1\"></p:e>")
check("#default keeps it", xml.canonicalize(def, { id: "1", inclusive_prefixes: "#default" }), "<p:e xmlns=\"urn:d\" xmlns:p=\"urn:p\" ID=\"1\"></p:e>")
check("a used default namespace is kept", xml.canonicalize("<r xmlns=\"urn:d\"><e ID=\"1\"/></r>", { id: "1" }), "<e xmlns=\"urn:d\" ID=\"1\"></e>")

' --- the enveloped signature ----------------------------------------------
env = "<a ID=\"x\" xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\"><ds:Signature><ds:SignedInfo/></ds:Signature><b/></a>"
check("exclude_signature drops the element's own ds:Signature", xml.canonicalize(env, { id: "x", exclude_signature: true }), "<a ID=\"x\"><b></b></a>")
check("without it the signature is there", xml.canonicalize(env, { id: "x" }), "<a ID=\"x\"><ds:Signature xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\"><ds:SignedInfo></ds:SignedInfo></ds:Signature><b></b></a>")
check("a whole document's root signature", xml.canonicalize(env, { exclude_signature: true }), "<a ID=\"x\"><b></b></a>")
deep = "<a ID=\"x\" xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\"><b><ds:Signature/></b></a>"
check("a signature further down is not the enveloped one", xml.canonicalize(deep, { id: "x", exclude_signature: true }), "<a ID=\"x\"><b><ds:Signature xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\"></ds:Signature></b></a>")
other = "<a ID=\"x\"><Signature/><b/></a>"
check("a Signature in no namespace is not XML-DSig's", xml.canonicalize(other, { id: "x", exclude_signature: true }), "<a ID=\"x\"><Signature></Signature><b></b></a>")
check("id_attribute", xml.canonicalize("<r><e AssertionID=\"q\"/><e ID=\"q\" n=\"2\"/></r>", { id: "q", id_attribute: "AssertionID" }), "<e AssertionID=\"q\"></e>")

' --- refusals ------------------------------------------------------------
check("an ID held twice", refusal("<r><e ID=\"s\"/><f ID=\"s\"/></r>", { id: "s" }), "xml.canonicalize: 2 elements have ID=\"s\", and a signed ID must be one element's (two is how a signature is wrapped round other content)")
check("an ID nobody holds", refusal("<r/>", { id: "s" }), "xml.canonicalize: no element has ID=\"s\"")
check("a DTD", refusal("<!DOCTYPE r [<!ENTITY x \"y\">]><r>&x;</r>", {}), "xml.canonicalize: the document has a DTD (<!DOCTYPE>), which it refuses: SAML forbids one, and an entity the DTD declares would be read differently by xml.parse")
check("a DTD with no entity", refusal("<!DOCTYPE r><r/>", {}), "xml.canonicalize: the document has a DTD (<!DOCTYPE>), which it refuses: SAML forbids one, and an entity the DTD declares would be read differently by xml.parse")
two = "<a ID=\"x\" xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\"><ds:Signature/><ds:Signature/></a>"
check("two enveloped signatures", refusal(two, { id: "x", exclude_signature: true }), "xml.canonicalize: the element holds more than one ds:Signature, so which one is enveloped is not decided")
check("two signatures are fine when none is excluded", refusal(two, { id: "x" }), "accepted")
check("an unknown option", refusal("<r/>", { ids: "x" }), "xml.canonicalize has no option `ids`; its options are id, id_attribute, element, namespace, exclusive, comments, inclusive_prefixes and exclude_signature")
check("prefixes without exclusive", refusal("<r/>", { exclusive: false, inclusive_prefixes: "p" }), "xml.canonicalize: `inclusive_prefixes` belongs to exclusive canonicalization, and `exclusive` is false")
check("id_attribute alone", refusal("<r/>", { id_attribute: "AssertionID" }), "xml.canonicalize: `id_attribute` names where to find `id`, and no `id` was given")
check("not well-formed", left(refusal("<r>", {}), 5), "xml: ")
check("options not a record", refusal("<r/>", 3), "xml.canonicalize expects its options as a record, not number")

' --- an element by name: a signature's SignedInfo, which has no ID ---------
dsig = "http://www.w3.org/2000/09/xmldsig#"
si = "<a ID=\"x\" xmlns:ds=\"" + dsig + "\"><ds:Signature><ds:SignedInfo><ds:Reference URI=\"#x\"/></ds:SignedInfo></ds:Signature><b/></a>"
check("SignedInfo by name, inside the ID", xml.canonicalize(si, { id: "x", element: "SignedInfo", namespace: dsig }), "<ds:SignedInfo xmlns:ds=\"" + dsig + "\"><ds:Reference URI=\"#x\"></ds:Reference></ds:SignedInfo>")
check("by name alone", xml.canonicalize(si, { element: "b" }), "<b></b>")
check("a name in another namespace is not it", refusal(si, { element: "SignedInfo", namespace: "urn:other" }), "xml.canonicalize: no element is named SignedInfo in urn:other, and the one to canonicalize must be the only one")
check("a name held twice", refusal("<r><e/><e/></r>", { element: "e" }), "xml.canonicalize: more than one element is named e, and the one to canonicalize must be the only one")
check("a name outside the ID's element is not inside it", refusal("<r><a ID=\"x\"/><e/></r>", { id: "x", element: "e" }), "xml.canonicalize: no element is named e inside the element with that ID, and the one to canonicalize must be the only one")
check("namespace alone", refusal("<r/>", { namespace: dsig }), "xml.canonicalize: `namespace` is the namespace of `element`, and no `element` was given")

print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
