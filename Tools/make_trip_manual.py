"""Trip-A-Tron's manual for the launcher's docs viewer: the PDF (Piku's 2003 transcription of the 1988 manual,
Private/trip-a-tron-manual.pdf) -> plain ASCII text, wrapped to 78 columns, as MANUAL.TXT in Trip-A-Tron's folder
(the launcher shows it with X, then Yak's README.DOC; package_portable.ps1 copies the folder, so it ships).
The app can't read a PDF itself (compressed streams, embedded fonts): it is converted here, once.
Needs pdftotext (poppler; Git for Windows has it in mingw64\\bin). pypdf splits words ("ke yboard") - not used.
usage: python -P Tools/make_trip_manual.py [<manual.pdf>] [<out MANUAL.TXT>]"""
import re
import subprocess
import sys
import textwrap

pdf = sys.argv[1] if len(sys.argv) > 1 else 'Private/trip-a-tron-manual.pdf'
out = sys.argv[2] if len(sys.argv) > 2 else 'Private/new/trip_a_t/TRIP_A_T.RON/MANUAL.TXT'

text = subprocess.run(['pdftotext', '-enc', 'UTF-8', pdf, '-'], capture_output=True, check=True).stdout.decode('utf-8')

# The viewer's font is ASCII.
ascii_map = {'‘': "'", '’': "'", '“': '"', '”': '"', '–': '-', '—': '-',
             '…': '...', '•': '-', '©': '(c)', '´': "'", ' ': ' ', '×': 'x',
             '': '-', 'é': 'e', 'è': 'e', 'à': 'a', 'ü': 'u', 'ö': 'o'}
text = ''.join(ascii_map.get(ch, ch) for ch in text)
text = ''.join(ch if ch in '\n\f' or 32 <= ord(ch) < 127 else '' for ch in text)

lines = []
for raw in text.replace('\f', '\n\n').split('\n'):
    line = raw.rstrip()
    if re.fullmatch(r'\s*-\s*\d+\s*-\s*', line):  # page numbers
        continue
    if not line.strip():
        if lines and lines[-1] != '':
            lines.append('')
        continue
    indent = '  ' if line.lstrip().startswith('- ') else ''
    wrapped = textwrap.wrap(line.strip(), 78, subsequent_indent=indent) or ['']
    lines.extend(wrapped)

# The contents page is a table of page numbers (meaningless here, and it comes out jumbled): from the first
# chapter on.
first = next((i for i, l in enumerate(lines) if l.startswith('- AN INTRODUCTION')), 0)
lines = lines[first:]

head = ['TRIP-A-TRON  -  THE MANUAL (JEFF MINTER, 1988)',
        'Typed up by James "Piku" Grimwood in 2003; converted from his PDF.', '']
body = '\r\n'.join(head + lines).strip() + '\r\n'
open(out, 'w', encoding='ascii', newline='').write(body)
print(f'{out}: {len(lines)} lines')
