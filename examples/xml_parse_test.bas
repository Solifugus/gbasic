' WP-XML-1 — xml.parse: node records, attributes, nesting, namespaces, entity
' decoding, CDATA, and whitespace drop/keep. All pure in-memory parsing.
program main(args)
    load xml

    ' attributes + nesting + local names + coalesced text
    doc = xml.parse("<order id='A1' qty='3'><item>Widget</item><item>Gadget</item></order>")
    print("root " + doc["name"] + " id=" + doc["attrs"]["id"] + " qty=" + doc["attrs"]["qty"])
    print("items " + string(count(doc["children"])))
    print("item0 " + doc["children"][0]["children"][0])
    print("item1 " + doc["children"][1]["children"][0])

    ' namespaces: local name (prefix stripped), qname (as written), ns URI
    ns = xml.parse("<a:root xmlns:a='urn:example:x'><a:leaf>v</a:leaf></a:root>")
    print("ns local=" + ns["name"] + " qname=" + ns["qname"] + " uri=" + ns["ns"])
    leaf = ns["children"][0]
    print("leaf local=" + leaf["name"] + " qname=" + leaf["qname"])

    ' predefined entities decoded, CDATA preserved, adjacent runs coalesced
    ent = xml.parse("<t>5 &lt; 10 &amp; 3 &gt; 1 <![CDATA[ raw <b> ]]></t>")
    print("entity [" + ent["children"][0] + "]")

    ' whitespace-only text dropped by default; retained with keep_space=true
    print("ws_default " + string(count(xml.parse("<p> <x/> <y/> </p>")["children"])))
    print("ws_keep " + string(count(xml.parse("<p> <x/> <y/> </p>", true)["children"])))

    ' no namespace -> ns is nothing; empty element -> no children
    plain = xml.parse("<plain/>")
    print("plain_ns_nothing " + string(is_nothing(plain["ns"])))
    print("plain_children " + string(count(plain["children"])))
end program
