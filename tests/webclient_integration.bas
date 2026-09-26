load webclient

base = "http://127.0.0.1:18765"

response = webclient.get(base + "/get")
print(response.status)
print(response.reason)
print(response.body)
print(response.headers["content-type"])
print(response.headers["x-test"])
print(response.headers["x-duplicate"])

posted = webclient.post(base + "/post", "plain body")
print(posted.status)
print(posted.body)
print(posted.headers["x-method"])

request_headers = {}
request_headers["X-Client"] = "gbasic"
requested = webclient.request({method:"PUT", url:base + "/request", headers:request_headers, body:"request body", timeout:5})
print(requested.status)
print(requested.json.method)
print(requested.json.body)
print(requested.json.client)

json_response = webclient.get(base + "/json")
print(json_response.json.name)
print(json_response.json.active)
print(is_nothing(json_response.json.optional))

invalid_json = webclient.get(base + "/invalid-json")
print(is_unknown(invalid_json["json"]))

missing = webclient.get(base + "/status/404")
print(missing.status)
print(missing.body)

redirected = webclient.get(base + "/redirect")
print(redirected.status)
print(redirected.body)

' --- redirects: following is the DEFAULT, declining is now possible ---------
'
' A redirect is not always plumbing. `follow: false` returns the 3xx itself --
' its status, its `location`, and its `set-cookie`, which following discards
' along with the rest of the intermediate response. Without this a program
' could not be a client for OAuth, POST-redirect-GET, or any API that answers
' 302 and expects the caller to look; nor could it hold a session, since the
' cookie establishing one usually arrives on the response being redirected
' away from. libcurl's own default is not to follow; gBASIC's is, and stays
' so, because changing it silently would break every existing caller.
kept = webclient.request({ method: "GET", url: base + "/redirect", follow: false })
print(kept.status)
print(kept.headers["location"])
print(kept.headers["set-cookie"])

' The control: the SAME url with the default still follows to the 200.
followed = webclient.request({ method: "GET", url: base + "/redirect" })
print(followed.status)

' --- a body is BYTES, in both directions (PLAT-NUL, 2026-09-25) -------------
'
' `webclient` used to RAISE `binary responses are not supported` on a response
' containing a NUL, so it could not fetch an image, a PDF or a zip -- in a
' language whose strings have carried arbitrary bytes since they were designed.
' The refusal was not a fact about HTTP: `http.read`, over the same libcurl and
' the same buffer, already handed those bytes back correctly, so two readers of
' one transfer disagreed about what a body is.
'
' THE TWO DIRECTIONS ARE ASSERTED SEPARATELY, because a client that truncates
' on the way out and a reader that truncates on the way in agree with each
' other perfectly -- which is precisely how this survived. So the read is
' checked against a body the FIXTURE chose, and the write is checked against
' the length THE SERVER reports.
binary = webclient.get(base + "/nul")
print(len(binary.body))
print(hex_encode(binary.body))

' No `json` field, because the JSON decoder walks a C string and then checks it
' reached the end -- a test any interior NUL satisfies -- so without a guard a
' valid prefix followed by arbitrary bytes would be reported as a whole valid
' document, which is a wrong answer where offering nothing is a true one.
print(has(binary, "json"))

sent = webclient.post(base + "/length", "a" + chr(0) + "b")
print(sent.body)
