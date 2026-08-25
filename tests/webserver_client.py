#!/usr/bin/env python3

import http.client
import json
import sys


def request(port, method, path, body=None, headers=None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    connection.request(method, path, body=body, headers=headers or {})
    response = connection.getresponse()
    payload = response.read().decode("utf-8")
    response_headers = {name.lower(): value for name, value in response.getheaders()}
    response_headers["set-cookie-count"] = str(len(response.msg.get_all("Set-Cookie") or []))
    response_headers["set-cookie-values"] = "|".join(response.msg.get_all("Set-Cookie") or [])
    result = (
        response.status,
        response_headers,
        payload,
    )
    connection.close()
    return result


def main():
    port = int(sys.argv[1])

    status, _, body = request(
        port,
        "GET",
        "/inspect?name=Ada+Lovelace&empty=",
        headers={"X-Test": "present"},
    )
    print(status)
    print(body)

    status, headers, body = request(port, "GET", "/redirect-default")
    print(status)
    print(headers.get("location", ""))
    print(len(body))

    status, headers, body = request(port, "GET", "/redirect-permanent")
    print(status)
    print(headers.get("location", ""))
    print(len(body))

    status, _, body = request(
        port,
        "GET",
        "/cookies",
        headers={"Cookie": "session=abc123; theme=light; empty=; bad"},
    )
    print(status)
    print(body)

    status, headers, body = request(port, "GET", "/set-cookies")
    print(status)
    print(headers.get("set-cookie-count", ""))
    print(headers.get("set-cookie-values", ""))
    print(body)

    status, headers, body = request(
        port,
        "POST",
        "/json",
        body=json.dumps({"name": "Grace", "active": True}),
        headers={"Content-Type": "application/json"},
    )
    print(status)
    print(headers.get("content-type", ""))
    parsed = json.loads(body)
    print(parsed["name"])
    print(str(parsed["active"]).lower())

    status, _, body = request(
        port,
        "POST",
        "/invalid-json",
        body="{bad",
        headers={"Content-Type": "application/json"},
    )
    print(status)
    print(body)

    # The wire parser is STRICT and must stay strict. `decode` gained the
    # non-finite spellings in 0.1.0-rc7 so the gBASIC-to-gBASIC dialect
    # round-trips, and that gate is what keeps an external document from
    # injecting an infinity RFC 8259 has no syntax for. Sent raw, because
    # json.dumps would refuse to produce it.
    for hostile in ('{"a":inf}', '{"a":nan}', '{"a":-inf}', '{"a":nothing}'):
        status, _, body = request(
            port,
            "POST",
            "/invalid-json",
            body=hostile,
            headers={"Content-Type": "application/json"},
        )
        print(status)
        print(body)

    status, headers, body = request(port, "GET", "/defaults")
    print(status)
    print(headers.get("content-type", ""))
    print(len(body))

    status, _, body = request(port, "GET", "/timeout")
    print(status)
    print(body)

    status, _, body = request(port, "GET", "/shutdown")
    print(status)
    print(body)


if __name__ == "__main__":
    main()
