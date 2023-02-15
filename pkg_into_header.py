"""
Reads src code and packages into single header file
"""

from io import TextIOWrapper


DEST_HEADER = "ws_ctube.h"

file_list = [
    "ws_ctube.c",
]

system_include = set()


def system_include_str() -> str:
    result = ""
    for hfile in system_include:
        result += f"#include <{hfile}>\n"
    return result


def pkg_from(src: TextIOWrapper) -> str:
    out_code = ""

    lines = src.readlines()
    for line in lines:
        if line.startswith("#include <"):
            system_hdr = line.split("#include <")[1].split(">")[0]
            system_include.add(system_hdr)
        elif line.startswith("#include \""):
            pass
        else:
            out_code += line

    return out_code

def main():
    code = ""
    for fname in file_list:
        with open(fname, "r") as f:
            code += pkg_from(f)

    with open(DEST_HEADER, "w") as hfile:
        hfile.write(system_include_str())
        hfile.write(code)
