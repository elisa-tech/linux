#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2025 Red Hat, Inc. Alessandro Carminati <acarmina@redhat.com>
#
# Scans a given directory tree and adds missing SPDX-Req-ID identifiers

import re
import sys
import hashlib
import os

ID_ERR_PATTERN = r"SPDX-Req-ID:[ \t]*([^ \t].*)$"
ID_PATTERN = r"SPDX-Req-ID:[ \t]*([0-9a-f]{8})\.([0-9]{3})[ \t]*$"
ID_NEW_PATTERN = r"SPDX-Req-ID:[ \t]*$"
COMMENT_START_PATTERN = r"^[ \t]*/\*"
COMMENT_END_PATTERN = r"\*\/"
max_id_map = {}


def md5_prefix(filename: str) -> str:
    return hashlib.md5(filename.encode()).hexdigest()[:8]


def get_source_files(directory: str):
    source_files = []
    for root, _, files in os.walk(directory):
        for file in files:
            if file.endswith(('.c', '.h')):
                source_files.append(os.path.join(root, file))
    return source_files


def preprocess(files):
    global max_id_map
    ret = []

    for filepath in files:
        with open(filepath, 'r') as f:
            content = f.read()

        in_comment = False
        for line in content.splitlines():
            if re.search(COMMENT_START_PATTERN, line):
                in_comment = True
            elif re.search(COMMENT_END_PATTERN, line):
                in_comment = False

            if in_comment:
                id_match = re.search(ID_PATTERN, line)
                if id_match:
                    file_prefix, progressive_str = id_match.groups()
                    max_id_map.setdefault(file_prefix, 0)
                    max_id_map[file_prefix] = max(max_id_map[file_prefix], int(progressive_str))
                    continue
                wid_match = re.search(ID_ERR_PATTERN, line)
                if wid_match:
                    wrong_id = wid_match.groups()
                    sys.stderr.write(f"{filepath}: '{wrong_id[0]}' is not a valid ID\n")
                if re.search(ID_NEW_PATTERN, line):
                    ret.append(filepath)

    return ret


def process_comment(lines, md5):
    global max_id_map
    updated_block = []

    for line in lines:
        modified_line = line
        if re.search(ID_NEW_PATTERN, line):
            next_max_id = max_id_map.get(md5,0) + 1
            new_id = f"SPDX-Req-ID: {md5}.{next_max_id:03}"
            modified_line = re.sub(ID_NEW_PATTERN, new_id, line)
            max_id_map[md5] = next_max_id

        updated_block.append(modified_line)

    return updated_block


def process_file(filepath: str):
    md5 = md5_prefix(filepath)
    file_lines = []
    in_comment = False
    need_write = False
    comment_lines = []

    with open(filepath, 'r') as file:
        content = file.read()

    for line in content.splitlines():

        if in_comment:
            in_comment = in_comment + 1
            comment_lines.append(line)
        elif re.search(COMMENT_START_PATTERN, line):
            in_comment = True
            file_lines.extend(process_comment(comment_lines, md5))
            comment_lines = [line]
        else:
            file_lines.append(line)
        if in_comment and re.search(COMMENT_END_PATTERN, line):
            new_lines = process_comment(comment_lines, md5)
            if set(new_lines) != set(comment_lines):
                need_write = True
            file_lines.extend(new_lines)
            in_comment = False
            comment_lines = []

    if need_write:
        with open(filepath, 'w') as file:
            print(f"{filepath}: Updated.")
            file.write('\n'.join(file_lines) + '\n')


if len(sys.argv) != 2:
    sys.stderr.write(f"Usage: {sys.argv[0]} <directory>\n")
    sys.exit(1)

directory = sys.argv[1]

if not os.path.isdir(directory):
    sys.stderr.write("Error: Input must be a valid directory.\n")
    sys.exit(1)

source_files = get_source_files(directory)

flist = preprocess(source_files)

for filepath in flist:
    process_file(filepath)
