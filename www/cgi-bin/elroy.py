#!/usr/bin/env python3
import cgi
import os
import html
import csv
import datetime

# Read form (works for both GET and POST in CGI)
form = cgi.FieldStorage()
name = form.getfirst("name", "").strip()
age = form.getfirst("age", "").strip()
method = os.environ.get("REQUEST_METHOD", "UNKNOWN").upper()

# Sanitize for HTML output
name_html = html.escape(name) if name else "<em>not provided</em>"
age_html = html.escape(age) if age else "<em>not provided</em>"

# Persist submission to a simple CSV in /tmp (safe for development)
outpath = "/tmp/elroy_submissions.csv"
try:
    need_header = not os.path.exists(outpath)
    with open(outpath, "a", newline="") as f:
        writer = csv.writer(f)
        if need_header:
            writer.writerow(["timestamp_utc", "method", "name", "age"])
        writer.writerow([datetime.datetime.utcnow().isoformat(), method, name, age])
    saved_msg = "Saved to " + outpath
except Exception as e:
    saved_msg = "Failed to save: " + str(e)

# Output HTML response
print("Content-Type: text/html; charset=utf-8")
print()
print("<!doctype html>")
print("<html><head><meta charset='utf-8'><title>Form result</title></head><body>")
print("<h1>Submission received</h1>")
print("<p>Request method: {}</p>".format(html.escape(method)))
print("<p>Name: {}</p>".format(name_html))
print("<p>Age: {}</p>".format(age_html))
print("<p>{}</p>".format(html.escape(saved_msg)))
print("<p><a href='/elroy.html'>Back to form</a></p>")
print("</body></html>")