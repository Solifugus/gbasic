#!/usr/bin/env python3

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):
        pass

    def send_payload(self, status, body, content_type="text/plain", headers=None):
        payload = body.encode("utf-8")
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
