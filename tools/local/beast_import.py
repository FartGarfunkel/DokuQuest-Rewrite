#!/usr/bin/env python3
"""Alex's Mobs (Soulrend's copy) into our game as built-in creatures, local only (the mod's art and moves are not ours to
ship): data/local_models/beasts.json (git-ignored), merged by the registry (core/registry.cpp) like the bosses'.
  python3 the creature import [data-dir] [--only=raccoon,tiger]
  python3 the creature voice import [data-dir]     # then: their voices (it writes them into the creatures)

For each creature the mod draws with a Citadel AdvancedEntityModel (client/model/Model*.java):
  - its boxes, read from the model's constructor, turned into a Bedrock-style geometry (Blockbench's Java -> Bedrock rule:
    pivot (-Px, 24 - Py, Pz), cube origin (-(Px + x + w), 24 - (Py + y + h), Pz + z), turns [-rx, -ry, rz] in degrees)
    and voxelised from the mod's picture by tools/local/boss_import.py (a voxel a model pixel), x mirrored to ours;
  - its setupAnim(), translated statement by statement into a small program ("am" in the model) that sim/beast_anim.cpp runs
    each frame: Citadel's walk / swing / flap / bob / chain* / progressRotation(Prev) / progressPosition(Prev) / faceTarget,
    assignments to a box's rotateAngle* / rotationPoint*, locals, ifs, the maths; the entity's own values (sitProgress,
    isFlying()...) become named inputs ("ents") the game feeds from our creature's state;
  - its animate() keyframes (Citadel's ModelAnimator: startKeyframe / rotate / move / endKeyframe / setStaticKeyframe /
    resetKeyframe, eased with sin) sampled into clips, played on an attack / eating;
  - the renderer's picture and scale, the registry's size and category, the entity's attributes and goals -> a mobs.json
    entry (new species), or just the model for the ones we already have (our behaviour kept, the mod's body worn).
"""
import json, math, pathlib, re, sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import geo_boss_import as G
from PIL import Image

AM = pathlib.Path('/home/matt/soulrend_extract/src/alexsmobs-1.22.9/com/github/alexthe666/alexsmobs')
ASSETS = pathlib.Path('/home/matt/soulrend_extract/mods/alexsmobs-1.22.9/assets/alexsmobs')
CFG = pathlib.Path('/home/matt/Documents/Soulrend/Soulrend - Dark Fantasy RPG Soulslike/minecraft/config')

# the mod's creatures we already keep under another name (our behaviour stays; their body is the mod's)
ALIAS = {'grizzly_bear': 'brown_bear', 'anaconda': 'green_snake'}
SKIP = {'bone_serpent_part', 'cachalot_echo', 'void_worm_part', 'centipede_body', 'centipede_tail', 'anaconda_part',
        'murmur_head', 'hemolymph', 'cosmic_cod_part', 'skreecher'}   # (skreecher: not wanted in the game)

# ------------------------------------------------------------------ Java tokens and a parser for the bits we need
TOK = re.compile(r'''\s*(?:(?P<num>(?:0x[0-9a-fA-F]+|\d+\.\d*(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?|\d+(?:[eE][+-]?\d+)?)[fFdDlL]?)
  |(?P<str>"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(?P<id>[A-Za-z_$][\w$]*)
  |(?P<op>>>>=|<<=|>>=|>>>|\+\+|--|->|::|&&|\|\||[+\-*/%&|^]=|[=!<>]=|<<|>>|[-+*/%=<>!&|^~?:;,.(){}\[\]@]))''', re.X)


def tokens(src):
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    src = re.sub(r'//[^\n]*', ' ', src)
    out, i = [], 0
    while i < len(src):
        m = TOK.match(src, i)
        if not m or m.end() == i:
            if src[i:].strip() == '': break
            i += 1; continue
        i = m.end()
        k = m.lastgroup
        out.append((k, m.group(k)))
    return out


def num(s):
    s = s.rstrip('fFdDlL')
    return float(int(s, 16)) if s.lower().startswith('0x') else float(s)


PRIM = {'float', 'double', 'int', 'long', 'boolean', 'short', 'byte', 'char', 'var'}


class P:
    def __init__(self, toks): self.t = toks; self.i = 0
    def peek(self, k=0): return self.t[self.i + k] if self.i + k < len(self.t) else ('eof', '')
    def v(self, k=0): return self.peek(k)[1]
    def eat(self, s=None):
        tk = self.peek()
        if s is not None and tk[1] != s: raise SyntaxError('want %s got %s at %d' % (s, tk[1], self.i))
        self.i += 1; return tk
    def accept(self, s):
        if self.v() == s: self.i += 1; return True
        return False

    # ---- statements
    def block(self):
        self.eat('{'); out = []
        while self.v() != '}' and self.peek()[0] != 'eof': out += self.stmt()
        self.eat('}'); return out

    def skip_balanced(self, open_, close):
        d = 0
        while True:
            tk = self.eat()[1]
            if tk == open_: d += 1
            elif tk == close:
                d -= 1
                if d == 0: return

    def skip_stmt(self):
        d = 0
        while self.peek()[0] != 'eof':
            tk = self.v()
            if tk in '({[': d += 1
            elif tk in ')}]': d -= 1
            self.i += 1
            if d <= 0 and tk == ';': return
            if d == 0 and tk == '}': return

    def is_decl(self):
        k, v = self.peek()
        if k != 'id': return False
        if v == 'final': return True
        j = 1
        if v in PRIM or v[0].isupper():
            if self.v(1) == '<':   # generics
                d = 0
                while True:
                    x = self.v(j)
                    if x == '<': d += 1
                    elif x == '>': d -= 1
                    elif x == '>>': d -= 2
                    j += 1
                    if d <= 0 or self.peek(j)[0] == 'eof': break
            while self.v(j) == '.' and self.peek(j + 1)[0] == 'id': j += 2   # (Outer.Inner)
            while self.v(j) == '[' and self.v(j + 1) == ']': j += 2
            return self.peek(j)[0] == 'id' and self.v(j + 1) in ('=', ';', ',')
        return False

    def stmt(self):
        v = self.v()
        if v == '{': return [('block', self.block())]
        if v == ';': self.eat(); return []
        if v == 'if':
            self.eat(); self.eat('('); c = self.expr(); self.eat(')')
            a = self.stmt(); b = []
            if self.accept('else'): b = self.stmt()
            return [('if', c, a, b)]
        if v == 'switch':
            self.eat(); self.eat('('); c = self.expr(); self.eat(')'); self.eat('{')
            cases = []; cur = None
            while self.v() != '}' and self.peek()[0] != 'eof':
                if self.v() == 'case':
                    self.eat(); val = self.expr()
                    if not self.accept(':'): self.accept('->')
                    cur = [val, []]; cases.append(cur); continue
                if self.v() == 'default':
                    self.eat()
                    if not self.accept(':'): self.accept('->')
                    cur = [None, []]; cases.append(cur); continue
                st = self.stmt()
                if cur is not None: cur[1] += st
            self.eat('}')
            return [('switch', c, cases)]
        if v in ('for', 'while', 'do', 'try', 'synchronized'):
            self.eat()
            if v == 'do':
                self.stmt(); self.eat('while'); self.skip_balanced('(', ')'); self.accept(';'); return [('skip', v)]
            if self.v() == '(': self.skip_balanced('(', ')')
            if v == 'try':
                self.stmt()
                while self.v() in ('catch', 'finally'):
                    self.eat()
                    if self.v() == '(': self.skip_balanced('(', ')')
                    self.stmt()
                return [('skip', v)]
            self.stmt(); return [('skip', v)]
        if v == 'return':
            self.eat()
            if self.v() != ';': self.expr()
            self.accept(';'); return [('ret',)]
        if v in ('break', 'continue', 'throw'): self.skip_stmt(); return [('skip', v)]
        if self.is_decl():
            while self.v() == 'final': self.eat()
            self.eat()   # type
            if self.v() == '<':
                d = 0
                while True:
                    x = self.eat()[1]
                    if x == '<': d += 1
                    elif x == '>': d -= 1
                    elif x == '>>': d -= 2
                    if d <= 0: break
            while self.v() == '.' and self.peek(1)[0] == 'id': self.eat(); self.eat()
            while self.v() == '[': self.eat('['); self.eat(']')
            out = []
            while True:
                name = self.eat()[1]
                e = None
                if self.accept('='): e = self.expr()
                out.append(('let', name, e))
                if not self.accept(','): break
            self.accept(';'); return out
        start = self.i
        try:
            e = self.expr(); self.accept(';')
            return [('expr', e)]
        except (SyntaxError, IndexError):
            self.i = start; self.skip_stmt(); return [('skip', 'unparsed')]

    # ---- expressions
    def expr(self):
        lhs = self.ternary()
        v = self.v()
        if v in ('=', '+=', '-=', '*=', '/='):
            self.eat(); rhs = self.expr()
            return ('assign', v, lhs, rhs)
        return lhs

    def ternary(self):
        c = self.binop(0)
        if self.accept('?'):
            a = self.expr(); self.eat(':'); b = self.expr()
            return ('?', c, a, b)
        return c

    LEVELS = [('||',), ('&&',), ('|',), ('^',), ('&',), ('==', '!='), ('<', '>', '<=', '>=', 'instanceof'), ('<<', '>>', '>>>'), ('+', '-'), ('*', '/', '%')]

    def binop(self, lv):
        if lv >= len(self.LEVELS): return self.unary()
        a = self.binop(lv + 1)
        while self.v() in self.LEVELS[lv] and self.peek()[0] == 'op' or (self.v() == 'instanceof' and lv == 6):
            op = self.eat()[1]
            if op == 'instanceof':
                self.eat()
                while self.v() == '.': self.eat(); self.eat()
                if self.peek()[0] == 'id': self.eat()
                a = ('num', 0.0); continue
            b = self.binop(lv + 1)
            a = (op, a, b)
        return a

    def unary(self):
        v = self.v()
        if v == '-' and self.peek()[0] == 'op': self.eat(); return ('neg', self.unary())
        if v == '+' and self.peek()[0] == 'op': self.eat(); return self.unary()
        if v == '!': self.eat(); return ('not', self.unary())
        if v == '~': self.eat(); return ('neg', self.unary())
        if v in ('++', '--'): self.eat(); x = self.unary(); return ('assign', '+=' if v == '++' else '-=', x, ('num', 1.0))
        if v == '(' and self.peek(1)[0] == 'id' and self.v(2) == ')' and (self.v(1) in PRIM or self.v(1)[0].isupper()):
            nk, nv = self.peek(3)
            if nk in ('id', 'num', 'str') or nv in ('(', '!', '-') and self.v(1) in PRIM or nv in ('(',):
                self.eat(); self.eat(); self.eat(); return self.unary()   # a cast
        if v == '(' and self.peek(1)[0] == 'id' and self.v(2) == '.' :   # (pkg.Type)x
            j = 1
            while self.peek(j)[0] == 'id' and self.v(j + 1) == '.': j += 2
            if self.peek(j)[0] == 'id' and self.v(j + 1) == ')' and self.v(j)[0].isupper() and self.peek(j + 2)[0] in ('id', 'num'):
                self.i += j + 2; return self.unary()
        return self.postfix(self.primary())

    def args(self):
        self.eat('('); out = []
        while self.v() != ')':
            out.append(self.expr())
            if not self.accept(','): break
        self.eat(')'); return out

    def primary(self):
        k, v = self.peek()
        if k == 'num': self.eat(); return ('num', num(v))
        if k == 'str': self.eat(); return ('str', v)
        if v == '(':
            self.eat(); e = self.expr(); self.eat(')'); return e
        if v == 'new':
            self.eat(); name = self.eat()[1]
            while self.v() == '.': self.eat(); name = self.eat()[1]
            if self.v() == '<': self.skip_balanced('<', '>')
            if self.v() == '[':
                while self.v() == '[':
                    self.eat('[')
                    if self.v() != ']': self.expr()
                    self.eat(']')
                if self.v() == '{':
                    self.eat('{'); items = []
                    while self.v() != '}':
                        items.append(self.expr())
                        if not self.accept(','): break
                    self.eat('}')
                    return ('array', items)
                return ('array', [])
            a = self.args()
            if self.v() == '{': self.skip_balanced('{', '}')
            return ('new', name, a)
        if v in ('true', 'false'): self.eat(); return ('bool', 1.0 if v == 'true' else 0.0)
        if v == 'null': self.eat(); return ('num', 0.0)
        if k == 'id': self.eat(); return ('name', v)
        raise SyntaxError('primary %r' % (v,))

    def postfix(self, e):
        while True:
            v = self.v()
            if v == '.':
                self.eat(); n = self.eat()[1]
                if self.v() == '(': e = ('call', e, n, self.args())
                else: e = ('field', e, n)
            elif v == '(' and e[0] == 'name':
                e = ('call', None, e[1], self.args())
            elif v == '[':
                self.eat('['); ix = self.expr(); self.eat(']'); e = ('index', e, ix)
            elif v in ('++', '--'):
                self.eat(); e = ('assign', '+=' if v == '++' else '-=', e, ('num', 1.0))
            elif v == '::':
                self.eat(); self.eat(); e = ('num', 0.0)
            elif v == '->':   # a lambda: skip its body
                self.eat()
                if self.v() == '{': self.skip_balanced('{', '}')
                else: self.expr()
                e = ('num', 0.0)
            else: return e


def find_method(src_toks, name):
    """the parameter names and the body tokens of the first method called name (with a body)"""
    t = src_toks
    for i in range(len(t) - 2):
        if t[i][1] == name and t[i + 1][1] == '(' and i > 0 and (t[i - 1][0] == 'id' and t[i - 1][1] not in ('new', 'return', 'this') or t[i - 1][1] == '>'):
            j = i + 1; d = 0; params = []
            seg = []
            while True:
                x = t[j][1]
                if x == '(': d += 1
                elif x == ')':
                    d -= 1
                    if d == 0: break
                elif x == ',' and d == 1:
                    params.append(seg); seg = []; j += 1; continue
                if d >= 1 and not (x == '(' and d == 1): seg.append(t[j])
                j += 1
            if seg: params.append(seg)
            j += 1
            while j < len(t) and t[j][1] not in ('{', ';'): j += 1
            if j >= len(t) or t[j][1] == ';': continue
            k = j; d = 0
            while True:
                x = t[k][1]
                if x == '{': d += 1
                elif x == '}':
                    d -= 1
                    if d == 0: break
                k += 1
            pnames = [p[-1][1] for p in params if p]
            return pnames, t[j:k + 1]
    return None, None


def method_names(src_toks):
    out = set()
    for i in range(1, len(src_toks) - 1):
        if src_toks[i][0] == 'id' and src_toks[i + 1][1] == '(' and src_toks[i - 1][0] == 'id' and src_toks[i - 1][1] not in ('new', 'return', 'this', 'else'):
            out.add(src_toks[i][1])
    return out


# ------------------------------------------------------------------ the model: boxes from the constructor
ATTR = {'rotateAngleX': 0, 'rotateAngleY': 1, 'rotateAngleZ': 2, 'rotationPointX': 3, 'rotationPointY': 4, 'rotationPointZ': 5,
        'xRot': 0, 'yRot': 1, 'zRot': 2, 'x': 3, 'y': 4, 'z': 5}


def cval(e, env=None):
    """a constant expression's value (Maths.rad, casts, arithmetic), or None"""
    env = env or {}
    k = e[0]
    if k in ('num', 'bool'): return e[1]
    if k == 'neg': v = cval(e[1], env); return None if v is None else -v
    if k == 'name': return env.get(e[1])
    if k in ('+', '-', '*', '/'):
        a, b = cval(e[1], env), cval(e[2], env)
        if a is None or b is None: return None
        return a + b if k == '+' else a - b if k == '-' else a * b if k == '*' else (a / b if b else 0.0)
    if k == 'call':
        args = [cval(a, env) for a in e[3]]
        if any(a is None for a in args): return None
        n = e[2]
        if n in ('rad', 'toRadians'): return math.radians(args[0])
        if n in ('toDegrees',): return math.degrees(args[0])
        if n == 'abs': return abs(args[0])
        return None
    if k in ('==', '!=', '<', '>', '<=', '>='):
        a, b = cval(e[1], env), cval(e[2], env)
        if a is None or b is None: return None
        return float({'==': a == b, '!=': a != b, '<': a < b, '>': a > b, '<=': a <= b, '>=': a >= b}[k])
    if k == 'field' and e[1] == ('name', 'this') and e[2] in env: return env[e[2]]
    if k == 'field' and e[2] == 'PI': return math.pi
    return None


class Model:
    def __init__(self, path, args=()):
        self.path = path; self.name = path.stem[5:]; self.args = list(args)
        self.src = path.read_text(errors='replace')
        self.toks = tokens(self.src)
        self.bones = {}      # field name -> dict
        self.order = []
        self.tw, self.th = 64, 32
        self.warn = []
        self.read_ctor()

    def bone(self, n):
        if n not in self.bones:
            self.bones[n] = {'name': n, 'pos': [0.0, 0.0, 0.0], 'rot': [0.0, 0.0, 0.0], 'parent': None, 'cubes': [], 'u': 0, 'v': 0, 'mirror': False, 'shown': True}
            self.order.append(n)
        return self.bones[n]

    def target(self, e):
        """this.body / body -> 'body' if it's a box"""
        if e is None: return None
        if e[0] == 'field' and e[1] == ('name', 'this'): return e[2]
        if e[0] == 'name' and e[1] in self.bones: return e[1]
        return None

    def read_ctor(self):
        pn, body = find_method(self.toks, 'Model' + self.name)
        if body is None: raise ValueError('no constructor')
        st = P(body).block()
        self.ctor_env = {}
        for n, a in zip(pn or [], self.args): self.ctor_env[n] = float(a)
        self.exec_ctor(st)

    def exec_ctor(self, stmts):
        for s in stmts:
            if s[0] == 'block': self.exec_ctor(s[1]); continue
            if s[0] == 'if':
                c = cval(s[1], self.ctor_env)
                self.exec_ctor(s[2] if c is None or c else s[3]); continue
            if s[0] == 'switch':
                c = cval(s[1], self.ctor_env); hit = None
                for val, body in s[2]:
                    if val is not None and c is not None and cval(val, self.ctor_env) == c: hit = body; break
                if hit is None: hit = next((b for v_, b in s[2] if v_ is None), [])
                self.exec_ctor(hit); continue
            if s[0] == 'expr' and s[1][0] == 'assign' and s[1][2][0] == 'field' and s[1][2][1] == ('name', 'this') and s[1][2][2] not in ('texWidth', 'texHeight'):
                v = cval(s[1][3], self.ctor_env)   # (this.type = type: a field the boxes ask about later)
                if v is not None: self.ctor_env[s[1][2][2]] = v
            if s[0] == 'let' and s[2] is not None:
                v = cval(s[2], self.ctor_env)
                if v is not None: self.ctor_env[s[1]] = v
                continue
            if s[0] != 'expr': continue
            e = s[1]
            if e[0] == 'assign':
                _, op, lhs, rhs = e
                if lhs == ('field', ('name', 'this'), 'texWidth') or lhs == ('name', 'texWidth'): self.tw = cval(rhs, self.ctor_env) or self.tw; continue
                if lhs == ('field', ('name', 'this'), 'texHeight') or lhs == ('name', 'texHeight'): self.th = cval(rhs, self.ctor_env) or self.th; continue
                if rhs[0] == 'new' and rhs[1] in ('AdvancedModelBox', 'ModelRenderer', 'BasicModelPart'):
                    n = self.target(lhs)
                    if n is None and lhs[0] == 'field': n = lhs[2]
                    if n is None: continue
                    b = self.bone(n)
                    a = rhs[2]
                    if len(a) >= 3 and cval(a[1]) is not None and cval(a[2]) is not None: b['u'], b['v'] = int(cval(a[1])), int(cval(a[2]))
                    continue
                if lhs[0] == 'field':
                    n = self.target(lhs[1])
                    if n and lhs[2] in ATTR:
                        v = cval(rhs, self.ctor_env)
                        if v is None: continue
                        a = ATTR[lhs[2]]; arr = self.bones[n]['rot'] if a < 3 else self.bones[n]['pos']
                        if op == '=': arr[a % 3] = v
                        elif op == '+=': arr[a % 3] += v
                        elif op == '-=': arr[a % 3] -= v
                    elif n and lhs[2] == 'mirror': self.bones[n]['mirror'] = bool(cval(rhs) or 0)
                    elif n and lhs[2] == 'showModel': self.bones[n]['shown'] = bool(cval(rhs) or 0)
                continue
            if e[0] == 'call': self.ctor_call(e)

    def ctor_call(self, e):
        chain = []
        while e is not None and e[0] == 'call':
            chain.append(e); e = e[1]
        chain.reverse()
        base = self.target(e)
        if base is None:   # this.setRotationAngle(this.body, x, y, z) / this.setRotateAngle(...)
            c = chain[0]
            if c[2] in ('setRotationAngle', 'setRotateAngle', 'setRotation') and c[3]:
                n = self.target(c[3][0])
                if n:
                    vals = [cval(a, self.ctor_env) for a in c[3][1:4]]
                    if all(v is not None for v in vals): self.bones[n]['rot'] = vals
            return
        b = self.bone(base)
        for c in chain:
            n, a = c[2], [cval(x, self.ctor_env) for x in c[3]]
            if n in ('setPos', 'setRotationPoint'):
                if all(v is not None for v in a[:3]): b['pos'] = a[:3]
            elif n == 'setTextureOffset' or n == 'texOffs':
                b['u'], b['v'] = int(a[0] or 0), int(a[1] or 0)
            elif n == 'addChild':
                ch = self.target(c[3][0])
                if ch: self.bone(ch)['parent'] = base
            elif n == 'addBox':
                raw = c[3]
                if raw and raw[0][0] == 'str':   # addBox("name", x, y, z, w, h, d, inflate, u, v)
                    v = [cval(x, self.ctor_env) for x in raw[1:]]
                    if len(v) >= 9: b['u'], b['v'] = int(v[7]), int(v[8])
                    box = dict(xyz=v[0:3], whd=v[3:6], inf=v[6] if len(v) > 6 else 0.0, mirror=b['mirror'])
                else:
                    v = a
                    if any(x is None for x in v[:6]): self.warn.append('addBox?'); continue
                    inf, mir = 0.0, b['mirror']
                    if len(v) == 7:
                        if raw[6] == ('num', 1.0) or raw[6] == ('num', 0.0) and raw[6] != ('num', 0.0): mir = bool(v[6])
                        else: inf = v[6]
                        if raw[6][0] == 'num' and str(raw[6]) in ("('num', 1.0)",) and False: pass
                    elif len(v) == 8: inf, mir = v[6], bool(v[7])
                    elif len(v) >= 9: inf = (v[6] + v[7] + v[8]) / 3.0
                    box = dict(xyz=v[0:3], whd=v[3:6], inf=inf, mirror=mir)
                box['uv'] = (b['u'], b['v'])
                b['cubes'].append(box)
            elif n in ('setRotationAngle', 'setRotateAngle'):
                if all(v is not None for v in a[:3]): b['rot'] = a[:3]
            elif n == 'mirror': pass

    # Bedrock-style geometry (tools/local/boss_import.py voxelises it)
    def geo(self, hide=()):
        absp = {}
        def ap(n):
            if n in absp: return absp[n]
            b = self.bones[n]; p = b['pos']
            if b['parent'] and b['parent'] in self.bones:
                q = ap(b['parent']); r = [q[0] + p[0], q[1] + p[1], q[2] + p[2]]
            else: r = list(p)
            absp[n] = r; return r
        # parents first
        out, seen = [], set()
        def visit(n):
            if n in seen: return
            b = self.bones[n]
            if b['parent'] and b['parent'] in self.bones: visit(b['parent'])
            seen.add(n)
            P_ = ap(n)
            bone = {'name': n, 'pivot': [-P_[0], 24 - P_[1], P_[2]]}
            if b['parent'] and b['parent'] in self.bones: bone['parent'] = b['parent']
            if any(b['rot']): bone['rotation'] = [-math.degrees(b['rot'][0]), -math.degrees(b['rot'][1]), math.degrees(b['rot'][2])]
            cubes = []
            if b['shown'] and n not in hide:
                for c in b['cubes']:
                    (x, y, z), (w, h, d) = c['xyz'], c['whd']
                    cube = {'origin': [-(P_[0] + x + w), 24 - (P_[1] + y + h), P_[2] + z], 'size': [w, h, d], 'uv': list(c['uv'])}
                    if c['inf']: cube['inflate'] = c['inf']
                    if c['mirror']: cube['mirror'] = True
                    cubes.append(cube)
            if cubes: bone['cubes'] = cubes
            out.append(bone)
        for n in self.order: visit(n)
        return out


# ------------------------------------------------------------------ segmented creatures: one model of head, body segments, tail
# The mod makes these of several entities (the head drags its body segments and tail behind it). Here they're one creature:
# each segment hangs behind the one before at its own length, on a joint the chain wave bends (a slither, a crawl).
SEGMENTED = {
    'centipede_head': [('ModelCaveCentipede', [0]), ('ModelCaveCentipede', [1], 4), ('ModelCaveCentipede', [2])],
    'bone_serpent': [('ModelBoneSerpentHead', []), ('ModelBoneSerpentBody', [], 5), ('ModelBoneSerpentTail', [])],
    'void_worm': [('ModelVoidWorm', []), ('ModelVoidWormBody', [], 7), ('ModelVoidWormTail', [])],
}


def java_box(M):
    """the model's extent in Java pixels (absolute, turns ignored): (min xyz, max xyz)"""
    absp = {}
    def ap(n):
        if n in absp: return absp[n]
        b = M.bones[n]; q = ap(b['parent']) if b['parent'] in M.bones else [0.0, 0.0, 0.0]
        absp[n] = [q[i] + b['pos'][i] for i in range(3)]; return absp[n]
    lo, hi = [1e9] * 3, [-1e9] * 3
    for n, b in M.bones.items():
        if not b['cubes']: continue
        a = ap(n)
        for c in b['cubes']:
            for i in range(3):
                lo[i] = min(lo[i], a[i] + c['xyz'][i]); hi[i] = max(hi[i], a[i] + c['xyz'][i] + c['whd'][i])
    return lo, hi, ap


def segmented(key):
    """(the joined Model, [(segment Model, prefix, its pivot bone name)...])"""
    spec = SEGMENTED[key]
    head = Model(AM / 'client/model' / (spec[0][0] + '.java'), spec[0][1])
    hlo, hhi, _ = java_box(head)
    joinY = (hlo[1] + hhi[1]) / 2          # (the joints at the head's middle height)
    back = hhi[2]                          # (the head's back end: the first joint)
    segs = []; prev = None; k = 0
    for entry in spec[1:]:
        cls, args = entry[0], entry[1]; n = entry[2] if len(entry) > 2 else 1
        for _ in range(n):
            k += 1
            S = Model(AM / 'client/model' / (cls + '.java'), args)
            lo, hi, ap = java_box(S)
            piv = 'seg%d' % k; pre = 's%d_' % k
            jb = head.bone(piv)
            if prev is None: jb['pos'] = [0.0, joinY, back]
            else: jb['pos'] = [0.0, 0.0, prev_len]; jb['parent'] = prev
            # the segment's own boxes, its front set at the joint
            for bn in S.order:
                b = S.bones[bn]; nb = head.bone(pre + bn)
                nb.update({k_: (list(v) if isinstance(v, list) else v) for k_, v in b.items() if k_ != 'name'})
                nb['cubes'] = [dict(c) for c in b['cubes']]
                if b['parent'] in S.bones: nb['parent'] = pre + b['parent']
                else:
                    a = ap(bn); nb['parent'] = piv
                    nb['pos'] = [a[0], a[1] - joinY, a[2] - lo[2]]
            segs.append((S, pre, piv))
            prev = piv; prev_len = hi[2] - lo[2]
    return head, segs


def seg_index(x, i):
    """getBodyIndex() / getIndex(): which segment this is"""
    if isinstance(x, list):
        if x[:2] in (['e', 'bodyIndex'], ['e', 'index'], ['e', 'segmentIndex']): return float(i)
        return [seg_index(y, i) for y in x]
    return x


def no_look(x):
    """a segment's program without the head's look (each of the mod's segments has its own pitch, following)"""
    if isinstance(x, list):
        if x[:2] == ['in', 'yaw'] or x[:2] == ['in', 'pitch']: return 0.0
        return [no_look(y) for y in x]
    return x


def shift_f(x, d):
    """the program with limbSwing (and age) a little later down the body: each segment's legs a beat behind"""
    if isinstance(x, list):
        if x[:2] == ['in', 'f'] or x[:2] == ['in', 'age']: return ['+', x, d]
        return [shift_f(y, d) for y in x]
    return x


# ------------------------------------------------------------------ setupAnim / animate -> our program and clips
HELP = {'walk': 'walk', 'swing': 'swing', 'flap': 'flap', 'bob': 'bob', 'chainSwing': 'chainSwing', 'chainWave': 'chainWave',
        'chainFlap': 'chainFlap', 'progressRotationPrev': 'prp', 'progressRotation': 'pr', 'progressPositionPrev': 'ppp',
        'progressPosition': 'pp', 'faceTarget': 'face'}
MATH = {'sin': 'sin', 'cos': 'cos', 'abs': 'abs', 'min': 'min', 'max': 'max', 'sqrt': 'sqrt', 'pow': 'pow', 'signum': 'sign',
        'floor': 'floor', 'ceil': 'ceil', 'atan2': 'atan2', 'atan': 'atan', 'tan': 'tan', 'exp': 'exp', 'log': 'log', 'round': 'floor',
        'm_14031_': 'sin', 'm_14089_': 'cos', 'm_14154_': 'abs', 'm_14036_': 'clamp', 'm_14179_': 'lerp', 'm_14116_': 'sqrt',
        'm_14143_': 'floor', 'm_14167_': 'floor', 'm_14177_': 'wrapdeg', 'm_14189_': 'lerp', 'clamp': 'clamp', 'lerp': 'lerp',
        'rad': 'rad', 'toRadians': 'rad', 'toDegrees': 'deg', 'm_144914_': 'abs', 'm_14008_': 'clamp', 'm_14139_': 'lerp',
        'm_14148_': 'wrapdeg', 'm_14205_': 'sign', 'm_184648_': 'lerp', 'm_14193_': 'sqrt'}
# Minecraft's own (obfuscated) entity methods that the models ask about
ENT_SRG = {'m_19879_': 'id', 'm_20069_': 'isInWater', 'm_20072_': 'isInWater', 'm_20077_': 'isInLava', 'm_20096_': 'onGround', 'm_6162_': 'isBaby',
           'm_5803_': 'isSleeping', 'm_21825_': 'isSitting', 'm_21827_': 'isSitting', 'm_6047_': 'isCrouching', 'm_20142_': 'isSprinting',
           'm_21660_': 'isAggressive', 'm_5912_': 'isAggressive', 'm_20159_': 'isPassenger', 'm_20160_': 'isVehicle', 'm_21224_': 'isDeadOrDying',
           'm_20184_': 'velocity', 'm_6084_': 'isAlive', 'm_21824_': 'isTame', 'm_20202_': 'vehicle', 'm_146909_': 'xRot',
           'm_146908_': 'yRot', 'm_21023_': 'hasEffect', 'm_20186_': 'y', 'm_6688_': 'mainHand', 'm_21205_': 'mainHand'}
ENT_FIELD = {'f_19797_': 'tickCount', 'f_20916_': 'hurtTime', 'f_20919_': 'deathTime', 'f_20883_': 'yBodyRot', 'f_20884_': 'yBodyRotO',
             'f_19861_': 'onGround', 'f_20885_': 'yHeadRot', 'f_20886_': 'yHeadRotO', 'f_19859_': 'yRotO', 'f_19860_': 'xRotO',
             'f_267362_': 'walkAnimation', 'f_20911_': 'swinging', 'f_20921_': 'attackAnim', 'f_20920_': 'oAttackAnim'}


def entkey(n):
    n = ENT_SRG.get(n, ENT_FIELD.get(n, n))
    if n.startswith('prev') and len(n) > 4 and n[4].isupper(): n = n[4].lower() + n[5:]
    if n.startswith('last') and len(n) > 4 and n[4].isupper(): n = n[4].lower() + n[5:]
    if n.startswith('get') and len(n) > 3 and n[3].isupper(): n = n[3].lower() + n[4:]
    return n


class Compiler:
    """the Java AST of setupAnim (and what it calls on the model) -> our program (lists, JSON)"""
    def __init__(self, model, params, ent_class):
        self.m = model; self.bix = {}
        self.slots = {}      # local name -> slot
        self.arrays = {}     # local name -> [bone index...]
        self.consts = {}
        self.ents = set(); self.anims = set()
        self.warn = model.warn
        self.depth = 0
        self.ent_class = ent_class
        names = ['entity', 'f', 'f1', 'age', 'yaw', 'pitch']
        self.inputs = {}
        for i, p in enumerate(params[:6]): self.inputs[p] = names[i]
        self.model_methods = method_names(model.toks)

    def slot(self, n):
        if n not in self.slots: self.slots[n] = len(self.slots)
        return self.slots[n]

    def boneref(self, e):
        n = self.m.target(e)
        if n is None and e[0] == 'name' and e[1] in self.m.bones and e[1] not in self.slots: n = e[1]
        return self.bix.get(n) if n else None

    def bones_of(self, e):
        if e[0] == 'array':
            out = [self.boneref(x) for x in e[1]]; return [x for x in out if x is not None]
        if e[0] == 'name' and e[1] in self.arrays: return self.arrays[e[1]]
        if e[0] == 'field' and e[1] == ('name', 'this') and e[2] in self.arrays: return self.arrays[e[2]]
        b = self.boneref(e)
        return [b] if b is not None else []

    def is_entity(self, e):
        return e is not None and e[0] == 'name' and self.inputs.get(e[1]) == 'entity'

    def ex(self, e):
        k = e[0]
        if k in ('num', 'bool'): return e[1]
        if k == 'str': return 0.0
        if k == 'neg': x = self.ex(e[1]); return -x if isinstance(x, float) else ['neg', x]
        if k == 'not': return ['not', self.ex(e[1])]
        if k == '?': return ['?', self.ex(e[1]), self.ex(e[2]), self.ex(e[3])]
        if k in ('+', '-', '*', '/', '%', '<', '>', '<=', '>=', '==', '!=', '&&', '||', '&', '|', '^'):
            # entity.getAnimation() == EntityX.ANIMATION_Y
            for a, b in ((e[1], e[2]), (e[2], e[1])):
                if k in ('==', '!=') and a[0] == 'call' and a[2] == 'getAnimation' and b[0] == 'field' and b[2].startswith('ANIMATION_'):
                    r = ['anim', b[2][10:].lower()]; self.anims.add(b[2][10:].lower())
                    return r if k == '==' else ['not', r]
            x, y = self.ex(e[1]), self.ex(e[2])
            op = {'&': '&&', '|': '||', '^': '!='}.get(k, k)
            if isinstance(x, float) and isinstance(y, float) and op in '+-*/':
                return x + y if op == '+' else x - y if op == '-' else x * y if op == '*' else (x / y if y else 0.0)
            return [op, x, y]
        if k == 'name':
            n = e[1]
            if n in self.slots: return ['v', self.slots[n]]
            if n in self.inputs:
                i = self.inputs[n]
                return 0.0 if i == 'entity' else ['in', i]
            if n in self.consts: return self.consts[n]
            if n == 'partialTicks' or n == 'partialTick': return 0.0
            self.warn.append('name ' + n); return 0.0
        if k == 'field':
            base, f = e[1], e[2]
            if self.is_entity(base): self.ents.add(entkey(f)); return ['e', entkey(f)]
            if base == ('name', 'this'):
                if f in self.arrays: return 0.0
                if f == 'young': self.ents.add('isBaby'); return ['e', 'isBaby']
                if f in self.consts: return self.consts[f]
                return 0.0
            b = self.boneref(base)
            if b is not None and f in ATTR: return ['b', b, ATTR[f]]
            if b is not None and f in ('defaultRotationX', 'defaultRotationY', 'defaultRotationZ', 'defaultPositionX', 'defaultPositionY', 'defaultPositionZ'):
                return ['d', b, {'defaultRotationX': 0, 'defaultRotationY': 1, 'defaultRotationZ': 2, 'defaultPositionX': 3, 'defaultPositionY': 4, 'defaultPositionZ': 5}[f]]
            if f == 'PI': return math.pi
            if f in ('x', 'y', 'z') and base[0] == 'call' and self.is_entity(base[1]) and base[2] in ('m_20184_', 'getDeltaMovement'):
                self.ents.add('vel' + f); return ['e', 'vel' + f]
            return 0.0
        if k == 'call':
            obj, n, args = e[1], e[2], e[3]
            if self.is_entity(obj):
                if n == 'getAnimationTick': self.ents.add('animTick'); return ['e', 'animTick']
                key = entkey(n)
                # entity.getXProgress(partialTicks) / entity.isFlying()
                self.ents.add(key); return ['e', key]
            if n in MATH and (obj is None or obj[0] == 'name' and obj[1] in ('Math', 'Mth', 'Maths', 'MathHelper', 'StrictMath', 'ACMath') or obj[0] == 'field'):
                xs = [self.ex(a) for a in args]
                f_ = MATH[n]
                if f_ == 'rad' and isinstance(xs[0], float): return math.radians(xs[0])
                if f_ == 'deg' and isinstance(xs[0], float): return math.degrees(xs[0])
                return ['fn', f_] + xs
            if obj is not None and obj[0] == 'call' and obj[2] == 'm_91087_' and n == 'm_91296_': return 0.0   # partial tick
            if obj is not None and obj[0] == 'call' and obj[2] == 'm_91087_' and n == 'm_91297_': return 0.0
            if n in ('getPartialTick', 'getFrameTime', 'm_91296_'): return 0.0
            # an entity value through a cast: ((EntityX)entity).foo()
            self.warn.append('call ' + n); return 0.0
        if k == 'index': return 0.0
        if k == 'assign': return 0.0
        if k == 'array' or k == 'new': return 0.0
        self.warn.append('expr ' + k); return 0.0

    def stmts(self, ss, env=None):
        out = []
        for s in ss:
            out += self.stmt(s)
        return out

    def stmt(self, s):
        k = s[0]
        if k == 'block': return self.stmts(s[1])
        if k == 'skip': self.warn.append('skip ' + s[1]); return []
        if k == 'ret': return [['ret']]
        if k == 'switch':
            out = []; rest = []
            for val, body in reversed(s[2]):
                if val is None: rest = self.stmts(body); continue
                rest = [['if', ['==', self.ex(s[1]), self.ex(val)], self.stmts(body), rest]]
            return rest
        if k == 'if':
            return [['if', self.ex(s[1]), self.stmts(s[2]), self.stmts(s[3])]]
        if k == 'let':
            n, e = s[1], s[2]
            if e is not None and e[0] == 'array':
                self.arrays[n] = self.bones_of(e); return []
            if e is not None and self.boneref(e) is not None:
                self.arrays[n] = [self.boneref(e)]; return []
            if e is None: return [['let', self.slot(n), 0.0]]
            if e[0] == 'name' and self.inputs.get(e[1]) == 'entity': self.inputs[n] = 'entity'; return []   # (EntityX x = (EntityX)entity)
            v = self.ex(e)
            return [['let', self.slot(n), v]]
        if k == 'expr':
            e = s[1]
            if e[0] == 'assign':
                _, op, lhs, rhs = e
                if lhs[0] == 'field':
                    b = self.boneref(lhs[1])
                    if b is not None and lhs[2] in ATTR:
                        return [['set', b, ATTR[lhs[2]], op[0] if op != '=' else '=', self.ex(rhs)]]
                    if b is not None and lhs[2] == 'showModel':
                        return [['show', b, self.ex(rhs)]]
                    if b is not None: return []
                if lhs[0] == 'name' and lhs[1] in self.slots:
                    v = self.ex(rhs); sl = self.slots[lhs[1]]
                    if op == '=': return [['let', sl, v]]
                    return [['let', sl, [op[0], ['v', sl], v]]]
                if lhs[0] == 'name' and lhs[1] not in self.slots and op == '=':
                    return [['let', self.slot(lhs[1]), self.ex(rhs)]]
                return []
            if e[0] == 'call':
                obj, n, args = e[1], e[2], e[3]
                if (obj is None or obj == ('name', 'this')) and n in HELP:
                    return [self.helper(HELP[n], args)]
                b = self.boneref(obj) if obj is not None else None
                if b is not None and n in ('walk', 'swing', 'flap', 'bob'):
                    return [self.helper(n, [obj] + args)]
                if b is not None and n in ('setScale',):
                    return [['scale', b] + [self.ex(a) for a in args[:3]]]
                if b is not None and n in ('resetToDefaultPose',): return [['reset1', b]]
                if b is not None and n in ('setPos', 'setRotationPoint'):
                    return [['set', b, 3 + i, '=', self.ex(a)] for i, a in enumerate(args[:3])]
                if (obj is None or obj == ('name', 'this')) and n in ('setRotateAngle', 'setRotationAngle') and len(args) >= 4:
                    b = self.boneref(args[0])
                    if b is not None: return [['set', b, i, '=', self.ex(a)] for i, a in enumerate(args[1:4])]
                if (obj is None or obj == ('name', 'this')) and n in ('resetToDefaultPose', 'animate'): return []
                if (obj is None or obj == ('name', 'this')) and n in self.model_methods and self.depth < 3:
                    return self.inline(n, args)
                if obj is not None and obj[0] == 'field' and obj[2] == 'animator': return []
                self.warn.append('stmt call ' + n); return []
        return []

    def helper(self, h, args):
        if h in ('walk', 'swing', 'flap'):
            b = self.boneref(args[0])
            if b is None or len(args) < 8: return ['nop']
            return [h, b] + [self.ex(a) for a in args[1:8]]
        if h == 'bob':
            b = self.boneref(args[0])
            if b is None or len(args) < 6: return ['nop']
            return [h, b] + [self.ex(a) for a in args[1:6]]
        if h in ('chainSwing', 'chainWave', 'chainFlap'):
            bs = self.bones_of(args[0])
            if not bs or len(args) < 6: return ['nop']
            return [h, bs] + [self.ex(a) for a in args[1:6]]
        if h in ('prp', 'pr', 'ppp', 'pp'):
            b = self.boneref(args[0])
            if b is None or len(args) < 6: return ['nop']
            return [h, b] + [self.ex(a) for a in args[1:6]]
        if h == 'face':
            bs = []
            for a in args[3:]: bs += self.bones_of(a)
            return ['face', self.ex(args[0]), self.ex(args[1]), self.ex(args[2]), bs]
        return ['nop']

    def inline(self, n, args):
        pn, body = find_method(self.m.toks, n)
        if body is None: return []
        saved = dict(self.inputs); self.depth += 1
        out = []
        for i, p in enumerate(pn or []):
            if i >= len(args): break
            a = args[i]
            if self.is_entity(a): self.inputs[p] = 'entity'; continue
            if self.boneref(a) is not None or a[0] == 'array': self.arrays[p] = self.bones_of(a); continue
            out.append(['let', self.slot(p), self.ex(a)])
        try: out += self.stmts(P(body).block())
        except (SyntaxError, IndexError): self.warn.append('inline ' + n)
        self.inputs = saved; self.depth -= 1
        return out


def clips_of(model, comp):
    """ModelAnimator keyframes in animate(): {name: {'len': s, 'loop': False, 'bones': {idx: [[t, rx, ry, rz, px, py, pz]...]}}} (Java units)"""
    pn, body = find_method(model.toks, 'animate')
    if body is None: return {}
    try: st = P(body).block()
    except (SyntaxError, IndexError): return {}
    flat = []
    def walk_(ss):
        for s in ss:
            if s[0] == 'block': walk_(s[1])
            elif s[0] == 'if': walk_(s[2]); walk_(s[3])
            elif s[0] == 'expr' and s[1][0] == 'call': flat.append(s[1])
    walk_(st)
    clips = {}; cur = None
    frames = []        # [(start, dur, kind, transforms)]
    t = 0; tf = None
    def finish():
        if cur is None or not frames: return
        # the pose at a tick: Citadel's endKeyframe (prev transforms eased out, these eased in, sin)
        total = frames[-1][0] + frames[-1][1]
        bones = sorted({b for f in frames for b in f[3]})
        keys = {b: [] for b in bones}
        prev = {}
        samples = []
        for (s0, d, kind, T) in frames:
            for q in range(4 if kind == 'move' else 1):
                u = q / 4.0
                inc = math.sin(u * math.pi / 2)
                pose = {}
                for b in bones:
                    a = prev.get(b, [0.0] * 6); c = T.get(b, [0.0] * 6) if kind != 'static' else a
                    pose[b] = [a[i] * (1 - inc) + c[i] * inc for i in range(6)]
                samples.append(((s0 + u * d) / 20.0, pose))
            if kind != 'static': prev = T
        samples.append((total / 20.0, {b: prev.get(b, [0.0] * 6) for b in bones}))
        for (ts, pose) in samples:
            for b in bones: keys[b].append([round(ts, 4)] + [round(x, 5) for x in pose[b]])
        clips[cur] = {'len': total / 20.0, 'bones': {str(b): k for b, k in keys.items()}}
    for c in flat:
        n = c[2]; a = c[3]
        if n == 'setAnimation':
            finish()
            f = a[0] if a else None
            cur = f[2][10:].lower() if f and f[0] == 'field' and f[2].startswith('ANIMATION_') else None
            frames = []; t = 0; tf = None
        elif cur is None: continue
        elif n == 'startKeyframe':
            d = cval(a[0]) or 1; tf = {}; frames.append((t, d, 'move', tf)); t += d
        elif n == 'setStaticKeyframe':
            d = cval(a[0]) or 1; frames.append((t, d, 'static', {})); t += d
        elif n == 'resetKeyframe':
            d = cval(a[0]) or 1; frames.append((t, d, 'move', {})); t += d
        elif n in ('rotate', 'move') and tf is not None and len(a) >= 4:
            b = comp.boneref(a[0])
            if b is None: continue
            vals = [cval(x) for x in a[1:4]]
            vals = [v if v is not None else 0.0 for v in vals]
            r = tf.setdefault(b, [0.0] * 6)
            o = 0 if n == 'rotate' else 3
            for i in range(3): r[o + i] += vals[i]
    finish()
    return clips


# ------------------------------------------------------------------ renderers, registry, entities
def renderers():
    out = {}
    for f in (AM / 'client/render').glob('Render*.java'):
        s = f.read_text(errors='replace')
        m = re.search(r'new (Model\w+)\(', s)
        if not m: continue
        tex = re.findall(r'ResourceLocation\("alexsmobs:textures/entity/([\w/]+)\.png"\)', s)
        cls = re.search(r'extends \w+<(\w+), ', s)
        sc = None
        sm = re.search(r'void scale\([^)]*\)\s*\{(.*?)\n    \}', s, re.S)
        if sm:
            x = re.search(r'm_85841_\(([\d.]+)f?, ([\d.]+)f?, ([\d.]+)f?\)', sm.group(1))
            if x: sc = float(x.group(1))
        out.setdefault(m.group(1), []).append({'file': f.name, 'tex': tex, 'entity': cls.group(1) if cls else None, 'scale': sc, 'src': s})
    return out


def registry():
    s = (AM / 'entity/AMEntityRegistry.java').read_text()
    out = {}
    for m in re.finditer(r'register\("(\w+)", \(\) -> AMEntityRegistry\.registerEntity\(EntityType\.Builder\.m_20704_\((\w+)::new, \(MobCategory\)MobCategory\.(\w+)\)\.m_20699_\(([\d.]+)f, ([\d.]+)f\)(.*?)"\w+"\)\)', s):
        out[m.group(2)] = {'key': m.group(1), 'cat': m.group(3), 'w': float(m.group(4)), 'h': float(m.group(5)), 'fireImmune': 'm_20719_' in m.group(6)}
    return out


ATTRS = {'f_22276_': 'hp', 'f_22279_': 'speed', 'f_22281_': 'damage', 'f_22278_': 'knock', 'f_22280_': 'flySpeed', 'f_22284_': 'armor'}


def entity_info(cls):
    f = AM / 'entity' / (cls + '.java')
    if not f.exists(): return {}
    s = f.read_text(errors='replace')
    info = {'src': s}
    m = re.search(r'bakeAttributes\(\)\s*\{(.*?)\n    \}', s, re.S)
    if m:
        for a, v in re.findall(r'Attributes\.(f_\d+_), (?:\(double\))?([\d.]+)', m.group(1)):
            if a in ATTRS: info[ATTRS[a]] = float(v)
    ext = re.search(r'public class \w+\s+extends (\w+)', s)
    info['extends'] = ext.group(1) if ext else ''
    info['progress'] = {}
    for n, mx in re.findall(r'this\.(\w+Progress)\s*<\s*([\d.]+)f?\)', s): info['progress'][n] = max(info['progress'].get(n, 0), float(mx))
    tg = re.findall(r'f_21346_\.m_25352_\(\d+, (?:\(Goal\))?new (\w+)\(([^;]*)', s)
    info['targets'] = tg
    gg = re.findall(r'f_21345_\.m_25352_\(\d+, (?:\(Goal\))?new (\w+)\(([^;]*)', s)
    info['goals'] = gg
    return info


# our creatures each of the mod's prey classes stands for
PREY = {'Chicken': 'chicken', 'Rabbit': 'rabbit', 'Sheep': 'sheep', 'Pig': 'pig', 'Cow': 'cow', 'AbstractFish': 'cod', 'Cod': 'cod',
        'Salmon': 'salmon', 'Squid': 'squid', 'Turtle': 'turtle', 'Fox': 'fox', 'Fox.class': 'fox', 'Goat': 'goat', 'Llama': 'llama',
        'EntityMouse': 'mouse', 'EntityGazelle': 'gazelle', 'EntityRoadrunner': 'roadrunner', 'EntityCrow': 'crow', 'Horse': 'horse',
        'EntityCockroach': 'cockroach', 'EntityFly': 'fly', 'Frog': 'frog', 'Parrot': 'parrot', 'Bee': 'bee', 'EntityLobster': 'lobster',
        'EntityCrayfish': 'crayfish', 'Dolphin': 'dolphin', 'TropicalFish': 'cod', 'Pufferfish': 'cod', 'EntityBlobfish': 'blobfish'}
SOUNDS = {'bear': 'bear', 'cat': 'bigcat', 'tiger': 'bigcat', 'leopard': 'bigcat', 'bird': 'parrot', 'crow': 'crow', 'snake': 'snake', 'fish': 'cod'}


def behaviour(key, reg_e, info, model_flags):
    d = {}
    hp = info.get('hp', 10.0)
    d['hp'] = round(hp, 1)
    d['w'] = round(reg_e['w'] / 2, 3); d['h'] = round(reg_e['h'], 3)
    sp = info.get('speed', 0.25)
    d['speed'] = round(max(0.4, min(4.0, sp * 5.5)), 2)   # (Minecraft's attribute 0.25 = a cow's walk, ours ~1.4)
    if info.get('damage'): d['damage'] = round(info['damage'], 1)
    if info.get('knock'): d['knockResist'] = round(info['knock'], 2)
    if reg_e.get('fireImmune'): d['fireImmune'] = True
    cat = reg_e['cat']; src = info.get('src', '')
    targets = ' '.join(t[0] + '(' + t[1] for t in info.get('targets', []))
    hostile_player = cat == 'MONSTER' or re.search(r'NearestAttackableTargetGoal[^)]*Player\.class', targets) is not None
    retaliate = 'HurtByTargetGoal' in targets
    prey = []
    for t, a in info.get('targets', []):
        if 'Player' in a: continue
        for c in re.findall(r'(\w+)\.class', a):
            if c in PREY and PREY[c] not in prey and PREY[c] != key: prey.append(PREY[c])
    water = cat in ('WATER_CREATURE', 'WATER_AMBIENT', 'UNDERGROUND_WATER_CREATURE')
    semi = 'ISemiAquatic' in src or 'AmphibiousPathNavigation' in src or 'SemiAquaticPathNavigator' in src
    flies = re.search(r'FlyingMoveControl|FlightMoveController|FlyingPathNavigation|implements[^{]*FlyingAnimal|FlyingAnimal', src) is not None
    d['xp'] = 1 if cat in ('AMBIENT', 'WATER_AMBIENT') else 3 if not hostile_player else 5
    if hostile_player and cat == 'MONSTER': d['hostile'] = True
    elif water: d['ai'] = 'swimmer'; d['swims'] = True; d['water'] = True   # (the sea's own, whether or not they come up the shore)
    elif flies and key not in ('warped_toad', 'leafcutter_ant'): d['ai'] = 'flyer'; d['fly'] = round(max(2.0, info.get('flySpeed', 0.6) * 6), 2); d['perches'] = True
    elif semi: d['ai'] = 'amphibian'; d['swims'] = True; d['walks'] = True; d['swimSpeed'] = round(d['speed'] * 1.6, 2)
    elif prey and info.get('damage'): d['ai'] = 'predator'; d['prey'] = prey; d['run'] = round(d['speed'] * 3, 2)
    elif hostile_player and info.get('damage'): d['ai'] = 'neutral'; d['angersWhenHurt'] = True
    elif retaliate and info.get('damage'): d['ai'] = 'neutral'
    else: d['ai'] = 'passive'; d['run'] = round(d['speed'] * 3, 2)
    if hostile_player and cat != 'MONSTER': d['retaliates'] = True
    if cat in ('AMBIENT',): d['ambient'] = True
    if 'PanicGoal' in ' '.join(g[0] for g in info.get('goals', [])) and d.get('ai') == 'passive': d['skittish'] = True
    for k, v in SOUNDS.items():
        if k in key: d['sound'] = v
    return d


def loot(key, items):
    f = ASSETS.parent.parent / 'data/alexsmobs/loot_tables/entities' / (key + '.json')
    if not f.exists(): return []
    out = []
    try: j = json.loads(f.read_text())
    except ValueError: return []
    for pool in j.get('pools', []):
        for e in pool.get('entries', []):
            n = e.get('name', '')
            if not n.startswith('minecraft:'): continue
            it = n.split(':', 1)[1]
            if it not in items: continue
            lo, hi = 0, 1
            for fn in e.get('functions', []):
                c = fn.get('count')
                if isinstance(c, dict): lo, hi = int(c.get('min', 0)), int(c.get('max', 1))
                elif isinstance(c, (int, float)): lo = hi = int(c)
            out.append([it, lo, max(lo, hi), 1])
    return out


# where the mod brings these about by other means (the farseer at the world's edge after dark, the sea bear in cold seas,
# the cosmaw over the End's islands); the void worm and the warped mosco are bosses the mod summons, the leafcutter ants
# come from their anthills: none of those three spawn of themselves
EXTRA_SPAWN = {'farseer': {'spawnDim': [['night', 1, 1, 1, 'g']]}, 'sea_bear': {'spawnInWater': [['cold', 2, 1, 1]]},
               'cosmaw': {'spawnDim': [['end', 3, 1, 1, 'a']]}}
CFG_NAME = {'centipede_head': 'cave_centipede'}
NETHER = r'nether|soul_sand|crimson|warped|basalt|is_nether'
END = r'the_end|:end_|is_end|end_highlands|end_midlands|end_barrens|small_end|chorus'
SEA = r'ocean|:sea|lukewarm|reef'
CAVE = r'cave|deep_dark|dripstone|lush|underground|is_cave'


_DEFAULTS = None


def default_biomes():
    """the mod's own spawn lands (config/DefaultBiomes.java): {name: [[{value, negate}...] group...]}"""
    global _DEFAULTS
    if _DEFAULTS is not None: return _DEFAULTS
    src = (AM / 'config/DefaultBiomes.java').read_text()
    consts = {}
    for m in re.finditer(r'SpawnBiomeData (\w+) = new SpawnBiomeData\(\)(.*?);\n', src):
        groups = {}
        for t, neg, val, g in re.findall(r'addBiomeEntry\(BiomeEntryType\.(\w+), (true|false), "([^"]+)", (\d+)\)', m.group(2)):
            groups.setdefault(int(g), []).append({'value': val, 'negate': neg == 'true'})
        consts[m.group(1)] = [groups[k] for k in sorted(groups)]
    out = {}
    for f in (AM / 'config').glob('*.java'):
        for n, c in re.findall(r'"alexsmobs:(\w+?)(?:_spawns)?", \(Object\)DefaultBiomes\.(\w+)\)', f.read_text()):
            if c in consts: out[n] = consts[c]
    _DEFAULTS = out
    return out


def spawns(key, mob, info):
    """Soulrend's config (alexsmobs.toml weight, config/alexsmobs/<name>_spawns.json biome groups) -> our places:
    spawnIn [[land, w, lo, hi]] (the Overworld's lands, by day and night), spawnInWater [[ocean...]], spawnDim
    [[nether|end|cave|night, w, lo, hi, where]] (where: g ground, a air, w water, l lava). A monster of the Overworld's
    lands comes out at night, as ours do."""
    try: import soulrend_spawns as SS
    except ImportError: return
    name = CFG_NAME.get(key, key)
    toml = CFG / 'alexsmobs.toml'
    f = CFG / 'alexsmobs' / (name + '_spawns.json')
    if not f.exists(): f = CFG / 'alexsmobs' / (name + '.json')
    if not toml.exists(): return
    groups = json.loads(f.read_text()).get('biomes', []) if f.exists() else []
    if not groups: groups = default_biomes().get(name, [])   # (switched off in Soulrend: the mod's own lands for it)
    camel = re.sub(r'_(\w)', lambda m: m.group(1).upper(), name)
    m = re.search(r'\b' + camel + r'SpawnWeight\s*=\s*(\d+)', toml.read_text())
    if not m or int(m.group(1)) <= 0: return
    w = max(1, min(12, int(m.group(1)) // 2))
    lands, places = set(), set()
    for grp in groups:
        pos = [c['value'] for c in grp if not c.get('negate')]
        if any(re.search(SEA, v) for v in pos): places.add('sea'); continue   # (an ocean group: its other tags only narrow the sea)
        if any(re.search(r'river', v) for v in pos): places.add('river'); continue
        if any(re.search(r'mushroom', v) for v in pos): lands.add('forest'); w_m = True; continue
        for v in pos:
            if v == 'minecraft:is_overworld':
                if len(pos) == 1: lands |= set(SS.ALL)
                continue
            ns = v.split(':')[0]
            if ns in ('minecraft', 'forge', 'byg', 'alexsmobs') and re.search(NETHER, v): places.add('nether'); continue
            if ns in ('minecraft', 'forge', 'alexsmobs') and re.search(END, v): places.add('end'); continue
            if re.search(CAVE, v):
                if v.split(':')[0] in ('minecraft', 'forge', 'alexsmobs'): places.add('cave')
                continue
            if re.search(SEA, v): places.add('sea'); continue
            l = SS.lands_of(v)
            if l: lands |= l
    hm = re.search(r'\b' + camel + r'SpawnHeight\s*=\s*(-?\d+)', toml.read_text())
    if key == 'skreecher': places.add('cave')
    deep = key in ('underminer', 'skreecher') or (hm is not None and int(hm.group(1)) < 40 and not mob.get('water'))
    if deep and lands: lands = set(); places.add('cave')
    src = info.get('src', '')
    where = 'a' if mob.get('ai') == 'flyer' else 'w' if mob.get('water') else 'g'
    if mob.get('fireImmune') and ('m_20077_' in src or 'isInLava' in src) and re.search(r'LavaPathNavigation|lava', src, re.I) and key in ('bone_serpent', 'stradpole'):
        where = 'l'
    hostile = mob.get('hostile')
    dims = []
    for pl in ('nether', 'end', 'cave'):
        if pl in places: dims.append([pl, w, 1, 2, where])
    if lands:
        if hostile: dims.append(['night', w, 1, 2, where])
        elif not mob.get('water'): mob['spawnIn'] = [[l, w, 1, 2] for l in sorted(lands)]
        else: places.add('sea')
    if 'river' in places:
        if mob.get('water') or mob.get('swims'): mob.setdefault('spawnInWater', []).append(['lake', w, 1, 2])
        else: mob['spawnIn'] = mob.get('spawnIn', []) + [['swamp', w, 1, 2], ['forest', max(1, w // 2), 1, 2]]
    if 'sea' in places:
        if mob.get('water') or mob.get('swims'): mob['spawnInWater'] = mob.get('spawnInWater', []) + [['ocean', w, 1, 3], ['warm', w, 1, 3]]
        elif not lands: mob['spawnIn'] = [['beach', w, 1, 2]]
    if dims: mob['spawnDim'] = dims


# ------------------------------------------------------------------ the ents: what each entity value is in our game
STATES = [(r'sleep', 'sleeping'), (r'sit|rest|lay|lie|laying|curl|tuck', 'sitting'), (r'fly|flight|glide|soar|hover|air', 'flying'),
          (r'swim|water|dive|submerg|float', 'swimming'), (r'run|sprint|charg|gallop|dash', 'running'),
          (r'attack|bite|strike|claw|punch|stomp|slam|maul|swipe|roar|rage|anger|angry|aggress|hiss|rattle|threat', 'attacking'),
          (r'eat|graze|peck|dig|sniff|forag|drink', 'grazing'), (r'stand|rear|beg', 'rearing'), (r'baby|young|child', 'baby'),
          (r'ground', 'onGround'), (r'tame', 'never'), (r'dance|danc', 'never')]


def ent_spec(name, info):
    """[kind, ...]: ["prog", max, state] a value ramping 0..max while the state holds; ["flag", state]; ["tick"]; ["anim"]; ["vel", axis]"""
    if name == 'animTick': return ['anim']
    if name in ('tickCount',): return ['tick']
    if name in ('velx', 'vely', 'velz'): return ['vel', 'xyz'.index(name[3])]
    if name in ('hurtTime',): return ['hurt']
    if name == 'id': return ['id']
    low = name.lower()
    st = 'never'
    for rx, s in STATES:
        if re.search(rx, low): st = s; break
    if 'progress' in low or name in info.get('progress', {}):
        mx = info.get('progress', {}).get(name, 5.0)
        return ['prog', mx, st]
    if low.startswith('is') or low.startswith('has') or low.startswith('can') or low in ('onground',):
        return ['flag', st]
    return ['prog', 5.0, st] if st != 'never' else ['zero']


BAD_TEX = r'moss|overlay|pattern|_eyes|glow|saddle|sheared|baby|snowy|armor|armour|collar|harness|chest|bandana|crown|_hurt|_angry|_sleep|ghost|_shot|_item|_rider|_charge|_glint|_emissive|_light'


def pick_tex(key, listed):
    """the creature's plain coat: its own name first; not a layer (moss, pattern, eyes...), a baby's or a season's"""
    base = ASSETS / 'textures/entity'
    cands = list(listed)
    for f in list(base.glob(key + '*.png')) + list(base.glob(key + '/*.png')):
        cands.append(str(f.relative_to(base))[:-4])
    cands = [c for c in dict.fromkeys(cands) if (base / (c + '.png')).exists()]
    def score(c):
        n = c.split('/')[-1]
        sc = 0
        if n == key: sc += 100
        elif n.startswith(key): sc += 40
        if re.search(BAD_TEX, n): sc -= 200
        if c in listed: sc += 10
        return sc
    return max(cands, key=score) if cands else (listed[0] if listed else key)


LIMB = r'leg|arm|thigh|limb'
NOT_LIMB = r'foot|feet|lower|shin|calf|hand|paw|toe|knee|claw|fin|wing|tentacle|ear|antenna|horn|hair|fur|tuft|feather|pad'


def joint_limb(b, bones):
    """a limb worth a joint: named a leg / arm, its longest box at least 6 pixels long and longer than it's thick,
    hanging down from its pivot, with no knee of its own already (a lower leg / shin / forearm under it)"""
    n = b['name']
    if not re.search(LIMB, n, re.I) or re.search(NOT_LIMB, n, re.I): return False
    cs = [c for c in b.get('cubes', []) if 'rotation' not in c]
    if not cs: return False
    c0 = max(cs, key=lambda c: c['size'][1])
    w, h, d = c0['size']
    if h < 6 or h < 1.15 * max(w, d): return False
    if c0['origin'][1] + h > b['pivot'][1] + 1.5: return False   # (it must hang below its pivot)
    for o in bones:
        if o.get('parent') == n and re.search(r'lower|shin|calf|fore(arm)?|knee|leg2|arm2|bottom', o['name'], re.I): return False
    return True


def set_cells(p, vox, pv):
    """a part's voxels (model space) into its cells, min, max (x mirrored to ours, about its pivot pv)"""
    import numpy as np
    if not vox:
        p['min'] = [0, 0, 0]; p['max'] = [0.001, 0.001, 0.001]; p['vox'] = []; return 0
    ks = np.array(list(vox.keys())); lo = ks.min(0); hi = ks.max(0) + 1
    cells = [[int(-(x + 1) - (-hi[0])), int(y - lo[1]), int(z - lo[2]), 1, 1, 1, rgb] + ([1] if lit else []) for (x, y, z), (rgb, lit) in vox.items()]
    n = [int(hi[0] - lo[0]), int(hi[1] - lo[1]), int(hi[2] - lo[2])]
    mn = [(-hi[0] + pv[0]) / 16, (lo[1] - pv[1]) / 16, (lo[2] - pv[2]) / 16]
    p['min'] = [round(v, 5) for v in mn]
    p['max'] = [round(mn[0] + n[0] / 16, 5), round(mn[1] + n[1] / 16, 5), round(mn[2] + n[2] / 16, 5)]
    p['vox'] = {'n': n, 'c': cells}; p['res'] = 16
    return len(cells)


def tint_vox(vox, t):
    """a part's voxel colours through a variant's tint: hue turn (degrees), sat / val multipliers, colVal (the coloured
    ones only), mix [r, g, b, amount] (render/creature_models.cpp tintImg)"""
    import colorsys
    if not vox or not isinstance(t, dict): return vox
    hue, sat, val, colVal = t.get('hue', 0.0), t.get('sat', 1.0), t.get('val', 1.0), t.get('colVal', 1.0)
    mix = t.get('mix') if isinstance(t.get('mix'), list) and len(t.get('mix')) >= 4 else None
    cells = []
    for c in vox['c']:
        rgb = c[6]; r, g, b = ((rgb >> 16) & 255) / 255, ((rgb >> 8) & 255) / 255, (rgb & 255) / 255
        h, s0, v = colorsys.rgb_to_hsv(r, g, b)
        h = ((h * 360 + hue) % 360) / 360; s = min(1.0, max(0.0, s0 * sat)); v = min(1.0, max(0.0, v * val * (1 + (colVal - 1) * min(1.0, s0 * 1.6))))
        r, g, b = colorsys.hsv_to_rgb(h, s, v)
        if mix and mix[3] > 0: r += (mix[0] - r) * mix[3]; g += (mix[1] - g) * mix[3]; b += (mix[2] - b) * mix[3]
        n = c[:6] + [(int(round(r * 255)) << 16) | (int(round(g * 255)) << 8) | int(round(b * 255))] + c[7:]
        cells.append(n)
    return {'n': vox['n'], 'c': cells}


def clip_role(name):
    n = name.lower()
    if re.search(r'attack|bite|swipe|claw|strike|punch|slap|peck|sting|maul|slash|hit|kick|stomp|smash|headbutt|tail|pounce|scratch|lunge|slam|grab|snap|spit|ram|shock', n): return 'attack'
    if re.search(r'eat|graze|dig|sniff|drink|forage|chew|nibble|groom|wash', n): return 'eat'
    return 'idle'


def main(d='data', only=None):
    d = pathlib.Path(d); od = d / 'local_models'; od.mkdir(parents=True, exist_ok=True)
    (od / '.gitignore').write_text('*\n!.gitignore\n')
    ours = json.loads((d / 'mobs.json').read_text()); ours = ours.get('mobs', ours)
    items = json.loads((d / 'items.json').read_text()); items = items.get('items', items)
    rends = renderers(); regs = registry()
    our_models = json.loads((d / 'models.json').read_text()); our_models = our_models.get('models', our_models) if isinstance(our_models.get('models'), dict) else our_models
    out = {'models': {}, 'clips': {}, 'mobs': {}, 'voiceOf': {v: k for k, v in ALIAS.items()}}
    report = []
    for mf in sorted((AM / 'client/model').glob('Model*.java')):
        if 'extends AdvancedEntityModel' not in mf.read_text(errors='replace'): continue
        mname = mf.stem
        rs = rends.get(mname)
        if not rs: continue
        rs = [x for x in rs if regs.get(x['entity']) and regs[x['entity']]['key'] not in SKIP and x['tex']]
        if not rs: continue
        r = rs[0]
        ent = r['entity']
        reg_e = regs[ent]
        key = reg_e['key']
        if only and key not in only: continue
        if mname.endswith('Baby') or not r['tex']: continue
        ourkey = ALIAS.get(key, key)
        segs = []
        try:
            if key in SEGMENTED: M, segs = segmented(key)
            else: M = Model(mf)
        except (ValueError, SyntaxError, IndexError, FileNotFoundError) as ex: report.append('%s: model unreadable (%s)' % (key, ex)); continue
        # the picture: the renderer's first (its plain coat)
        tex = pick_tex(key, r['tex'])
        png = ASSETS / 'textures/entity' / (tex + '.png')
        if not png.exists(): report.append('%s: no picture %s' % (key, tex)); continue
        glow = None
        for gname in (tex + '_eyes', tex + '_glow', key + '_eyes', key + '_glow'):
            g = ASSETS / 'textures/entity' / (gname + '.png')
            if g.exists(): glow = g; break
        img = Image.open(png).convert('RGBA')
        gimg = Image.open(glow).convert('RGBA') if glow else None
        bones = M.geo()
        parts = []; absp = {b['name']: b['pivot'] for b in bones}; nvox = 0; knees = []
        new_kind = ourkey not in ours
        import numpy as np
        reparent = {}   # (a foot or claws hanging below a new knee: carried by it)
        for b in bones:
            if b['name'] in reparent: b['parent'] = reparent[b['name']]
            pv = np.array(absp[b['name']], float); par = b.get('parent')
            rel = pv - np.array(absp[par], float) if par in absp else pv
            p = {'name': b['name'], 'pivot': [round(-rel[0] / 16, 5), round(rel[1] / 16, 5), round(rel[2] / 16, 5)]}
            if par: p['parent'] = par
            if 'rotation' in b: rot = b['rotation']; p['rest'] = [round(math.radians(rot[0]), 5), round(-math.radians(rot[1]), 5), round(-math.radians(rot[2]), 5)]   # (Java's turns, y mirrored: -x, y, -z)
            vox = G.bone_voxels(b, img, gimg, M.tw, M.th, java=True)
            # a knee (an elbow) for the creatures new to us: a limb cut in two at its middle, the lower half its own part
            # hung at the joint (the very voxels it had: the picture stays as it was), folded by the swing above it
            knee = None
            if new_kind and vox and joint_limb(b, bones):
                ys = [c['origin'][1] for c in b['cubes']] + [c['origin'][1] + c['size'][1] for c in b['cubes']]
                sy = (min(ys) + max(ys)) / 2.0
                c0 = max(b['cubes'], key=lambda c: c['size'][1])
                kx = c0['origin'][0] + c0['size'][0] / 2.0; kz = c0['origin'][2] + c0['size'][2] / 2.0
                lower = {k_: v_ for k_, v_ in vox.items() if k_[1] < sy}
                if lower and len(lower) < len(vox):
                    vox = {k_: v_ for k_, v_ in vox.items() if k_[1] >= sy}
                    knee = (b['name'] + '_knee', np.array([kx, sy, kz]), lower)
            nvox += set_cells(p, vox, pv)
            # the Java pose of the box (its pivot relative to its parent's, its turn), for the program's resets and reads
            jb = M.bones[b['name']]
            p['jpos'] = [round(v, 4) for v in jb['pos']]; p['jrot'] = [round(v, 5) for v in jb['rot']]
            if b.get('_kneeJ') is not None: p['jpos'] = [round(p['jpos'][i] - b['_kneeJ'][i], 4) for i in range(3)]
            parts.append(p)
            if knee:
                kn, kp, kvox = knee
                rel = kp - pv
                q = {'name': kn, 'parent': b['name'], 'pivot': [round(-rel[0] / 16, 5), round(rel[1] / 16, 5), round(rel[2] / 16, 5)]}
                nvox += set_cells(q, kvox, kp)
                kj = [-rel[0], -rel[1], rel[2]]
                q['jpos'] = [round(v, 4) for v in kj]; q['jrot'] = [0.0, 0.0, 0.0]
                parts.append(q); absp[kn] = list(kp); knees.append((b['name'], kn))
                for o in bones:
                    if o.get('parent') == b['name'] and o['pivot'][1] < sy:
                        reparent[o['name']] = kn; o['_kneeJ'] = kj
        idx = {p_['name']: i for i, p_ in enumerate(parts)}
        # the program
        params, body = find_method(M.toks, 'setupAnim')
        info = entity_info(ent)
        C = Compiler(M, params or [], ent)
        C.bix = idx
        prog = []
        if body is not None:
            try: prog = C.stmts(P(body).block())
            except (SyntaxError, IndexError) as ex: M.warn.append('setupAnim unparsed: %s' % ex)
        # the segments: each its own program (its legs), a beat later than the one before; the chain bends behind the head
        for i, (S, pre, piv) in enumerate(segs):
            sp_, sb_ = find_method(S.toks, 'setupAnim')
            if sb_ is None: continue
            SC = Compiler(S, sp_ or [], ent)
            SC.bix = {bn: idx[pre + bn] for bn in S.bones if pre + bn in idx}
            SC.consts = dict(S.ctor_env)
            base_slot = len(C.slots)
            try: body_prog = SC.stmts(P(sb_).block())
            except (SyntaxError, IndexError): body_prog = []
            # (its locals after the head's)
            def reslot(x):
                if isinstance(x, list):
                    if len(x) == 2 and x[0] == 'v' and isinstance(x[1], int): return ['v', x[1] + base_slot]
                    if len(x) >= 2 and x[0] == 'let' and isinstance(x[1], int): return ['let', x[1] + base_slot] + [reslot(y) for y in x[2:]]
                    return [reslot(y) for y in x]
                return x
            body_prog = shift_f(seg_index(no_look(reslot(body_prog)), i), -0.9 * (i + 1))
            for n_, sl in SC.slots.items(): C.slots[n_ + '#%d' % i] = sl + base_slot
            C.ents |= SC.ents
            prog += [['scope', body_prog]]   # (a return in it ends only its own part)
        if segs:
            chain = [idx[piv] for _, _, piv in segs]
            prog += [['chainSwing', chain, 0.55, 0.32, -3.0, ['in', 'f'], ['in', 'f1']],
                     ['chainSwing', chain, 0.06, 0.1, -3.0, ['in', 'age'], 1.0],
                     ['chainWave', chain, 0.06, 0.03, -3.0, ['in', 'age'], 1.0]]
        # the knees fold as the limb above them swings forward (Story Mode's joints), and the body breathes
        for leg, kn in knees:
            arm = re.search(r'arm|hand|fore(?!leg)', leg, re.I) is not None and not re.search(r'front|leg', leg, re.I)
            prog.append(['knee', idx[kn], idx[leg], 0.9 if arm else 1.25, -1.0 if arm else 1.0])
        if new_kind and not segs:
            body = next((idx[n] for n in idx if re.fullmatch(r'(body|torso|chest|main_body|bodyMain|body_main|middle|trunk)', n, re.I)), None)
            if body is not None: prog.append(['bob', body, 0.09, 0.22, 0.0, ['in', 'age'], 1.0])
        clips = clips_of(M, C)
        ents = {n: ent_spec(n, info) for n in sorted(C.ents)}
        scale = r['scale'] or 1.0
        model = {'rig': 'beast', 'rotOrder': 'zyx', 'scale': scale, 'hipY': 12, 'parts': parts,
                 'beast': {'prog': prog, 'slots': len(C.slots), 'ents': ents, 'clipRole': {c: clip_role(c) for c in clips}}}
        out['models'][ourkey] = model
        if clips: out['clips'][ourkey] = clips
        # variants we already know by name (tiger_white...): the same body, their own picture
        if ourkey in ours:
            for vd in ours[ourkey].get('variantDefs', []) + [{'model': v} for v in ours[ourkey].get('variants', [])]:
                vk = vd.get('model')
                if not vk or vk == ourkey: continue
                suffix = vk[len(ourkey) + 1:] if vk.startswith(ourkey + '_') else None
                vt = None
                for t in r['tex']:
                    if suffix and t.endswith(suffix): vt = t
                if not vt:
                    # no picture of it in the mod: the mod's body in our look's colours (its packTint, as render/creature_models.cpp tints)
                    tint = our_models.get(vk, {}).get('packTint')
                    if isinstance(tint, str):
                        try: tint = json.loads(tint.replace("'", '"'))
                        except ValueError: tint = None
                    out['models'][vk] = dict(model, parts=[dict(p0, vox=tint_vox(p0['vox'], tint)) for p0 in parts])
                    if clips: out['clips'][vk] = clips
                    continue
                vimg = Image.open(ASSETS / 'textures/entity' / (vt + '.png')).convert('RGBA')
                vparts = []
                for b, p0 in zip(bones, parts):
                    p = dict(p0)
                    vox = G.bone_voxels(b, vimg, gimg, M.tw, M.th, java=True)
                    if vox and p0['vox']:
                        ks = np.array(list(vox.keys())); lo = ks.min(0); hi = ks.max(0) + 1
                        p['vox'] = {'n': p0['vox']['n'], 'c': [[int(-(x + 1) - (-hi[0])), int(y - lo[1]), int(z - lo[2]), 1, 1, 1, rgb] + ([1] if lit else []) for (x, y, z), (rgb, lit) in vox.items()]}
                    vparts.append(p)
                out['models'][vk] = dict(model, parts=vparts)
                if clips: out['clips'][vk] = clips
        if ourkey not in ours:
            mob = behaviour(key, reg_e, info, {})
            mob['model'] = ourkey
            mob['label'] = 'a ' + key.replace('_', ' ') if key[0] not in 'aeiou' else 'an ' + key.replace('_', ' ')
            mob['name'] = key.replace('_', ' ').title()
            drops = loot(key, items)
            if drops: mob['drops'] = drops
            else: mob['drops'] = []
            spawns(key, mob, info)
            if key in EXTRA_SPAWN and not any(a in mob for a in ('spawnIn', 'spawnInWater', 'spawnDim')): mob.update(EXTRA_SPAWN[key])
            if key == 'sunbird': mob['perches'] = False; mob['soar'] = 22   # (it never lands: it soars high, its wings wide)
            mob['imported'] = True
            out['mobs'][ourkey] = mob
        warn = sorted(set(M.warn))
        report.append('%s%s: %d parts, %d voxels, %d statements, %d clips, %d inputs%s' % (key, ' (ours: %s)' % ourkey if ourkey in ours else '', len(parts), nvox, len(prog), len(clips), len(ents), ('; skipped: ' + ', '.join(warn[:8]) + (' +%d' % (len(warn) - 8) if len(warn) > 8 else '')) if warn else ''))
    (od / 'beasts.json').write_text(json.dumps(out, separators=(',', ':')))
    (od / 'alexsmobs_report.txt').write_text('\n'.join(report) + '\n')
    print('alexsmobs_import: %d models (%d new creatures), %d with clips -> %s' % (len(out['models']), len(out['mobs']), len(out['clips']), od / 'alexsmobs.json'))


if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    only = None
    for x in sys.argv[1:]:
        if x.startswith('--only='): only = set(x[7:].split(','))
    main(a[0] if a else 'data', only)
