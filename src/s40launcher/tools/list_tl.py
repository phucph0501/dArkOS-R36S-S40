"""List every string passed to tl("...") in the given sources (adjacent literals joined), unique,
in order of first appearance. With an existing ui.tsv, report which strings still lack a translation.
Usage: python3 list_tl.py <out.txt> <ui.tsv or -> files...   (one string per line, \\n escaped)"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wrap_tr import scan  # noqa: E402


def esc(s):
    return s.replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t")


def unesc_c(s):
    """C escapes inside a literal -> the runtime text."""
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            c = s[i + 1]
            out.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(c, "\\" + c))
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def tl_strings(code):
    lits = list(scan(code))
    k, res = 0, []
    while k < len(lits):
        s, e, t = lits[k]
        texts, m, end = [t], k + 1, e
        while m < len(lits) and code[end:lits[m][0]].strip() == "":
            texts.append(lits[m][2])
            end = lits[m][1]
            m += 1
        if code[max(0, s - 3):s] == "tl(":
            res.append(unesc_c("".join(texts)))
        k = m
    return res


def main():
    out, table, files = sys.argv[1], sys.argv[2], sys.argv[3:]
    seen = []
    for f in files:
        for s in tl_strings(open(f, encoding="utf-8").read()):
            if s not in seen:
                seen.append(s)
    with open(out, "w", encoding="utf-8", newline="\n") as o:
        for s in seen:
            o.write(esc(s) + "\n")
    print(len(seen), "strings")
    if table != "-" and os.path.exists(table):
        have = set()
        for line in open(table, encoding="utf-8"):
            if line.strip() and not line.startswith("#"):
                have.add(line.rstrip("\n").split("\t")[0])
        missing = [s for s in seen if esc(s) not in have]
        print(len(missing), "without translation")
        for s in missing[:40]:
            print("  ", esc(s))


if __name__ == "__main__":
    main()
