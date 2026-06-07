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
