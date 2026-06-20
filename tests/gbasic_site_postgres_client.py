#!/usr/bin/env python3

import http.client
import sys
import urllib.parse


def request(port, method, path, body=None, headers=None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    connection.request(method, path, body=body, headers=headers or {})
    response = connection.getresponse()
    body = response.read().decode("utf-8")
    headers = {name.lower(): value for name, value in response.getheaders()}
    connection.close()
    return response.status, headers, body


def get(port, path):
    return request(port, "GET", path)


def post_form(port, path, fields):
    body = urllib.parse.urlencode(fields)
    return request(
        port,
        "POST",
        path,
        body=body,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )


def main():
    port = int(sys.argv[1])

    status, headers, body = get(port, "/")
    print(status)
    print(headers.get("content-type", ""))
    print("Readable programs, practical web experiments." in body)

    status, _, body = get(port, "/docs")
    print(status)
    print("Guides and examples for learning gBASIC." in body)

    status, _, body = get(port, "/forum")
    print(status)
    print("Questions, ideas, and project discussion." in body)

    status, _, body = get(port, "/forum/general")
    print(status)
    print("Welcome to the gBASIC forum" in body)

    status, _, body = get(port, "/topic/1")
    print(status)
    print("This seeded topic proves the Postgres-backed forum tables are ready." in body)
    print("Reply support is wired into the initial schema." in body)

    status, _, body = get(port, "/forum/general/new")
    print(status)
    print("<form" in body)

    status, _, body = post_form(
        port,
        "/forum/general/new",
        {"title": "", "author_name": "Tester", "body": "Missing title"},
    )
    print(status)
    print("required" in body)

    status, _, body = post_form(
        port,
        "/forum/general/new",
        {
            "title": "Dogfood topic",
            "author_name": "Tester",
            "body": "Posted through a form",
        },
    )
    print(status)
    print("Topic created" in body)

    status, _, body = get(port, "/forum/general")
    print(status)
    print("Dogfood topic" in body)

    status, _, body = get(port, "/admin")
    print(status)
    print("Enter the local moderation token." in body)

    status, _, body = get(port, "/admin?token=test-admin-token")
    print(status)
    print("Dogfood topic" in body)
    print("Reply support is wired into the initial schema." in body)

    status, _, body = post_form(
        port,
        "/admin/hide-topic",
        {"token": "bad-token", "topic_id": "1"},
    )
    print(status)
    print(body)

    status, _, body = post_form(
        port,
        "/admin/hide-topic",
        {"token": "test-admin-token", "topic_id": "2"},
    )
    print(status)
    print("Topic hidden" in body)

    status, _, body = get(port, "/forum/general")
    print(status)
    print("Dogfood topic" in body)

    status, _, body = get(port, "/topic/1/reply")
    print(status)
    print("<form" in body)

    status, _, body = post_form(
        port,
        "/topic/1/reply",
        {"author_name": "Tester", "body": "Reply from a form"},
    )
    print(status)
    print("Reply posted" in body)

    status, _, body = get(port, "/topic/1")
    print(status)
    print("Reply from a form" in body)

    status, _, body = post_form(
        port,
        "/admin/hide-post",
        {"token": "test-admin-token", "post_id": "2"},
    )
    print(status)
    print("Reply hidden" in body)

    status, _, body = get(port, "/topic/1")
    print(status)
    print("Reply from a form" in body)

    status, _, body = get(port, "/missing")
    print(status)
    print(body)

    status, _, body = get(port, "/shutdown")
    print(status)
    print(body)


if __name__ == "__main__":
    main()
