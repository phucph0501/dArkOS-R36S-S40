"""Wrap user-visible C++ string literals with tr(...) for the UI translation table (s40tr.h).
A literal (or a run of adjacent literals) is wrapped when it contains non-ASCII letters (Vietnamese
text) and does not look like a shell command or path. Idempotent: literals already inside tr( are
left alone. Usage: python3 wrap_tr.py file.cpp [file2 ...]   (rewrites the files in place)"""
import re
import sys
import unicodedata

SHELL = re.compile(r"2>|/dev/null|\bsudo\b|\| |\$\(|&&|/usr/|/opt/|/etc/|/home/|awk |grep |\bdf -|printf ")


def scan(code):
    """Yield (start, end, text) of every ordinary string literal, skipping comments/char/raw literals."""
    i, n = 0, len(code)
    while i < n:
        c = code[i]
        if code.startswith("//", i):
            j = code.find("\n", i)
            i = n if j < 0 else j
        elif code.startswith("/*", i):
            j = code.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c == "'":
            j = i + 1
            while j < n and code[j] != "'":
                j += 2 if code[j] == "\\" else 1
            i = j + 1
        elif code.startswith('R"', i) and (i == 0 or not (code[i - 1].isalnum() or code[i - 1] == "_")):
            d = code.index("(", i)
            delim = ")" + code[i + 2:d] + '"'
            i = code.index(delim, d) + len(delim)
        elif c == '"' and code[i - 1:i] != "\\":
            if code.rfind("#include", code.rfind("\n", 0, i) + 1, i) >= 0:
                i = code.find("\n", i)
                continue
            j = i + 1
            while j < n and code[j] != '"':
                j += 2 if code[j] == "\\" else 1
            yield i, j + 1, code[i + 1:j]
            i = j + 1
        else:
            i += 1


def eligible(text):
    if SHELL.search(text):
        return False
    return any(ord(ch) > 127 and unicodedata.category(ch).startswith("L") for ch in text)


def wrap(code):
    lits = list(scan(code))
    out, last, k = [], 0, 0
    while k < len(lits):
        s, e, t = lits[k]
        group_end, texts, m = e, [t], k + 1
        while m < len(lits) and code[group_end:lits[m][0]].strip() == "":  # adjacent literals
            group_end = lits[m][1]
            texts.append(lits[m][2])
            m += 1
        before = code[max(0, s - 3):s]
        if any(eligible(x) for x in texts) and not before.endswith("tl("):
            out.append(code[last:s])
            out.append("tl(" + code[s:group_end] + ")")
            last = group_end
        k = m
    out.append(code[last:])
    return "".join(out)


if __name__ == "__main__":
    for f in sys.argv[1:]:
        src = open(f, encoding="utf-8").read()
        new = wrap(src)
        if new != src:
            open(f, "w", encoding="utf-8", newline="\n").write(new)
            print(f, src.count('"') - new.count('"'), "->", new.count("tl(") - src.count("tl("), "wrapped")
