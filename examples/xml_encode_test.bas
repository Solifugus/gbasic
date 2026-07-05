' WP-XML-3 — xml.encode: record tree -> XML, escaping, qname/namespace
' preservation, pretty printing, and parse->encode->parse round-trip equality.

function roundtrips(s)
    once = xml.parse(s)
    return serialize(once) = serialize(xml.parse(xml.encode(once)))
end function

program main(args)
    load xml

    ' basic encode + attribute
    doc = xml.parse("<order id='A1'><item>Widget</item><item>Gadget</item></order>")
    print("encode " + xml.encode(doc))

    ' escaping: entities decoded into the record come back escaped, in text and attrs
    esc = xml.parse("<n note='a &quot;b&quot; &amp; c'>5 &lt; 10 &amp; 3 &gt; 1</n>")
    print("escaped " + xml.encode(esc))

    ' hand-built record (encode works on any well-formed record; number attr is
    ' stringified; special chars escaped)
    node = {}
    node["name"] = "x"
    node["attrs"] = {}
    node["attrs"]["n"] = 5
    node["attrs"]["s"] = "a<b&c"
    node["children"] = ["hello & <world>"]
    print("handbuilt " + xml.encode(node))

    ' round-trip structural equality across shapes
    print("rt_attrs_nested " + string(roundtrips("<a x='1'><b>hi</b><c/></a>")))
    print("rt_prefix_ns " + string(roundtrips("<p:root xmlns:p='urn:x'><p:v>1</p:v></p:root>")))
    print("rt_default_ns " + string(roundtrips("<root xmlns='urn:d'><v>1</v></root>")))
    print("rt_empty " + string(roundtrips("<e/>")))
    print("rt_entities " + string(roundtrips("<t>a &amp; b &lt; c</t>")))

    ' pretty printing (indented)
    print("--- pretty ---")
    print(xml.encode(doc, true))
end program
