#!/usr/bin/env python3

import http.client
import re
import sys
import urllib.parse

CSRF_TOKEN = "test-csrf-token"


def request(port, method, path, body=None, headers=None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    connection.request(method, path, body=body, headers=headers or {})
    response = connection.getresponse()
    body = response.read().decode("utf-8")
    headers = {name.lower(): value for name, value in response.getheaders()}
    headers["set-cookie-values"] = "|".join(response.msg.get_all("Set-Cookie") or [])
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


def csrf_from(body):
    match = re.search(r'name="csrf_token" value="([^"]+)"', body)
    return match.group(1) if match else ""


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

    status, _, body = get(port, "/login")
    print(status)
    print("temporary local admin token" in body)

    invalid_login_body = urllib.parse.urlencode({"token": "bad-token"})
    status, _, body = request(
        port,
        "POST",
        "/login",
        body=invalid_login_body,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    print(status)
    print("Invalid local admin token." in body)

    login_body = urllib.parse.urlencode({"token": "test-admin-token"})
    status, headers, body = request(
        port,
        "POST",
        "/login",
        body=login_body,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    print(status)
    print(headers.get("location", ""))
    session_cookie = headers.get("set-cookie-values", "").split(";", 1)[0]
    print(session_cookie.startswith("gbasic_site_session="))
    print(len(body))

    status, _, body = request(port, "GET", "/admin", headers={"Cookie": session_cookie})
    print(status)
    print("Signed in as local-admin." in body)
    print("Dogfood topic" in body)
    session_csrf = csrf_from(body)
    print(session_csrf != "")
    print(session_csrf != CSRF_TOKEN)

    status, _, body = request(
        port,
        "POST",
        "/admin/hide-topic",
        body=urllib.parse.urlencode({"topic_id": "1"}),
        headers={
            "Content-Type": "application/x-www-form-urlencoded",
            "Cookie": session_cookie,
        },
    )
    print(status)
    print(body)

    status, _, body = request(
        port,
        "POST",
        "/admin/hide-topic",
        body=urllib.parse.urlencode({"csrf_token": session_csrf, "topic_id": "1"}),
        headers={
            "Content-Type": "application/x-www-form-urlencoded",
            "Cookie": session_cookie,
        },
    )
    print(status)
    print("Topic hidden" in body)

    status, _, body = get(port, "/topic/1")
    print(status)
    print(body)

    status, _, body = request(
        port,
        "POST",
        "/admin/unhide-topic",
        body=urllib.parse.urlencode({"csrf_token": session_csrf, "topic_id": "1"}),
        headers={
            "Content-Type": "application/x-www-form-urlencoded",
            "Cookie": session_cookie,
        },
    )
    print(status)
    print("Topic restored" in body)

    status, headers, body = request(port, "POST", "/logout", headers={"Cookie": session_cookie})
    print(status)
    print(headers.get("location", ""))
    print("Max-Age=0" in headers.get("set-cookie-values", ""))

    status, _, body = request(port, "GET", "/admin", headers={"Cookie": session_cookie})
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

    status, _, body = get(port, "/admin?token=test-admin-token")
    print(status)
    print("Unhide topic" in body)

    status, _, body = post_form(
        port,
        "/admin/unhide-topic",
        {"token": "test-admin-token", "topic_id": "2"},
    )
    print(status)
    print("Topic restored" in body)

    status, _, body = get(port, "/topic/2")
    print(status)
    print("Dogfood topic" in body)

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

    status, _, body = get(port, "/admin?token=test-admin-token")
    print(status)
    print("Unhide reply" in body)

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

    status, _, body = post_form(
        port,
        "/admin/unhide-post",
        {"token": "test-admin-token", "post_id": "3"},
    )
    print(status)
    print("Reply restored" in body)

    status, _, body = get(port, "/topic/1")
    print(status)
    print("Reply from a form" in body)

    status, _, body = get(port, "/admin?token=test-admin-token")
    print(status)
    print("visible" in body)

    status, _, body = get(port, "/missing")
    print(status)
    print(body)

    status, _, body = get(port, "/shutdown")
    print(status)
    print(body)


if __name__ == "__main__":
    main()
