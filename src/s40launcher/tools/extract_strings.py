"""Extract the user-visible strings of the launcher (C++ literals containing non-ASCII text) into
assets/lang/strings.tsv (one Vietnamese source string per line, with the files using it).
Usage: python3 extract_strings.py <src dir> <out.tsv>"""
import glob
import os
import sys


def literals(code):
    """C++ string literals (handles comments, char literals, escapes; joins adjacent literals)."""
    i, n, out = 0, len(code), []
    while i < n:
        c = code[i]
        if code.startswith("//", i):
            i = code.find("\n", i)
            i = n if i < 0 else i
        elif code.startswith("/*", i):
            i = code.find("*/", i + 2)
            i = n if i < 0 else i + 2
        elif c == "'":
            j = i + 1
            while j < n and code[j] != "'":
                j += 2 if code[j] == "\\" else 1
            i = j + 1
        elif code.startswith('R"', i) and (i == 0 or not (code[i - 1].isalnum() or code[i - 1] == "_")):
            d = code.index("(", i)
            delim = ")" + code[i + 2:d] + '"'
            e = code.index(delim, d)
            out.append(code[d + 1:e])
            i = e + len(delim)
        elif c == '"':
            j, buf = i + 1, []
            while j < n and code[j] != '"':
                if code[j] == "\\" and j + 1 < n:
                    esc = code[j + 1]
                    buf.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(esc, "\\" + esc))
                    j += 2
                else:
                    buf.append(code[j])
                    j += 1
            out.append("".join(buf))
            i = j + 1
        else:
            i += 1
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    seen = {}
    for f in sorted(glob.glob(os.path.join(src, "*.cpp"))):
        for s in literals(open(f, encoding="utf-8").read()):
            if any(ord(ch) > 127 for ch in s) and s.strip():
                seen.setdefault(s, set()).add(os.path.basename(f))
    with open(dst, "w", encoding="utf-8", newline="\n") as o:
        for s in sorted(seen):
            o.write(s.replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t") + "\t" + ",".join(sorted(seen[s])) + "\n")
    print(len(seen), "strings")


if __name__ == "__main__":
    main()
