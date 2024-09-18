#!/usr/bin/env python3
import sys
import os
from bs4 import BeautifulSoup

if len(sys.argv) != 2:
    print("Need argument (path to file)")
    sys.exit(1)

fname = sys.argv[1]
if not os.path.isfile(fname):
    print(f"Invalid path to file, cannot find {fname}.")
    sys.exit(1)

with open(fname, 'r', encoding='utf-8') as file:
    html_contents = file.read()

soup = BeautifulSoup(html_contents, 'html.parser')
ctr = 0
for tr in soup.find_all('tr'):
    if "test/" in tr.get_text():
        ctr += 1
        tr.decompose()

# Write the result back
with open(fname, 'w', encoding='utf-8') as file:
    file.write(str(soup))

print(f"File updated, removed {ctr} items from the table.")
