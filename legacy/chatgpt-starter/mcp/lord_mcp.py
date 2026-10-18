#!/usr/bin/env python3
"""Minimal MCP stdio adapter for LordOS DEMO windows only. Python 3 stdlib."""
import json
import os
import socket
import sys

PROTOCOL = "2025-06-18"
TOOLS = [
    {"name": "lord_windows_list", "description": "List DEMO windows in LordOS starter; not actual Linux windows.", "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False}},
    {"name": "lord_windows_move", "description": "Move a DEMO window in LordOS starter, not a native Linux surface.", "inputSchema": {"type": "object", "properties": {"id": {"type": "integer", "minimum": 1}, "x": {"type": "integer", "minimum": -10000, "maximum": 10000}, "y": {"type": "integer", "minimum": -10000, "maximum": 10000}}, "required": ["id", "x", "y"], "additionalProperties": False}},
]

def call_core(command):
    path = f"/tmp/lordos-core-{os.getuid()}.sock"
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(2)
        s.connect(path)
        s.sendall((command + "\n").encode())
        s.shutdown(socket.SHUT_WR)
        buf = bytearray()
        while len(buf) < 65536:
            part = s.recv(4096)
            if not part:
                break
            buf.extend(part)
    return buf.decode("utf-8", errors="replace")

def handle(msg):
    method = msg.get("method")
    params = msg.get("params") or {}
    if method == "initialize":
        return {"protocolVersion": PROTOCOL, "capabilities": {"tools": {}}, "serverInfo": {"name": "lordos-starter-mcp", "version": "0.1.0"}}
    if method == "notifications/initialized":
        return None
    if method == "ping":
        return {}
    if method == "tools/list":
        return {"tools": TOOLS}
    if method == "tools/call":
        name = params.get("name")
        args = params.get("arguments") or {}
        try:
            if name == "lord_windows_list" and args == {}:
                command = "LIST"
            elif name == "lord_windows_move" and set(args) == {"id", "x", "y"} and all(type(args[k]) is int for k in ("id", "x", "y")) and args["id"] > 0 and all(-10000 <= args[k] <= 10000 for k in ("x", "y")):
                command = f"MOVE {args['id']} {args['x']} {args['y']}"
            else:
                return {"content": [{"type": "text", "text": "Invalid tool name or arguments"}], "isError": True}
            return {"content": [{"type": "text", "text": call_core(command)}]}
        except (OSError, UnicodeError) as e:
            return {"content": [{"type": "text", "text": f"Demo core unavailable: {e}"}], "isError": True}
    raise ValueError(f"Unsupported method: {method}")

def main():
    for line in sys.stdin:
        msg = None
        try:
            msg = json.loads(line)
            if not isinstance(msg, dict):
                continue
            result = handle(msg)
            if "id" in msg:
                out = {"jsonrpc": "2.0", "id": msg["id"]}
                if result is None:
                    out["result"] = {}
                else:
                    out["result"] = result
                print(json.dumps(out, ensure_ascii=False), flush=True)
        except Exception as exc:
            if isinstance(msg, dict) and "id" in msg:
                print(json.dumps({"jsonrpc": "2.0", "id": msg["id"], "error": {"code": -32603, "message": str(exc)}}), flush=True)

if __name__ == "__main__":
    main()
