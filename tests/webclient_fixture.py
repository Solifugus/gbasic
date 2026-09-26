#!/usr/bin/env python3

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):
        pass

    def send_payload(self, status, body, content_type="text/plain", headers=None):
        # `bytes` passes through untouched, so a fixture can serve a body that
        # is not text -- which is what the binary-response case needs.
        payload = body if isinstance(body, bytes) else body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        for name, value in headers or []:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(payload)

    def read_body(self):
        length = int(self.headers.get("Content-Length", "0"))
        return self.rfile.read(length).decode("utf-8")

    def read_body_bytes(self):
        length = int(self.headers.get("Content-Length", "0"))
        return self.rfile.read(length)

    def do_GET(self):
        if self.path == "/get":
            self.send_payload(
                200,
                "hello",
                headers=[
                    ("X-Test", "present"),
                    ("X-Duplicate", "first"),
                    ("X-Duplicate", "second"),
                ],
            )
        elif self.path == "/json":
            self.send_payload(
                200,
                json.dumps({"name": "Ada", "active": True, "optional": None}),
                "application/json",
            )
        elif self.path == "/nul":
            # THE READ DIRECTION ALONE. A response body is bytes: an image, a
            # PDF, a zip. `webclient` used to raise `binary responses are not
            # supported` on any body containing a NUL, while `http.read` over
            # the same libcurl handed the bytes back correctly -- so this is the
            # bytes a caller must now get.
            self.send_payload(200, b"a\x00b", "application/octet-stream")
        elif self.path == "/invalid-json":
            self.send_payload(200, "{not json", "application/json")
        elif self.path == "/status/404":
            self.send_payload(404, "missing")
        elif self.path == "/redirect":
            # Set-Cookie on the INTERMEDIATE response: following discards it
            # along with the rest of that response, which is why a client that
            # cannot decline to follow cannot hold a session.
            self.send_response(302)
            self.send_header("Location", "/get")
            self.send_header("Set-Cookie", "session=abc123; Path=/")
            self.send_header("Content-Length", "0")
            self.end_headers()
        else:
            self.send_payload(500, "unexpected path")

    def do_POST(self):
        if self.path == "/length":
            # THE WRITE DIRECTION ALONE, and measured BY THE SERVER rather than
            # by reading our own bytes back: a client that truncates on the way
            # out and a reader that truncates on the way in agree with each
            # other perfectly, which is how this class of defect survives.
            raw = self.read_body_bytes()
            self.send_payload(200, "len=%d hex=%s" % (len(raw), raw.hex()))
            return
        self.send_payload(200, self.read_body(), headers=[("X-Method", "POST")])

    def do_PUT(self):
        response = {
            "method": "PUT",
            "body": self.read_body(),
            "client": self.headers.get("X-Client", ""),
        }
        self.send_payload(200, json.dumps(response), "application/json")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print("READY", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
