# Copyright (c) 2023 Bryance Oyang
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""
Run from ../pkg.sh

Reads src code from src/ and packages into single header file ws_ctube.h
"""

from io import TextIOWrapper
import re

DEST_HEADER = "../ws_ctube.h"

file_list = [
    "likely.h",
    "container_of.h",

    "ref_count.h",
    "list.h",

    "crypt.h",
    "socket.h",
    "ws_base.h",
    "ws_ctube_struct.h",

    "crypt.c",
    "ws_base.c",
    "ws_ctube.c",
]

system_include = []
system_include_set = set()

def system_include_str() -> str:
    result = ""
    for hfile in system_include:
        result += f"#include <{hfile}>\n"
    return result

def rm_top_comment(code: str):
    return re.sub(r"^/\*.*?\*/", "", code, flags=re.DOTALL, count=1)

def pkg_from(src: TextIOWrapper) -> str:
    out_code = ""

    lines = src.readlines()
    for line in lines:
        if line.startswith("#include <"):
            # put #include <...> into set
            system_hdr = line.split("#include <")[1].split(">")[0]
            if system_hdr not in system_include_set:
                system_include_set.add(system_hdr)
                system_include.append(system_hdr)
        elif line.startswith("#include \""):
            # remove #include "...""
            pass
        else:
            # remove // comments
            out_code += line.split("//")[0]

    # remove first comment
    out_code = rm_top_comment(out_code)

    # remove doxygen
    out_code = re.sub(r"/\*\*(.*?)(@file)(.*?)\*/", "", out_code, flags=re.DOTALL, count=1)

    return out_code

def main():
    # main include
    with open("ws_ctube_api.h", "r") as f:
        ws_ctube_h = f.read()

    # all other files
    code = ""
    for fname in file_list:
        with open(fname, "r") as f:
            code += pkg_from(f)

    with open(DEST_HEADER, "w") as hfile:
        hfile.write(
"""/*
 * Copyright (c) 2023 Bryance Oyang
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * === websocket_ctube ===
 *
 * View README.md for usage info and documentation.
 *
 * The contents of this file are copied from ./src by ./pkg.sh in the
 * websocket_ctube distribution.
 */

""")
        hfile.write("#ifndef WS_CTUBE_H\n#define WS_CTUBE_H\n")
        hfile.write("#ifdef __cplusplus\nextern \"C\" {\n#endif /* __cplusplus */\n\n")

        hfile.write(rm_top_comment(ws_ctube_h))
        hfile.write(system_include_str())
        hfile.write(code)

        hfile.write("\n\n#ifdef __cplusplus\n} /* extern \"C\" */\n#endif /* __cplusplus */\n")
        hfile.write("\n\n#endif /* WS_CTUBE_H */\n")

main()
