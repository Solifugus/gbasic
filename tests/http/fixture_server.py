#!/usr/bin/env python3
"""Loopback HTTP fixture for tests/run_http.sh.

Nothing here reaches the network: it binds 127.0.0.1 only. Each route exists
for one tier and says which, because a fixture whose endpoints nobody can
account for grows endpoints nobody removes.

  /              200, a fixed body                 (parity, control)
  /boom          500 with a body and a header      (transport-vs-HTTP)
  /wait?ms=N     200 after N milliseconds          (concurrency)
  /slow?n=N&ms=M N chunks M ms apart, chunked      (incremental read)
  /echo          POST: the request body back       (method and body)
  /reflect       200, the request headers as text  (request headers)
"""
import http.server
import socketserver
import sys
import time
import urllib.parse


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def handle_one_request(self):
        # A client that stops reading is ORDINARY here -- http.stop and an
        # abandoned handle both do exactly that, and two of the tiers exist to
        # exercise it. Without this the fixture prints a traceback per stopped
        # transfer and the runner's clean-stderr check fails on a test doing
        # what it was written to do.
        try:
            super().handle_one_request()
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True

    def _query(self):
        parts = urllib.parse.urlsplit(self.path)
        return parts.path, urllib.parse.parse_qs(parts.query)

    def _plain(self, body, status=200, extra=None):
        self.send_response(status)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        for name, value in (extra or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path, query = self._query()
        if path == "/boom":
            self._plain(b"server said no", 500, {"X-Note": "five hundred"})
            return
        if path == "/wait":
            ms = int(query.get("ms", ["200"])[0])
            time.sleep(ms / 1000.0)
            self._plain(b"waited " + str(ms).encode())
            return
        if path == "/slow":
            count = int(query.get("n", ["3"])[0])
            ms = int(query.get("ms", ["150"])[0])
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(count):
                chunk = ("tick%d\n" % i).encode()
                self.wfile.write(b"%x\r\n" % len(chunk) + chunk + b"\r\n")
                self.wfile.flush()
                time.sleep(ms / 1000.0)
            self.wfile.write(b"0\r\n\r\n")
            return
        if path == "/reflect":
            lines = []
            for name in ("X-Probe", "User-Agent"):
                lines.append("%s=%s" % (name, self.headers.get(name, "")))
            self._plain("\n".join(lines).encode())
            return
        self._plain(b"hello from the fixture", 200, {"X-Fixture": "one"})

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        data = self.rfile.read(length)
        self._plain(data)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    # Port 0 lets the OS choose, and the chosen port is announced rather than
    # written down anywhere: no fixture and no golden carries a number this
    # machine happened to be able to bind.
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    with Server(("127.0.0.1", port), Handler) as httpd:
        print("READY %d" % httpd.server_address[1], flush=True)
        httpd.serve_forever()


if __name__ == "__main__":
    main()
