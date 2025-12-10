# ...existing code...
#!/usr/bin/env python3
import os
import sys
import html
from urllib.parse import parse_qs

# Only accept GET for this endpoint
method = os.environ.get("REQUEST_METHOD", "").upper()

print("Content-Type: text/html; charset=utf-8")
print()

if method != "GET":
    print("<!doctype html><html><body>")
    print("<h1>405 Method Not Allowed</h1>")
    print("<p>This endpoint accepts GET only.</p>")
    print("<p><a href='/elroy.html'>Back to form</a></p>")
    print("</body></html>")
    sys.exit(0)

# Parse query string (e.g. /cgi-bin/elroy.py?name=Elroy&age=32)
qs = os.environ.get("QUERY_STRING", "")
params = parse_qs(qs, keep_blank_values=True)
name = params.get("name", [""])[0]
age = params.get("age", [""])[0]

name_html = html.escape(name) if name else "<em>not provided</em>"
age_html = html.escape(age) if age else "<em>not provided</em>"

print("<!doctype html>")
print("<html><head><meta charset='utf-8'><title>Form result</title></head><body>")
print("<h1>Submission received (GET)</h1>")
print("<p>Name: {}</p>".format(name_html))
print("<p>Age: {}</p>".format(age_html))
print("<p><a href='/elroy.html'>Back to form</a></p>")
print("</body></html>")
# ...existing code...