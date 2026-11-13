"""Decompiled (CFR) Java class -> a JS class with its types stripped: just enough Java for the biome-builder and surface-rule
classes Soulrend's mods write in code (import.py runs them in node against stand-ins).  python3 j2js.py Foo.java [--static]"""
import re, sys
KW = {'return','throw','else','new','case','if','for','while','do','this','super'}
TYPE = r'[A-Za-z_$][\w.$]*(?:<[^;=(){}]*?>)?(?:\[\])*'

def split_params(s):
    out, d, cur = [], 0, ''
    for ch in s:
        if ch == '<': d += 1
        elif ch == '>': d -= 1
        if ch == ',' and d == 0: out.append(cur); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur)
    return [re.findall(r'[\w$]+', p)[-1] for p in out]

def arrays(src):   # new T[][]{ ... } -> [ ... ]  (nested braces -> brackets)
    while True:
        m = re.search(r'new [\w.<>]+(?:\[\])+\s*\{', src)
        if not m: break
        i = m.end() - 1; d = 0; j = i; body = list(src)
        while True:
            if src[j] == '{': d += 1; body[j] = '['
            elif src[j] == '}': d -= 1; body[j] = ']'
            if d == 0: break
            j += 1
        src = src[:m.start()] + ''.join(body[i:j + 1]) + src[j + 1:]
    src = re.sub(r'new [\w.<>]+(?:\[\])+\s*(?=\[)', '', src)   # (an array inside one already turned)
    src = re.sub(r'new [\w.<>]+\[(\w+)\](?:\[\])*', r'new Array(\1)', src)
    return src

STATIC = False
def conv(java):
    s = re.sub(r'/\*.*?\*/', '', java, flags=re.S)
    s = re.sub(r'//[^\n]*', '', s)
    s = '\n'.join(l for l in s.split('\n') if not re.match(r'\s*(import|package) ', l) and not re.match(r'\s*@\w+', l))
    s = arrays(s)
    s = re.sub(r'\((?:float|int|long|double|boolean|Object|[A-Z][\w.$]*(?:<[^()]*?>)?(?:\[\])*)\)(?=[\w(.\-"$[])', '', s)   # casts
    s = re.sub(r'(\d)[fFL]\b', r'\1', s)
    s = s.replace('->', '=>')
    s = re.sub(r'\.toArray\([^()]*::new\)', '.toArray()', s)
    s = re.sub(r'(?:public |private |protected )*(?:final )?(?:abstract )?class (\w+)[^{]*\{', r'class \1 {', s)
    out = []
    for line in s.split('\n'):
        st = line.strip()
        ind = line[:len(line) - len(line.lstrip())]
        m = re.match(r'(?:public|private|protected)\s+(\w+)\s*\((.*)\)\s*\{$', st)
        if m and len(ind) <= 4:   # constructor
            out.append(ind + 'constructor(' + ', '.join(split_params(m.group(2))) + ') {'); continue
        m = re.match(r'(?:(?:public|private|protected|static|final|synchronized)\s+)*(' + TYPE + r')\s+(\w+)\s*\((.*)\)\s*(?:throws [\w., ]+)?\{$', st)
        if m and m.group(1) not in KW and len(ind) <= 4:   # a method
            out.append(ind + ('static ' if STATIC and ' static ' in ' '+st else '') + m.group(2) + '(' + ', '.join(split_params(m.group(3))) + ') {'); continue
        m = re.match(r'(?:(?:public|private|protected|static|final)\s+)*(' + TYPE + r')\s+([\w$]+)\s*(=.*|;)$', st)
        if m and m.group(1) in KW: m = None
        if m and len(ind) <= 4:   # a field
            out.append(ind + ('static ' if STATIC and ' static ' in ' '+st else '') + m.group(2) + (' ' + m.group(3) if m.group(3) != ';' else ';')); continue
        if len(ind) <= 4 and re.match(TYPE + r'\s+\w+\s*;$', st) and st.split()[0] not in KW:   # field, package-private
            out.append(ind + st.split()[-1]); continue
        line = re.sub(r'for \((?:final )?' + TYPE + r' ([\w$]+) : ', r'for (const \1 of ', line)
        line = re.sub(r'for \((?:int|long) ', 'for (let ', line)
        m = re.match(r'(\s*)(?:final )?(' + TYPE + r')\s+([\w$]+)\s*(=|;)', line)
        if m and m.group(2) not in KW and len(ind) > 4:
            line = m.group(1) + 'let ' + m.group(3) + ' ' + line[m.end(3):].lstrip()
        out.append(line)
    res = '\n'.join(out)
    if STATIC:   # bare static fields -> Class.FIELD
        cn = re.search(r'class (\w+)', res).group(1)
        names = re.findall(r'^\s{0,4}static (\w+) =', res, flags=re.M)
        lines = res.split('\n')
        for i, l in enumerate(lines):
            m = re.match(r'(\s{0,4}static \w+ =)(.*)$', l)
            head, body = (m.group(1), m.group(2)) if m else ('', l)
            for n in names: body = re.sub(r'(?<![.\w])' + n + r'\b', cn + '.' + n, body)
            l = head + body
            lines[i] = l
        res = '\n'.join(lines)
    return res

if __name__ == '__main__':
    STATIC = '--static' in sys.argv
    print(conv(open(sys.argv[1]).read()))
