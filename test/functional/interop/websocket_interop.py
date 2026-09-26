#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
# SPDX-License-Identifier: MIT

import argparse
import asyncio
import inspect
import subprocess


TIMEOUT = 10


def start_server(command, description):
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    line = process.stdout.readline().strip()
    if not line.startswith("PORT "):
        stdout, stderr = process.communicate(timeout=TIMEOUT)
        raise RuntimeError(
            f"{description} did not publish a port: {line!r}\n{stdout}{stderr}"
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


def node_client(args, url):
    completed = subprocess.run(
        [args.node, args.node_helper, args.node_ws_module, "client", url],
        check=False,
        capture_output=True,
        text=True,
        timeout=TIMEOUT,
    )
    if completed.returncode:
        raise RuntimeError(
            "node-ws client rejected the Riwo server\n"
            + completed.stdout
            + completed.stderr
        )


async def python_websockets_client(url):
    import websockets

    options = {
        "subprotocols": ["interop.v1"],
        "open_timeout": 5,
        "close_timeout": 5,
    }
    header_argument = (
        "additional_headers"
        if "additional_headers" in inspect.signature(websockets.connect).parameters
        else "extra_headers"
    )
    options[header_argument] = {"X-Interop-Client": "external"}
    connection = websockets.connect(url, **options)
    async with connection as socket:
        if socket.subprotocol != "interop.v1":
            raise RuntimeError(f"unexpected subprotocol: {socket.subprotocol}")
        await socket.send("external-client")
        response = await socket.recv()
        if response != "riwo-server:external-client":
            raise RuntimeError(f"unexpected Riwo message: {response!r}")
        await socket.wait_closed()
        if socket.close_code != 1000:
            raise RuntimeError(f"unexpected Riwo close code: {socket.close_code}")


def python_websocket_client(url):
    import websocket

    socket = websocket.create_connection(
        url,
        timeout=5,
        subprotocols=["interop.v1"],
        header=["X-Interop-Client: external"],
        http_proxy_host=None,
        http_proxy_port=None,
    )
    try:
        if socket.getsubprotocol() != "interop.v1":
            raise RuntimeError(f"unexpected subprotocol: {socket.getsubprotocol()}")
        socket.send("external-client")
        response = socket.recv()
        if response != "riwo-server:external-client":
            raise RuntimeError(f"unexpected Riwo message: {response!r}")
        # recv() processes the peer's Close and emits the required Close reply.
        socket.recv()
    finally:
        socket.close()


def wscat_client(args, url):
    process = subprocess.Popen(
        [
            args.wscat,
            "--no-color",
            "--connect",
            url,
            "--subprotocol",
            "interop.v1",
            "--header",
            "X-Interop-Client: external",
            "--execute",
            "external-client",
            "--wait",
            "2",
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        returncode = process.wait(timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        process.terminate()
        process.wait(timeout=TIMEOUT)
        raise RuntimeError("wscat did not stop")
    output = process.stdout.read() + process.stderr.read()
    # wscat intentionally suppresses received-message output when stdout isn't
    # a terminal. Its exit status verifies the client side; the Riwo peer's
    # exit status below independently verifies the message and clean Close.
    if returncode:
        raise RuntimeError(f"wscat rejected the Riwo server\n{output}")


def test_external_client(args):
    process, port = start_server([args.peer, "server"], "Riwo WebSocket server")
    try:
        url = f"ws://127.0.0.1:{port}/interop?value=42"
        if args.backend == "node-ws":
            node_client(args, url)
        elif args.backend == "python-websockets":
            asyncio.run(python_websockets_client(url))
        elif args.backend == "python-websocket-client":
            python_websocket_client(url)
        else:
            wscat_client(args, url)
        finish_process(process, "Riwo WebSocket server")
        process = None
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            process.communicate(timeout=TIMEOUT)


def test_riwo_client_with_node(args):
    process, port = start_server(
        [args.node, args.node_helper, args.node_ws_module, "server"],
        "node-ws server",
    )
    try:
        url = f"ws://127.0.0.1:{port}/interop?value=42"
        completed = subprocess.run(
            [args.peer, "client", url],
            check=False,
            capture_output=True,
            text=True,
            timeout=TIMEOUT,
        )
        if completed.returncode:
            raise RuntimeError(
                "Riwo client rejected the node-ws server\n"
                + completed.stdout
                + completed.stderr
            )
        finish_process(process, "node-ws server")
        process = None
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            process.communicate(timeout=TIMEOUT)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--peer", required=True)
    parser.add_argument(
        "--backend",
        choices=("node-ws", "python-websockets", "python-websocket-client", "wscat"),
        required=True,
    )
    parser.add_argument("--node")
    parser.add_argument("--node-ws-module")
    parser.add_argument("--node-helper")
    parser.add_argument("--wscat")
    args = parser.parse_args()

    test_external_client(args)
    if args.backend == "node-ws":
        test_riwo_client_with_node(args)
    else:
        print("Selected WebSocket backend provides client-side coverage only")
    print(f"WebSocket interoperability passed with {args.backend}")


if __name__ == "__main__":
    main()
