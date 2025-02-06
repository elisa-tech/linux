#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2025 Red Hat, Inc. Alessandro Carminati <acarmina@redhat.com>
#
# Process a given file and adds or checks SPDX-Req-ID identifiers
# in the instance of the comment before the function.
"""
idgen.py – Generate and check SPDX-Req-ID values in source files

**SYNOPSIS**
```
idgen.py <generate|check> <filename.c|filename.h> [-debug]
```

**DESCRIPTION**
`idgen.py` is a script designed to manage `SPDX-Req-ID` values in source
files.
It provides two primary functions:
* `generate`: Searches for unpopulated `SPDX-Req-ID` fields and assigns a
   new ID where necessary.
    * Returns `0` on success.
    * Returns `1` on failure.
* `check`: Parses the specified file to verify existing `SPDX-Req-ID` values.
    * Reports the line numbers where IDs are found.
    * Indicates whether an ID is unchanged, has been modified, or is missing.
    * Returns `2` if any `SPDX-Req-ID` is missing; `1` if any file error,
      otherwise, returns `0`.

**OPTIONS**
`-debug` Enables debug mode, displaying all processing steps and the data
used for ID generation.

**EXAMPLES**
Checking a file for missing or modified IDs
```
$ idgen.py check source.c
The ID at line 120 is missing
The ID at line 345 is OK but has changed
The ID at line 678 is OK
2
```
Generating missing IDs
```
$ idgen.py generate source.c
0
```
"""
import re
import sys
import hashlib
from typing import List, Tuple


def delete_line(lines: List[str], id_regex: str) -> List[str]:
    """
    deletes the line where the id_regex is found.

    :lines: instance lines
    :id_regex: regex to match

    :return: the instance without the line containing id_regex
    """
    res = []
    for line in lines:
        if not re.search(id_regex, line):
            res.append(line)

    return res


def write_lines_to_file(filename: str, lines: List[str]) -> bool:
    """
    writes lines to the specified filename

    :lines: lines to write
    :filename: file name where write lines

    :return: True if successful, else otherwise.
    """
    try:
        with open(filename, 'w') as file:
            for line in lines:
                if not line.endswith('\n'):
                    line += '\n'
                file.write(line)

        return True

    except Exception:
        return False


def get_source_lines(filename: str) -> Tuple[List[str], bool]:
    """
    Read a file and return a list of string

    :filename: file to process

    :return: a tuple:
             - list of the lines
             - True if read ok False otherwise
    """
    try:
        with open(filename, 'r') as file:
            return [line.rstrip('\n') for line in file], True
    except Exception:
        return [], False


def sha256sum(input: str) -> str:
    """
    Calculate the SHA-256 hash of an input string.

    :input: The string to be hashed.

    :return: A hash hexadecimal representation.
    """
    data = input.encode('utf-8')
    sha256_hash = hashlib.sha256(data).hexdigest()
    return sha256_hash


def extract_instance(source: List[str], block: Tuple[int, int]) -> List[str]:
    """
    Slice the code to the specified block.

    :source: The code as list of strings.
    :block: A tuple containing two integers representing the start and end.

    :return: A new list containing the sliced source.

    :raises: ValueError if block[0] or block[1] is out of range for source.
    """
    if not all(0 <= i < len(source) for i in [block[0], block[1]]):
        raise ValueError("Indices are out of range.")

    if block[0] > block[1]:
        raise ValueError("Start index cannot be greater than end index.")

    return source[block[0]:block[1]]


def list_to_string(strings: List[str]) -> str:
    """
    Concatenate all strings in the input list with newline characters.

    :strings: A list of strings to be concatenated.

    :return: A single string containing all input strings.
    """

    return "\n".join(strings) + "\n"


def find_in_block_comments(
    pattern: str,
    lines: List[str]
) -> List[Tuple[int, int]]:
    """
    Search for a regex pattern only within block comments in a C source file.

    :pattern: Regular expression to search for.
    :lines: List of strings representing the C source file.

    :return: List of tuples (start_line, end_line) where the pattern is found.
    """
    in_comment_block = False
    comment_content = ""
    comment_start = 0
    matches = []

    for line_num, line in enumerate(lines, start=1):
        line = line.split('//', 1)[0]

        start_idx = line.find('/*')
        end_idx = line.find('*/')

        if in_comment_block:
            if end_idx != -1:
                comment_content += " " + line[:end_idx]
                in_comment_block = False
                if re.search(pattern, comment_content):
                    matches.append((comment_start - 1, line_num))
                comment_content = ""
            else:
                comment_content += " " + line

        elif start_idx != -1:
            in_comment_block = True
            comment_content = line[start_idx+2:]
            comment_start = line_num
            if end_idx != -1 and end_idx > start_idx:
                in_comment_block = False
                comment_content = line[start_idx+2:end_idx]
                if re.search(pattern, comment_content):
                    matches.append((line_num, line_num))
                comment_content = ""

    return matches


def replace_id(
    lines: List[str],
    comment_block: Tuple[int, int],
    pattern: str,
    id: str
) -> List[str]:
    """
    Insert id in the instance.

    :lines: List of strings representing the C source file.
    :comment_block: Tuple indicating (start_line, end_line) of
                    the comment block.
    :pattern: Regular expression to search for.
    :id: id to insert after the regex match.

    :return: Modified list of lines.
    """
    start_line, end_line = comment_block
    modified_lines = lines[:]

    for i in range(start_line - 1, end_line):
        match = re.search(pattern, modified_lines[i])
        if match:
            modified_lines[i] = modified_lines[i][:match.end()] + id

    return modified_lines


def get_id(
    lines: List[str],
    comment_block: Tuple[int, int],
    pattern: str
) -> Tuple[str, int]:
    """
    get id in the instance.

    :lines: List of strings representing the C source file.
    :comment_block: Tuple indicating (start_line, end_line) of
                    the comment block.
    :pattern: Regular expression to search for.
    :id: id to insert after the regex match.

    :return: a tuple coposed by
             - id currently stored if exists
             - the line number where pattern is found
    """
    start_line, end_line = comment_block
    pos = 0

    for i in range(start_line - 1, end_line):
        if re.search(pattern, lines[i]):
            pos = i + 1
            match = re.search(r"[ \t]([0-9a-f]{64})[ \t]*$", lines[i])
            if match:
                return match.group(1), pos

    return "", pos


def extract_function(lines: List[str], start_line: int) -> List[str]:
    """
    Extracts the C function after the start_line.

    :lines: List of strings representing the C source file.
    :start_line: line number where start the search

    :return: the function source code as string list.
    """
    extracted = []
    in_block_comment = False
    brace_count = 0
    function_started = False

    i = start_line
    while i < len(lines):
        line = lines[i]
        extracted.append(line)
        stripped_line = line

        if "/*" in line:
            in_block_comment = True
            stripped_line = line.split("/*", 1)[0]
        if "*/" in line:
            in_block_comment = False
            stripped_line = line.split("*/", 1)[1]

        if in_block_comment:
            i += 1
            continue

        if "//" in stripped_line:
            stripped_line = stripped_line.split("//")[0]

        if not function_started and "{" in stripped_line:
            function_started = True

        if function_started:
            brace_count += stripped_line.count("{") - stripped_line.count("}")

            if brace_count == 0:
                break

        i += 1

    return extracted


# main
help_msg = (
    F"Usage: python {sys.argv[0]}" +
    "<generate|check> <filename.c|filename.h> [-debug]\n"
)
debug = False
generate = False
ret_code = 0

if len(sys.argv) != 4 and len(sys.argv) != 3:
    sys.stderr.write(help_msg)
    sys.exit(1)

if sys.argv[1] == "generate":
    generate = True
elif sys.argv[1] == "check":
    generate = False
else:
    print(help_msg, file=sys.stderr)
farg = 2
if len(sys.argv) == 4:
    debug = True
    if sys.argv[2] == "-debug":
        farg = 3
    elif sys.argv[3] != "-debug":
        print(help_msg, file=sys.stderr)
        sys.exit(1)

id_key_regex = "[ \t]SPDX-Req-ID:"
project = "linux"
filename = sys.argv[farg]

file_content, state = get_source_lines(filename)
if not state:
    print(f"Can't read '{filename}'", file=sys.stderr)
    sys.exit(1)

res = find_in_block_comments(id_key_regex, file_content)

for i in res:
    # TODO: since sidecar is not yet defined, this script doesn't consider the
    #       sidecar added content to the instance.
    instance = list_to_string(
        delete_line(
            extract_instance(file_content, i),
            id_key_regex
        )
    )
    code = list_to_string(extract_function(file_content, i[1]))
    tohash = project + filename + instance + code
    id = sha256sum(tohash)
    current, linenum = get_id(file_content, i, id_key_regex)

    if generate:
        if current == "":
            file_content = replace_id(file_content, i, id_key_regex, " " + id)
    else:
        if current == "":
            print(f"The id at {linenum} is missing")
            ret_code = 2
        elif current != id:
            print(f"The id at {linenum} is OK but changes are made")
        else:
            print(f"The id at {linenum} is OK")

    if debug:
        print(f"DEBUG: /----------------------------------------------------/")
        print(f"DEBUG: PROJECT = '{project}'", file=sys.stderr)
        print(f"DEBUG: filename = '{filename}'", file=sys.stderr)
        print(f"DEBUG: instance_pos = '{i}'", file=sys.stderr)
        print(f"DEBUG: instance = '{instance}'", file=sys.stderr)
        print(f"DEBUG: code = '{code}'", file=sys.stderr)
        print(f"DEBUG: hashed_text = '{tohash}'", file=sys.stderr)
        print(f"DEBUG: id = '{id}'", file=sys.stderr)

if generate:
    if debug:
        print("\n".join(file_content))
    if not write_lines_to_file(filename, file_content):
        print(f"Can't write '{filename}'", file=sys.stderr)
        sys.exit(1)

sys.exit(ret_code)
