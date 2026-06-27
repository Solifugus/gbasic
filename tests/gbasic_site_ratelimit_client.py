#!/usr/bin/env python3

# Exercises the per-IP anonymous-posting rate limit. The server under test is
# started with GBASIC_SITE_POST_RATE_LIMIT=2, so the third accepted post from a
# given client IP is rejected with 429. The client IP is taken from the last
# X-Forwarded-For hop, so distinct header values get independent buckets.

import http.client
import sys
import urllib.parse

CSRF_TOKEN = "test-csrf-token"
IP_A = "203.0.113.10"
IP_B = "203.0.113.20"


def request(port, method, path, body=None, headers=None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    connection.request(method, path, body=body, headers=headers or {})
    response = connection.getresponse()
    text = response.read().decode("utf-8")
    connection.close()
    return response.status, text


def post_topic(port, ip, title):
    fields = {
        "title": title,
        "author_name": "Tester",
        "body": "Rate limit probe",
        "csrf_token": CSRF_TOKEN,
    }
    headers = {
        "Content-Type": "application/x-www-form-urlencoded",
        "X-Forwarded-For": ip,
    }
    return request(port, "POST", "/forum/general/new", urllib.parse.urlencode(fields), headers)


def post_reply(port, ip, topic_id):
    fields = {
        "author_name": "Tester",
        "body": "Rate limit probe reply",
        "csrf_token": CSRF_TOKEN,
    }
    headers = {
        "Content-Type": "application/x-www-form-urlencoded",
        "X-Forwarded-For": ip,
    }
    return request(port, "POST", f"/topic/{topic_id}/reply", urllib.parse.urlencode(fields), headers)


def main():
    port = int(sys.argv[1])

    # IP A: first two topics are accepted, the third trips the limit.
    status, _ = post_topic(port, IP_A, "A first")
    print(status)
    status, _ = post_topic(port, IP_A, "A second")
    print(status)
    status, body = post_topic(port, IP_A, "A third")
    print(status)
    print("posting too quickly" in body)

    # The bucket is shared across post kinds: a reply from IP A is also blocked.
    status, _ = post_reply(port, IP_A, 1)
    print(status)

    # IP B has its own bucket and can still post.
    status, _ = post_topic(port, IP_B, "B first")
    print(status)

    status, body = request(port, "GET", "/shutdown")
    print(status)


if __name__ == "__main__":
    main()
