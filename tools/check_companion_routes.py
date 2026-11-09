#!/usr/bin/env python3
"""Fail if an HTTP companion route has no USB-serial counterpart (or vice versa).

The firmware has two route tables: src/companion/http/CompanionHttp.cpp (makeRoute) and
src/companion/serial/CompanionSerial.cpp (dispatchRequest). A feature added to only one of
them silently 404s on the other transport (happened with /cover on 2026-11-09).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
http = ROOT / "src/companion/http/CompanionHttp.cpp"
serial = ROOT / "src/companion/serial/CompanionSerial.cpp"

http_routes = set()
for path, method in re.findall(r'makeRoute\("([^"]+)",\s*HTTP_(\w+)', http.read_text()):
    http_routes.add((method, path.replace("{id}", "*")))

serial_routes = set()
text = serial.read_text()
for method, path in re.findall(r'method == "(\w+)" && path == "([^"]+)"', text):
    serial_routes.add((method, path))
for method, prefix, suffix in re.findall(r'method == "(\w+)" && resourcePath\(path, "([^"]+)"(?:, "([^"]+)")?\)', text):
    serial_routes.add((method, prefix + "*" + (suffix or "")))
for method, path in re.findall(r'method == "(\w+)" && path\.starts_with\("([^"]+)"\)', text):
    serial_routes.add((method, path + "*"))

ignore = {("OPTIONS", "/api/v2/*")}  # CORS preflight exists only for browsers (HTTP)
missing_serial = sorted(r for r in http_routes - serial_routes if r not in ignore)
missing_http = sorted(serial_routes - http_routes)
for m, p in missing_serial:
    print(f"HTTP only (missing on USB): {m} {p}")
for m, p in missing_http:
    print(f"USB only (missing on HTTP): {m} {p}")
print(f"{len(http_routes)} HTTP routes, {len(serial_routes)} USB routes")
sys.exit(1 if missing_serial or missing_http else 0)
