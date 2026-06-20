#!/usr/bin/env python3

import http.client
import sys


def request(port, path):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    connection.request("GET", path)
    response = connection.getresponse()
    body = response.read().decode("utf-8")
    headers = {name.lower(): value for name, value in response.getheaders()}
    connection.close()
    return response.status, headers, body


def main():
    port = int(sys.argv[1])

    status, headers, body = request(port, "/")
    print(status)
    print(headers.get("content-type", ""))
    print("Readable programs, practical web experiments." in body)

    status, _, body = request(port, "/docs")
    print(status)
    print("Guides and examples for learning gBASIC." in body)

    status, _, body = request(port, "/forum")
    print(status)
    print("Questions, ideas, and project discussion." in body)

    status, _, body = request(port, "/forum/general")
    print(status)
    print("Welcome to the gBASIC forum" in body)

    status, _, body = request(port, "/topic/1")
    print(status)
    print("This seeded topic proves the Postgres-backed forum tables are ready." in body)
    print("Reply support is wired into the initial schema." in body)

    status, _, body = request(port, "/missing")
    print(status)
    print(body)

    status, _, body = request(port, "/shutdown")
    print(status)
    print(body)


if __name__ == "__main__":
    main()
