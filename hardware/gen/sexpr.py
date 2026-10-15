"""Minimal KiCad S-expression helpers (parsing symbol libraries and footprints)."""
import re

KICAD = r"C:\Program Files\KiCad\10.0\share\kicad"


def tokenize(text):
    return re.findall(r'\(|\)|"(?:\\.|[^"\\])*"|[^\s()"]+', text)


def parse(text):
    toks = tokenize(text)
    pos = 0

    def node():
        nonlocal pos
        if toks[pos] == "(":
            pos += 1
            lst = []
            while toks[pos] != ")":
                lst.append(node())
            pos += 1
            return lst
        t = toks[pos]
        pos += 1
        if t.startswith('"'):
            return t[1:-1].replace('\\"', '"')
        return t

    return node()


def find_all(tree, name):
    for x in tree:
        if isinstance(x, list) and x and x[0] == name:
            yield x


def symbol_pins(lib, sym):
    """Return {pin_number: pin_name} for a symbol of a .kicad_sym library."""
    text = open(f"{KICAD}\\symbols\\{lib}.kicad_sym", encoding="utf-8").read()
    start = text.index(f'(symbol "{sym}"')
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    tree = parse(text[start:end])
    pins = {}

    def walk(n):
        for x in n:
            if isinstance(x, list):
                if x and x[0] == "pin":
                    name = number = None
                    for y in x:
                        if isinstance(y, list) and y and y[0] == "name":
                            name = y[1]
                        if isinstance(y, list) and y and y[0] == "number":
                            number = y[1]
                    pins[number] = name
                else:
                    walk(x)

    walk(tree)
    return pins


def footprint_pads(lib, fp):
    text = open(f"{KICAD}\\footprints\\{lib}.pretty\\{fp}.kicad_mod", encoding="utf-8").read()
    tree = parse(text)
    pads = {}
    for p in find_all(tree, "pad"):
        pads.setdefault(p[1], []).append(p[2])
    return pads


if __name__ == "__main__":
    import sys
    lib, sym = sys.argv[1], sys.argv[2]
    pins = symbol_pins(lib, sym)
    for k in sorted(pins, key=lambda s: (len(s), s)):
        print(f"{k:>4}  {pins[k]}")
