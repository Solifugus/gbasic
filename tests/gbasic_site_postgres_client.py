#!/usr/bin/env python3

import http.client
import sys
import urllib.parse

CSRF_TOKEN = "test-csrf-token"


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


def post_form(port, path, fields, csrf=True):
    if csrf:
        fields = {**fields, "csrf_token": CSRF_TOKEN}
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
    print("SQL modules" in body)

    status, _, body = get(port, "/examples")
    print(status)
    print("The site itself is becoming one of those examples." in body)

    status, _, body = get(port, "/about")
    print(status)
    print("takes inspiration from BASIC" in body)

    status, _, body = get(port, "/forum")
    print(status)
    print("Questions, ideas, and project discussion." in body)
    print("1 topics, 1 replies. Latest activity:" in body)

    status, _, body = get(port, "/forum/general")
    print(status)
    print("Welcome to the gBASIC forum" in body)
    print("1 replies. Latest activity:" in body)

    status, _, body = get(port, "/topic/1")
    print(status)
    print("This seeded topic proves the Postgres-backed forum tables are ready." in body)
    print("Reply support is wired into the initial schema." in body)

    status, _, body = get(port, "/topic/not-a-number")
    print(status)
    print(body)

    status, _, body = get(port, "/topic/not-a-number/reply")
    print(status)
    print(body)

    status, _, body = get(port, "/forum/general/new")
    print(status)
    print("<form" in body)
    print('name="csrf_token"' in body)

    status, _, body = get(port, "/forum/missing/new")
    print(status)
    print(body)

    status, _, body = post_form(
        port,
        "/forum/missing/new",
        {"title": "Missing", "author_name": "Tester", "body": "No category"},
    )
    print(status)
    print(body)

    status, _, body = post_form(
        port,
        "/forum/general/new",
        {"title": "No CSRF", "author_name": "Tester", "body": "Missing token"},
        csrf=False,
    )
    print(status)
    print(body)

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
        {"title": "x" * 121, "author_name": "Tester", "body": "Too long"},
    )
    print(status)
    print("120 characters or fewer" in body)

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
    print("0 replies. Latest activity:" in body)

    for index in range(25):
        status, _, body = post_form(
            port,
            "/forum/general/new",
            {
                "title": f"Paged topic {index}",
                "author_name": "Tester",
                "body": "Topic used to prove the category limit",
            },
        )
        if status != 201:
            print(status)
            print(body)
            return

    status, _, body = get(port, "/forum/general")
    print(status)
    print("Showing the latest 20 topics." in body)
    print("Paged topic 24" in body)
    print("Paged topic 0" in body)

    status, _, body = get(port, "/admin")
    print(status)
    print("Enter the local moderation token." in body)

    status, _, body = get(port, "/admin?token=test-admin-token")
    print(status)
    print("Dogfood topic" in body)
    print("Reply support is wired into the initial schema." in body)
    print('name="csrf_token"' in body)

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
        {"token": "test-admin-token", "topic_id": "1"},
        csrf=False,
    )
    print(status)
    print(body)

    status, _, body = post_form(
        port,
        "/admin/hide-topic",
        {"token": "test-admin-token", "topic_id": "abc"},
    )
    print(status)
    print(body)

    status, _, body = get(port, "/topic/2")
    print(status)
    print("Dogfood topic" in body)

    status, _, body = get(port, "/topic/2/reply")
    print(status)
    print("Reply to Dogfood topic" in body)

    status, _, body = post_form(
        port,
        "/topic/2/reply",
        {"author_name": "Tester", "body": "Reply to a new topic"},
    )
    print(status)
    print("Reply posted" in body)

    status, _, body = get(port, "/topic/2")
    print(status)
    print("Reply to a new topic" in body)

    status, _, body = post_form(
        port,
        "/admin/hide-topic",
        {"token": "test-admin-token", "topic_id": "2"},
    )
    print(status)
    print("Topic hidden" in body)

    status, _, body = get(port, "/topic/2")
    print(status)
    print(body)

    status, _, body = get(port, "/topic/1/reply")
    print(status)
    print("<form" in body)
    print('name="csrf_token"' in body)

    status, _, body = post_form(
        port,
        "/topic/1/reply",
        {"author_name": "Tester", "body": "Missing csrf"},
        csrf=False,
    )
    print(status)
    print(body)

    status, _, body = post_form(
        port,
        "/topic/1/reply",
        {"author_name": "Tester", "body": "Reply from a form"},
    )
    print(status)
    print("Reply posted" in body)

    status, _, body = post_form(
        port,
        "/topic/1/reply",
        {"author_name": "Tester", "body": "x" * 4001},
    )
    print(status)
    print("4000 characters or fewer" in body)

    status, _, body = get(port, "/topic/1")
    print(status)
    print("Reply from a form" in body)

    status, _, body = post_form(
        port,
        "/admin/hide-post",
        {"token": "test-admin-token", "post_id": "3"},
    )
    print(status)
    print("Reply hidden" in body)

    status, _, body = post_form(
        port,
        "/admin/hide-post",
        {"token": "test-admin-token", "post_id": "1"},
        csrf=False,
    )
    print(status)
    print(body)

    status, _, body = post_form(
        port,
        "/admin/hide-post",
        {"token": "test-admin-token", "post_id": "abc"},
    )
    print(status)
    print(body)

    status, _, body = get(port, "/topic/1")
    print(status)
    print("Reply from a form" in body)

    status, _, body = get(port, "/admin?token=test-admin-token")
    print(status)
    print("hidden by local-admin" in body)

    status, _, body = get(port, "/missing")
    print(status)
    print(body)

    status, _, body = get(port, "/shutdown")
    print(status)
    print(body)


if __name__ == "__main__":
    main()
