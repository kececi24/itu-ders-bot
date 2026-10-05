import http.server
import subprocess
import sys
import ssl
import tempfile
from pathlib import Path
import threading
import time
import socket


class Fixture(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    applied_requests = {}
    applied_lock = threading.Lock()

    def log_message(self, *args):
        pass

    def do_HEAD(self):
        self.handle_request()

    def do_GET(self):
        self.handle_request()

    def do_POST(self):
        self.handle_request()

    def handle_request(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        path = self.path.split("?", 1)[0]
        assert path != "/must-not-dispatch", "cancelled transfer reached the fixture"
        if path in ("/apply-no-headers", "/apply-partial", "/apply-mixed"):
            assert self.command == "POST" and body == b"synthetic-secret-payload"
            with self.applied_lock:
                self.applied_requests[path] = self.applied_requests.get(path, 0) + 1
            if path == "/apply-no-headers":
                time.sleep(0.25)
                self.close_connection = True
                return
            if path == "/apply-partial":
                self.send_response(200)
                self.send_header("Content-Length", "1000")
                self.send_header("X-Sensitive", "synthetic-secret-header")
                self.end_headers()
                self.wfile.write(b"synthetic-secret-response")
                self.wfile.flush()
                time.sleep(0.25)
                self.close_connection = True
                return
            result = b'{"results":[{"success":true},{"success":false}]}'
            self.send_response(200)
            self.send_header("Content-Length", str(len(result)))
            self.end_headers()
            self.wfile.write(result)
            return
        if self.path == "/slow":
            time.sleep(0.15)
        if self.path == "/cancel":
            time.sleep(3)
        if self.path == "/redirect-unavailable":
            self.send_response(307)
            self.send_header("Location", f"http://127.0.0.1:{closed_socket.getsockname()[1]}/")
            result = b""
        elif self.path == "/crosshost":
            self.send_response(302)
            self.send_header("Location", f"http://localhost:{self.server.server_port}/echo")
            result = b""
        elif self.path == "/redirect":
            self.send_response(302)
            self.send_header("Set-Cookie", "cookie=yes; Path=/")
            self.send_header("Location", "/echo")
            self.send_header("X-Intermediate", "yes")
            result = b"discard this intermediate body"
        elif self.path == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            self.wfile.write(b"3\r\nabc\r\n3\r\ndef\r\n0\r\n\r\n")
            return
        else:
            self.send_response(418 if self.path == "/status" else 200)
            self.send_header("X-Final", "yes")
            self.send_header("X-Connection", str(self.client_address[1]))
            self.send_header("X-Auth", "yes" if self.headers.get("Authorization") else "no")
            result = self.command.encode() + b"|" + self.headers.get("Cookie", "").encode() + b"|" + body
        self.send_header("Content-Length", str(len(result)))
        self.end_headers()
        if self.command != "HEAD":
            try:
                self.wfile.write(result)
            except (BrokenPipeError, ConnectionResetError):
                pass


# Public, deliberately untrusted offline fixture material. Never install this CA.
fixture = Path(__file__).resolve().parent / "fixtures" / "tls"
cert, key, ca = fixture / "server.pem", fixture / "server-key.pem", fixture / "ca.pem"
for certificate in (cert, ca):
    decoded = ssl._ssl._test_decode_cert(str(certificate))
    assert ssl.cert_time_to_seconds(decoded["notBefore"]) <= time.time(), "TLS fixture is not valid yet"
    assert ssl.cert_time_to_seconds(decoded["notAfter"]) > time.time() + 86400 * 30, "TLS fixture expires within 30 days; regenerate fixture"
assert ("DNS", "localhost") in ssl._ssl._test_decode_cert(str(cert))["subjectAltName"]
tls_server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Fixture)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(cert, key)
tls_server.socket = context.wrap_socket(tls_server.socket, server_side=True)
threading.Thread(target=tls_server.serve_forever, daemon=True).start()
server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Fixture)
server.daemon_threads = True
closed_socket = socket.socket()
closed_socket.bind(("127.0.0.1", 0))  # Reserve a port without listening: deterministic connection refusal.
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    result = subprocess.run([sys.argv[1], f"http://127.0.0.1:{server.server_port}", f"https://localhost:{tls_server.server_port}", str(ca), f"http://127.0.0.1:{closed_socket.getsockname()[1]}/"], timeout=30)
    assert Fixture.applied_requests == {
        "/apply-no-headers": 1, "/apply-partial": 1, "/apply-mixed": 1
    }, "Synthetic requests were not applied exactly once (unexpected retry or missing request)"
finally:
    server.shutdown()
    server.server_close()
    tls_server.shutdown()
    tls_server.server_close()
    closed_socket.close()
sys.exit(result.returncode)
