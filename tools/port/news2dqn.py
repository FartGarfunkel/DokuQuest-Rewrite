#!/usr/bin/env python3
"""Convert the original launcher/news.html (patch notes) into the launcher's news format.

  python3 tools/port/news2dqn.py <DokuQuest-0.9.0.90>/launcher/news.html assets/news/alpha.dqn

Format (assets/news/*.dqn, rendered by src/launcher/news.cpp):
  # comment
  ## Title | Date          starts an entry
  - text                  bullet
  text                    paragraph
  inline: **bold**  *italic*  `code`
Patch-note rules: no entry mentions Herobrine, and every entry ends with "- Removed Herobrine.".
"""
import html, re, sys
from html.parser import HTMLParser


class P(HTMLParser):
    def __init__(self):
        super().__init__()
        self.entries = []   # [title, date, [lines]]
        self.cur = None; self.buf = None; self.kind = None; self.in_date = False; self.in_h2 = False

    def handle_starttag(self, tag, a):
        if tag == 'article': self.cur = ['', '', []]; self.entries.append(self.cur)
        elif tag == 'h2': self.in_h2 = True
        elif tag == 'span' and self.in_h2: self.in_date = True
        elif tag in ('li', 'p'): self.buf = ''; self.kind = '- ' if tag == 'li' else ''
        elif self.buf is not None and tag == 'b': self.buf += '**'
        elif self.buf is not None and tag == 'i': self.buf += '*'
        elif self.buf is not None and tag == 'code': self.buf += '`'
        elif self.buf is not None and tag == 'br': self.buf += ' '

    def handle_endtag(self, tag):
        if tag == 'h2': self.in_h2 = False
        elif tag == 'span': self.in_date = False
        elif tag in ('li', 'p') and self.buf is not None:
            self.cur[2].append((self.kind, ' '.join(self.buf.split()))); self.buf = None
        elif self.buf is not None and tag == 'b': self.buf += '**'
        elif self.buf is not None and tag == 'i': self.buf += '*'
        elif self.buf is not None and tag == 'code': self.buf += '`'

    def handle_data(self, d):
        if self.in_date: self.cur[1] += d
        elif self.in_h2: self.cur[0] += d
        elif self.buf is not None: self.buf += d


SYMBOLS = [(' (⚔)', ''), (' (🛡)', ''), (' (✦)', ''), ('☺ ', ''), ('☺', ''), ('→', '->'), ('←', '<-'), ('▲', '^'), ('▼', 'v')]


def clean(text):
    for a, b in SYMBOLS:   # the launcher's pixel font has no glyphs for these
        text = text.replace(a, b)
    # drop every sentence that names Herobrine; the closing line is added back by the caller
    parts = re.split(r'(?<=[.!?])\s+', text)
    return ' '.join(p for p in parts if 'herobrine' not in p.lower()).strip()


def main(src, dst):
    p = P(); p.feed(open(src, encoding='utf-8').read())
    out = ['# DokuQuest patch notes from the HTML/JS alphas (converted from launcher/news.html by tools/port/news2dqn.py).',
           '# Format: "## Title | Date" starts an entry; "- " bullet; other lines are paragraphs; **bold** *italic* `code`.', '']
    for title, date, lines in p.entries:
        out.append('## ' + ' '.join(html.unescape(title).split()) + ' | ' + date.strip())
        for kind, text in lines:
            t = clean(html.unescape(text))
            if t and t not in ('**', '*'): out.append(kind + t)
        out.append('- Removed Herobrine.')
        out.append('')
    open(dst, 'w', encoding='utf-8').write('\n'.join(out))
    print(f'{len(p.entries)} entries -> {dst}')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
