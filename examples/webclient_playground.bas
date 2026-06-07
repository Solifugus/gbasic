' WebClient playground
'
' This example uses https://httpbin.org, a public HTTP testing service.
' Change base to another compatible service or local endpoint if preferred.

load webclient

base = "https://httpbin.org"

print("=== Simple GET ===")
response = webclient.get(base + "/get?language=gbasic")
print("Status: " + response.status + " " + response.reason)
print("Content-Type: " + response.headers["content-type"])
print("Body bytes: " + len(response.body))

if not is_unknown(response["json"]) then
    print("Query parameter: " + response.json.args.language)
end if

print("")
print("=== POST encoded JSON ===")
payload = {
    name:"Ada",
    language:"gBASIC",
    active:true
}

posted = webclient.post(base + "/post", encode(payload))
print("Status: " + posted.status)
print("Response body bytes: " + len(posted.body))

print("")
print("=== Custom request and headers ===")
headers = {}
headers["Accept"] = "application/json"
headers["Content-Type"] = "application/json"
headers["X-Demo"] = "webclient-playground"

custom = webclient.request({
    method:"POST",
    url:base + "/anything",
    headers:headers,
    body:encode(payload),
    timeout:15
})

print("Status: " + custom.status)
if not is_unknown(custom["json"]) then
    print("Header received by server: " + custom.json.headers["X-Demo"])
    print("Parsed JSON name: " + custom.json.json.name)
end if

print("")
print("=== HTTP error status ===")
missing = webclient.get(base + "/status/404")
print("404 request returned status: " + missing.status)
print("Execution continued after the HTTP error response.")
