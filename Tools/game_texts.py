r"""Writes the games' built-in texts as plain text: Gridrunner's "game secrets" and "additional information",
and Llamatron's title screen scroller (version screen, instructions, credits, story, enemies, power-ups).
The texts use the games' display codes: \c<colour>, \r (new line), \g<2 bytes> (position), \s (Llamatron: \s 0
<icon> = a little picture; Gridrunner: \s <1 byte> = layout), \q (big letters), \e (page end: "press fire").
usage: game_texts.py <gridrunner RAM dump (--dump)> <tron.bin> <out.txt>"""
import re
import sys
import textwrap

BS = 0x5C  # backslash


def decode(m, start, icon_len, max_len=0x4000):
    r"""The text at start, until its end: \e not followed by another code, or two zero bytes."""
    out, i, end = [], start, min(len(m), start + max_len)
    while i < end:
        b = m[i]
        if b == BS and i + 1 < end:
            code = chr(m[i + 1])
            if code == 'c':
                i += 3
                continue
            if code == 'g':
                i += 4
                continue
            if code == 's':
                if icon_len == 2 and m[i + 2] == 0:
                    out.append('[*]')
                i += 2 + icon_len
                continue
            if code == 'r':
                out.append('\n')
                i += 2
                continue
            if code in 'q@':
                i += 2
                continue
            if code == 'e':
                out.append('\n\f')  # page break
                i += 2
                if i < end and m[i] != BS:  # that was the last page
                    break
                continue
        if b == 0:
            if i + 1 < end and m[i + 1] == 0:
                break
            i += 1
            continue
        if 32 <= b < 127:
            out.append(chr(b))
        i += 1
    return ''.join(out)


def tidy(text, width=78):
    pages = []
    for page in text.split('\f'):
        lines = []
        for para in page.split('\n'):
            para = re.sub(r'  +', ' ', para).strip()
            lines.append(textwrap.fill(para, width) if para else '')
        page = re.sub(r'\n{3,}', '\n\n', '\n'.join(lines)).strip('\n')
        if page.strip():
            pages.append(page)
    return ('\n\n' + ' ' * 35 + '* * *\n\n').join(pages)


def main():
    grid = open(sys.argv[1], 'rb').read()
    tron = open(sys.argv[2], 'rb').read()
    sections = [
        ('GRIDRUNNER: GAME SECRETS', decode(grid, 0x10EB9, 1)),
        ('GRIDRUNNER: ADDITIONAL INFORMATION', decode(grid, 0x4976C, 1)),
        ('LLAMATRON: THE TITLE SCREEN SCROLLER', decode(tron, 0x39DE4, 2)),
    ]
    with open(sys.argv[3], 'w', encoding='utf-8', newline='\r\n') as f:
        f.write('Texts built into Gridrunner and Llamatron (Jeff Minter / Llamasoft), from the game disk.\n')
        f.write('[*] marks a little picture in the original (an enemy, a beastie, a power-up).\n\n')
        for title, text in sections:
            f.write('=' * 78 + '\n' + title + '\n' + '=' * 78 + '\n\n' + tidy(text) + '\n\n\n')
    print('written', sys.argv[3])


if __name__ == '__main__':
    main()
