# rewrite every string (and compound key) of an NBT file through fn, every tag and number kept byte for byte
import gzip, struct
def transform(data, fn):
    gz = data[:2] == b'\x1f\x8b'
    raw = gzip.decompress(data) if gz else data
    out = bytearray(); pos = 0
    def rd(n):
        nonlocal pos; b = raw[pos:pos + n]; pos += n; return b
    def string():
        n = struct.unpack('>H', rd(2))[0]; s = rd(n).decode('utf-8', 'surrogatepass')
        t = fn(s).encode('utf-8', 'surrogatepass'); out.extend(struct.pack('>H', len(t))); out.extend(t)
    def payload(t):
        if t == 1: out.extend(rd(1))
        elif t == 2: out.extend(rd(2))
        elif t in (3, 5): out.extend(rd(4))
        elif t in (4, 6): out.extend(rd(8))
        elif t == 7: b = rd(4); out.extend(b); out.extend(rd(struct.unpack('>i', b)[0]))
        elif t == 8: string()
        elif t == 9:
            et = rd(1); out.extend(et); b = rd(4); out.extend(b)
            for _ in range(struct.unpack('>i', b)[0]): payload(et[0])
        elif t == 10:
            while True:
                tt = rd(1); out.extend(tt)
                if tt[0] == 0: break
                string(); payload(tt[0])
        elif t == 11: b = rd(4); out.extend(b); out.extend(rd(4 * struct.unpack('>i', b)[0]))
        elif t == 12: b = rd(4); out.extend(b); out.extend(rd(8 * struct.unpack('>i', b)[0]))
        else: raise ValueError('tag %d' % t)
    t = rd(1); out.extend(t)
    if t[0]: string(); payload(t[0])
    out.extend(raw[pos:])
    res = bytes(out)
    return (gzip.compress(res, mtime=0) if gz else res), res != raw
