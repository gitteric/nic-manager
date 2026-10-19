"""Builds res/oui.bin, the MAC vendor table embedded in NicManager.exe.

Input is the IEEE MA-L registry in its text form (oui.txt), e.g. from
https://standards-oui.ieee.org/oui/oui.txt or the copy shipped in the netaddr
Python package (netaddr/eui/oui.txt).

    python tools/make-oui.py path/to/oui.txt res/oui.bin

Format (little endian): "OUI1", u32 entries, u32 names, then per entry the
3-byte OUI (sorted ascending) and a u16 name index, then the names as
NUL-terminated UTF-8 strings in index order.
"""
import re
import struct
import sys

SUFFIXES = re.compile(
    r"[ ,.]*\b(inc|incorporated|corp|corporation|co|company|ltd|limited|llc|l\.l\.c|gmbh|ag|s\.?a|s\.?p\.?a|"
    r"b\.?v|n\.?v|oy|ab|as|a/s|kg|plc|pty|pte|sas|srl|s\.?r\.?l|k\.?k|kabushiki kaisha|technology|technologies|"
    r"electronics|communications?|international|group|holdings?|systems?)\b\.?$",
    re.IGNORECASE,
)
MAX_LEN = 24


def short_name(name):
    name = " ".join(name.split())
    for _ in range(4):  # "Foo Technology Co., Ltd." -> "Foo"
        trimmed = SUFFIXES.sub("", name).strip(" ,.-&")
        if not trimmed or trimmed == name:
            break
        name = trimmed
    return name[:MAX_LEN].rstrip()


def main(src, dst):
    entries = {}
    with open(src, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"^([0-9A-F]{6})\s+\(base 16\)\s+(.*\S)?", line)
            if m and m.group(2):
                entries[int(m.group(1), 16)] = short_name(m.group(2))
    names = sorted(set(entries.values()))
    index = {n: i for i, n in enumerate(names)}
    blob = b"".join(n.encode("utf-8") + b"\0" for n in names)
    out = bytearray(b"OUI1")
    out += struct.pack("<II", len(entries), len(names))
    for oui in sorted(entries):
        out += oui.to_bytes(3, "big") + struct.pack("<H", index[entries[oui]])
    out += blob
    with open(dst, "wb") as f:
        f.write(out)
    print(f"{len(entries)} prefixes, {len(names)} names, {len(out)} bytes")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
