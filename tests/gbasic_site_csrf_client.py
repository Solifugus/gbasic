#!/usr/bin/env python3

# Exercises the production CSRF posture for anonymous posting: the cookie-bound
# double-submit token. The server under test runs with GBASIC_SITE_CSRF_TOKEN
# unset, so anonymous forms issue a per-visitor token in an HttpOnly cookie and
# require the submitted form token to match that cookie on POST.

import http.client
import re
import sys
import urllib.parse

COOKIE_NAME = "gbasic_site_anon_csrf"


def request(port, method, path, body=None, headers=None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    connection.request(method, path, body=body, headers=headers or {})
    response = connection.getresponse()
    text = response.read().decode("utf-8")
    set_cookies = response.msg.get_all("Set-Cookie") or []
    connection.close()
    return response.status, set_cookies, text


def cookie_token(set_cookies):
    for header in set_cookies:
        if header.startswith(COOKIE_NAME + "="):
            return header.split(";", 1)[0].split("=", 1)[1]
    return ""


def form_token(body):
    match = re.search(r'name="csrf_token" value="([^"]+)"', body)
    return match.group(1) if match else ""


def get_form(port, cookie=None):
    headers = {"Cookie": f"{COOKIE_NAME}={cookie}"} if cookie else {}
    status, set_cookies, body = request(port, "GET", "/forum/general/new", headers=headers)
    return status, cookie_token(set_cookies), form_token(body)


def post_topic(port, cookie=None, token=None, title="Cookie CSRF topic"):
    fields = {"title": title, "author_name": "Tester", "body": "Body"}
    if token is not None:
        fields["csrf_token"] = token
    headers = {"Content-Type": "application/x-www-form-urlencoded"}
    if cookie is not None:
        headers["Cookie"] = f"{COOKIE_NAME}={cookie}"
    status, _, _ = request(port, "POST", "/forum/general/new", urllib.parse.urlencode(fields), headers)
    return status


def main():
    port = int(sys.argv[1])

    # First visit issues a token in both the cookie and the form's hidden field.
    status, issued_cookie, issued_form = get_form(port)
    print(status)
    print(issued_cookie != "")
    print(issued_form == issued_cookie)

    # Posting with the issued cookie and its matching form token is accepted.
    print(post_topic(port, cookie=issued_cookie, token=issued_form))

    # No cookie and no token -> rejected.
    print(post_topic(port, cookie=None, token=None))

    # Cookie present but the form token does not match -> rejected.
    print(post_topic(port, cookie=issued_cookie, token="not-the-right-token-value"))

    # Cookie present but the form token is missing -> rejected.
    print(post_topic(port, cookie=issued_cookie, token=None))

    # A malformed (too short) cookie token is rejected even if the form matches.
    print(post_topic(port, cookie="x", token="x"))

    # A returning visitor presents the cookie; the form reuses the same token
    # rather than rotating it, so multiple tabs/forms stay consistent.
    status, returning_cookie, returning_form = get_form(port, cookie=issued_cookie)
    print(status)
    print(returning_form == issued_cookie)

    status, _, _ = request(port, "GET", "/shutdown")
    print(status)


if __name__ == "__main__":
    main()
