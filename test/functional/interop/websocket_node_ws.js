#!/usr/bin/env node
// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

"use strict";

const WebSocket = require(process.argv[2]);

function fail(message) {
  process.stderr.write(`${message}\n`);
  process.exitCode = 1;
}

function client(url) {
  const socket = new WebSocket(url, ["interop.v1"], {
    headers: {"X-Interop-Client": "external"},
    handshakeTimeout: 5000,
  });
  let received = false;
  let upgraded = false;
  const timeout = setTimeout(() => {
    fail("node-ws client timed out");
    socket.terminate();
  }, 8000);

  socket.on("open", () => {
    if (socket.protocol !== "interop.v1") {
      fail(`unexpected subprotocol: ${socket.protocol}`);
      socket.close(1002, "subprotocol");
      return;
    }
    socket.send("external-client");
  });
  socket.on("upgrade", response => {
    upgraded = response.headers["x-riwo-interop"] === "server";
  });
  socket.on("message", (data, isBinary) => {
    if (isBinary || data.toString() !== "riwo-server:external-client") {
      fail(`unexpected Riwo message: ${data.toString()}`);
      socket.close(1002, "message");
      return;
    }
    received = true;
  });
  socket.on("error", error => fail(`node-ws client error: ${error.message}`));
  socket.on("close", code => {
    clearTimeout(timeout);
    if (!upgraded || !received || code !== 1000)
      fail(
        `unexpected Riwo close: code=${code}, upgraded=${upgraded}, ` +
        `received=${received}`);
  });
}

function server() {
  const listener = new WebSocket.Server({
    host: "127.0.0.1",
    port: 0,
    handleProtocols(protocols) {
      const supported = typeof protocols.has === "function" ?
        protocols.has("interop.v1") : protocols.includes("interop.v1");
      return supported ? "interop.v1" : false;
    },
  });
  let valid = true;
  const timeout = setTimeout(() => {
    fail("node-ws server timed out");
    listener.close();
  }, 8000);

  listener.on("listening", () => {
    process.stdout.write(`PORT ${listener.address().port}\n`);
  });
  listener.on("headers", headers => {
    headers.push("X-Interop-Server: node-ws");
  });
  listener.on("connection", (socket, request) => {
    valid = request.url === "/interop?value=42" &&
      request.headers["x-interop-client"] === "riwo" &&
      socket.protocol === "interop.v1";
    socket.on("message", (data, isBinary) => {
      valid = valid && !isBinary && data.toString() === "riwo-client";
      socket.send(valid ?
        "external-server:riwo-client" : "external-server:rejected");
    });
    socket.on("close", code => {
      clearTimeout(timeout);
      if (code !== 1000)
        valid = false;
      listener.close(() => {
        if (!valid)
          fail(`Riwo client exchange failed; close code=${code}`);
      });
    });
  });
  listener.on("error", error => fail(`node-ws server error: ${error.message}`));
}

if (process.argv[3] === "client" && process.argv.length === 5)
  client(process.argv[4]);
else if (process.argv[3] === "server" && process.argv.length === 4)
  server();
else {
  process.stderr.write(
    "Usage: websocket_node_ws.js <ws-module> client <url> | server\n");
  process.exitCode = 2;
}
