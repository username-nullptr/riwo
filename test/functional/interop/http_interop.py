#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

import argparse
import http.server
import subprocess
import threading
import urllib.parse
import urllib.request


TIMEOUT = 8


def start_riwo_server(peer):
    process = subprocess.Popen(
        [peer, "server"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    line = process.stdout.readline().strip()
    if not line.startswith("PORT "):
        stdout, stderr = process.communicate(timeout=TIMEOUT)
        raise RuntimeError(
            f"Riwo server did not publish a port: {line!r}\n{stdout}{stderr}"
        )
    return process, int(line[5:])


def finish_process(process, description):
    try:
        stdout, stderr = process.communicate(timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        process.terminate()
        stdout, stderr = process.communicate(timeout=TIMEOUT)
        raise RuntimeError(f"{description} did not stop\n{stdout}{stderr}")
    if process.returncode:
        raise RuntimeError(
            f"{description} exited with {process.returncode}\n{stdout}{stderr}"
        )


def external_request_with_curl(curl, url):
    completed = subprocess.run(
        [
            curl,
            "--silent",
            "--show-error",
            "--http1.1",
            "--max-time",
            "5",
            "--noproxy",
            "*",
            "--include",
            "--request",
            "POST",
            "--header",
            "X-Interop-Client: external",
            "--data-binary",
            "external-client",
            url,
        ],
        check=True,
        capture_output=True,
        text=True,
        timeout=TIMEOUT,
    )
    response = completed.stdout.replace("\r\n", "\n")
    if "HTTP/1.1 202 Accepted\n" not in response:
        raise RuntimeError(f"curl received an unexpected status:\n{response}")
    if "X-Riwo-Interop: server\n" not in response:
        raise RuntimeError(f"curl did not receive the Riwo marker:\n{response}")
    if not response.rstrip().endswith("riwo-server:external-client"):
        raise RuntimeError(f"curl received an unexpected body:\n{response}")


def external_request_with_stdlib(url):
    request = urllib.request.Request(
        url,
        method="POST",
        data=b"external-client",
        headers={"X-Interop-Client": "external"},
    )
    with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
        if response.status != 202:
            raise RuntimeError(f"urllib received HTTP {response.status}")
        if response.headers.get("X-Riwo-Interop") != "server":
            raise RuntimeError("urllib did not receive the Riwo marker")
        if response.read() != b"riwo-server:external-client":
            raise RuntimeError("urllib received an unexpected body")


class ExternalServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class ExternalHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        target = urllib.parse.urlsplit(self.path)
        valid = (
            target.path == "/interop"
            and urllib.parse.parse_qs(target.query) == {"value": ["42"]}
            and self.headers.get("X-Interop-Client") == "riwo"
        )
        body = (
            b"external-server:riwo-client"
            if valid
            else b"external-server:rejected"
        )
        self.send_response(202 if valid else 400)
        self.send_header("X-Interop-Server", "python-stdlib")
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, _format, *args):
        del args


def test_riwo_client(peer):
    server = ExternalServer(("127.0.0.1", 0), ExternalHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        url = f"http://127.0.0.1:{server.server_port}/interop?value=42"
        completed = subprocess.run(
            [peer, "client", url],
            check=False,
            capture_output=True,
            text=True,
            timeout=TIMEOUT,
        )
        if completed.returncode:
            raise RuntimeError(
                "Riwo client rejected the independent HTTP server\n"
                + completed.stdout
                + completed.stderr
            )
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=TIMEOUT)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--peer", required=True)
    parser.add_argument("--backend", choices=("curl", "python-stdlib"), required=True)
    parser.add_argument("--curl")
    args = parser.parse_args()

    process, port = start_riwo_server(args.peer)
    try:
        url = f"http://127.0.0.1:{port}/interop/tool?value=42"
        if args.backend == "curl":
            external_request_with_curl(args.curl, url)
        else:
            external_request_with_stdlib(url)
        finish_process(process, "Riwo HTTP server")
        process = None
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            process.communicate(timeout=TIMEOUT)

    test_riwo_client(args.peer)
    print(f"HTTP interoperability passed with {args.backend}")


if __name__ == "__main__":
    main()
