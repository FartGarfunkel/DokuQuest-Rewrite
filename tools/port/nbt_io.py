#!/usr/bin/env python3
"""tools/port/nbt_io.py — read and write Minecraft's NBT (gzipped structure templates and the like), no dependencies.

  read(path_or_bytes) -> python values: dict for compounds, list for lists, int/float/str, bytes/list for arrays
  write(path, value, root_name='') writes a gzipped file. Python values are typed this way: dict -> compound,
  list -> list (its element type from the first element; an empty list is a list of ends), str -> string,
  bool/int -> int (TAG_Int) unless wrapped: Byte(n), Short(n), Long(n); float -> double unless Float(x);
  IntArray([...]), ByteArray(b'..'), LongArray([...]).
"""
import gzip, io, struct

END, BYTE, SHORT, INT, LONG, FLOAT, DOUBLE, BYTE_ARRAY, STRING, LIST, COMPOUND, INT_ARRAY, LONG_ARRAY = range(13)


class Byte(int): pass
class Short(int): pass
class Long(int): pass
class Float(float): pass
class IntArray(list): pass
class LongArray(list): pass
class ByteArray(bytes): pass


def _read(f):
    r = f.read
    def s(): n = struct.unpack('>H', r(2))[0]; return r(n).decode('utf-8', 'replace')
    def payload(t):
        if t == BYTE: return struct.unpack('>b', r(1))[0]
        if t == SHORT: return struct.unpack('>h', r(2))[0]
        if t == INT: return struct.unpack('>i', r(4))[0]
        if t == LONG: return struct.unpack('>q', r(8))[0]
        if t == FLOAT: return struct.unpack('>f', r(4))[0]
        if t == DOUBLE: return struct.unpack('>d', r(8))[0]
        if t == BYTE_ARRAY: n = struct.unpack('>i', r(4))[0]; return r(n)
        if t == STRING: return s()
        if t == LIST:
            et = r(1)[0]; n = struct.unpack('>i', r(4))[0]
            return [payload(et) for _ in range(n)]
        if t == COMPOUND:
            out = {}
            while True:
                tt = r(1)[0]
                if tt == END: return out
                k = s(); out[k] = payload(tt)
        if t == INT_ARRAY: n = struct.unpack('>i', r(4))[0]; return list(struct.unpack('>%di' % n, r(4 * n)))
        if t == LONG_ARRAY: n = struct.unpack('>i', r(4))[0]; return list(struct.unpack('>%dq' % n, r(8 * n)))
        raise ValueError('bad tag %d' % t)
    t = r(1)[0]; s()
    return payload(t)


def read(src):
    data = src if isinstance(src, (bytes, bytearray)) else open(src, 'rb').read()
    if data[:2] == b'\x1f\x8b': data = gzip.decompress(data)
    return _read(io.BytesIO(data))


def _tag(v):
    if isinstance(v, bool): return BYTE
    if isinstance(v, Byte): return BYTE
    if isinstance(v, Short): return SHORT
    if isinstance(v, Long): return LONG
    if isinstance(v, int): return INT
    if isinstance(v, Float): return FLOAT
    if isinstance(v, float): return DOUBLE
    if isinstance(v, ByteArray): return BYTE_ARRAY
    if isinstance(v, str): return STRING
    if isinstance(v, IntArray): return INT_ARRAY
    if isinstance(v, LongArray): return LONG_ARRAY
    if isinstance(v, list): return LIST
    if isinstance(v, dict): return COMPOUND
    raise TypeError(type(v))


def _write(w, t, v):
    def s(x): b = x.encode('utf-8'); w(struct.pack('>H', len(b))); w(b)
    if t == BYTE: w(struct.pack('>b', int(v)))
    elif t == SHORT: w(struct.pack('>h', v))
    elif t == INT: w(struct.pack('>i', v))
    elif t == LONG: w(struct.pack('>q', v))
    elif t == FLOAT: w(struct.pack('>f', v))
    elif t == DOUBLE: w(struct.pack('>d', v))
    elif t == BYTE_ARRAY: w(struct.pack('>i', len(v))); w(bytes(v))
    elif t == STRING: s(v)
    elif t == LIST:
        et = _tag(v[0]) if v else END
        w(bytes([et])); w(struct.pack('>i', len(v)))
        for x in v: _write(w, et, x)
    elif t == COMPOUND:
        for k, x in v.items(): tt = _tag(x); w(bytes([tt])); s(k); _write(w, tt, x)
        w(bytes([END]))
    elif t == INT_ARRAY: w(struct.pack('>i', len(v))); w(struct.pack('>%di' % len(v), *v))
    elif t == LONG_ARRAY: w(struct.pack('>i', len(v))); w(struct.pack('>%dq' % len(v), *v))


def dumps(value, root_name=''):
    buf = io.BytesIO(); w = buf.write
    w(bytes([COMPOUND])); b = root_name.encode('utf-8'); w(struct.pack('>H', len(b))); w(b)
    _write(w, COMPOUND, value)
    return gzip.compress(buf.getvalue(), mtime=0)   # (mtime 0: the same bytes every run)


def write(path, value, root_name=''):
    with open(path, 'wb') as f: f.write(dumps(value, root_name))
