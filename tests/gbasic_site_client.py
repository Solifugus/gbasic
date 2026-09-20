#!/usr/bin/env python3
"""Drive the static gBASIC site.

There is no /shutdown request any more, and its absence is the point: the site
is a `server` block now, and a block server's soft stop is SIGTERM through the
`on drain` hook. An unauthenticated HTTP kill switch is not something to ship
in an example, and the runner asserts the signal path instead.
"""

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


def show(port, path, needle):
    status, headers, body = request(port, path)
    print(path, status, headers.get("content-type", ""), needle in body)


def main():
    port = int(sys.argv[1])

    status, headers, body = request(port, "/health")
    print("/health", status, headers.get("content-type", ""), body)

    show(port, "/", "modern BASIC for business programming")
    show(port, "/docs", "cookbooks")
    show(port, "/examples", "fails the build")
    show(port, "/about", "tree-walking interpreter")

    # The download page states a glibc floor that tools/build-release-tarball.sh
    # asserts on the artifact. Two representations of one fact, so the needle is
    # the FLOOR rather than a word of prose.
    show(port, "/download", "glibc 2.34")
    # AND IT MUST BE REACHABLE. A route nobody links to is invisible, which is a
    # different failure from a route that 404s -- and the one a page-by-page
    # check cannot see, since every page still answers 200.
    show(port, "/", 'href="/download"')
    show(port, "/static/site.css", ":root")
    show(port, "/static/site.js", "dataset.js")

    status, _, body = request(port, "/missing")
    print("/missing", status, body)

    # The static root is canonicalized and then checked for containment, so a
    # path climbing out of it is refused rather than served. Asserted here
    # because the site's own source sits one directory above that root.
    status, _, _ = request(port, "/static/..%2Fsite.bas")
    print("/static/escape", status)


if __name__ == "__main__":
    main()
