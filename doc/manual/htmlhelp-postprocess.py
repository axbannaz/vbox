#!/usr/bin/python3
# -*- coding: utf-8 -*-
# $Id$

"""
A python script to create a .qhp file out of a given htmlhelp
folder. Lots of things about the said folder is assumed. Please
see the code and inlined comments.
"""

__copyright__ = \
"""
Copyright (C) 2006-2024 Oracle and/or its affiliates.

This file is part of VirtualBox base platform packages, as
available from https://www.virtualbox.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, in version 3 of the
License.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <https://www.gnu.org/licenses>.

SPDX-License-Identifier: GPL-3.0-only
"""

import getopt
import logging
import os.path
import glob
import sys
import re

if sys.version_info[0] >= 3:
    from html.parser import HTMLParser
else:
    from HTMLParser import HTMLParser

# number of opened and not yet closed section tags of toc section
open_section_tags = 0

html_files = []

def css_in_parent(css_name, current_dir):
    css_path = os.path.normpath(os.path.join(current_dir, css_name))
    if os.path.isfile(css_path):
        return css_path
    parent_dir = os.path.dirname(current_dir)
    if parent_dir == current_dir:
        return ""
    return css_in_parent(css_name, parent_dir)

def create_html_list(folder):
    html_files = glob.glob(os.path.join(folder, '**', '*.html'), recursive=True)
    html_files += glob.glob(os.path.join(folder, '**', '*.htm'), recursive=True)
    css_href_pattern = re.compile(r'href="([^"]+\.css)"', re.IGNORECASE)
    for file_name in html_files:
        with open(file_name, encoding='utf-8') as file:
            content = file.read()
        updated = False
        matches = css_href_pattern.findall(content)
        for href in matches:
            css_path = os.path.normpath(os.path.join(folder, href))
            if os.path.isfile(css_path):
                continue
            print(f'======={file_name}=========')
            print(css_path)
            # look for the css file in parent folder(s)
            found_path = css_in_parent(os.path.basename(css_path), folder)
            if found_path != "":
                new_css_path = os.path.relpath(found_path, folder)
                new_href = f'href="{new_css_path}"'
                old_href = f'href="{href}"'
                content = content.replace(old_href, new_href)
                updated = True
                print(f"{new_href} <======= {old_href}")
        if updated:
            with open(file_name, 'w', encoding='utf-8') as f:
                f.write(content)



def create_files_section(folder, list_file):
    # files_section_lines = ['<files>']
    files_section_lines += create_html_list(folder, list_file)
    files_section_lines.append('</files>')
    return files_section_lines

def parse_param_tag(line):
    label = 'value="'
    start = line.find(label)
    if start == -1:
        return ''
    start += len(label)
    end = line.find('"', start)
    if end == -1:
        return ''
    return line[start:end]

def parse_object_tag(lines, index):
    """
    look at next two lines. they are supposed to look like the following
         <param name="Name" value="Oracle VirtualBox">
         <param name="Local" value="index.html">
    parse out value fields and return
    title="Oracle VirtualBox" ref="index.html
    """
    result = ''
    if index + 2 > len(lines):
        logging.warning('Not enough tags after this one "%s"', lines[index])
        return result
    if    not re.match(r'^\s*<param', lines[index + 1], re.IGNORECASE) \
       or not re.match(r'^\s*<param', lines[index + 2], re.IGNORECASE):
        logging.warning('Skipping the line "%s" since next two tags are supposed to be param tags', lines[index])
        return result
    title = parse_param_tag(lines[index + 1])
    ref = parse_param_tag(lines[index + 2])
    global open_section_tags
    if title and ref:
        open_section_tags += 1
        result = '<section title="' + title + '" ref="' + ref + '">'
    else:
        logging.warning('Title or ref part is empty for the tag "%s"', lines[index])
    return result

def parse_non_object_tag(lines, index):
    """
    parse any string other than staring with <OBJECT
    decide if <section> tag should be closed
    """

    if index + 1 > len(lines):
        return ''
    global open_section_tags
    if open_section_tags <= 0:
        return ''
    # replace </OBJECT with </section only if the next tag is not <UL
    if re.match(r'^\s*</OBJECT', lines[index], re.IGNORECASE):
        if not re.match(r'^\s*<UL', lines[index + 1], re.IGNORECASE):
            open_section_tags -= 1
            return '</section>'
    elif re.match(r'^\s*</UL', lines[index], re.IGNORECASE):
        open_section_tags -= 1
        return '</section>'
    return ''

def parse_line(lines, index):
    result = ''

    # if the line starts with <OBJECT
    if re.match(r'^\s*<OBJECT', lines[index], re.IGNORECASE):
        result = parse_object_tag(lines, index)
    else:
        result = parse_non_object_tag(lines, index)
    return result

def create_toc(folder, toc_file):
    """
    parse TOC file. assuming all the relevant information
    is stored in tags and attributes. whatever is outside of
    <... > pairs is filtered out. we also assume < ..> are not nested
    and each < matches to a >
    """
    toc_string_list = []
    content = [x[2] for x in os.walk(folder)]
    if toc_file not in content[0]:
        logging.error('Could not find toc file "%s" under "%s"', toc_file, folder)
        return toc_string_list
    full_path = os.path.join(folder, toc_file)
    with open(full_path, encoding='utf-8') as file:
        content = file.read()

    # convert the file string into a list of tags there by eliminating whatever
    # char reside outside of tags.
    char_pos = 0
    tag_list = []
    while char_pos < len(content):
        start = content.find('<', char_pos)
        if start == -1:
            break
        end = content.find('>', start)
        if end == -1 or end >= len(content) - 1:
            break
        char_pos = end
        tag_list.append(content[start:end +1])

    # # insert new line chars. to make sure each line includes at most one tag
    # content = re.sub(r'>.*?<', r'>\n<', content)
    # lines = content.split('\n')
    toc_string_list.append('<toc>')
    for index, _ in enumerate(tag_list):
        str = parse_line(tag_list, index)
        if str:
            toc_string_list.append(str)
    toc_string_list.append('</toc>')
    toc_string = '\n'.join(toc_string_list)

    return toc_string_list

def usage(iExitCode):
    print('htmlhelp-postprocess.py -d <helphtmlfolder> -o <outputfilename>')
    return iExitCode

def main(argv):
    # Parse arguments.
    helphtmlfolder = ''
    list_file = ''
    toc_file = ''
    try:
        opts, _ = getopt.getopt(argv[1:], "hd:")
    except getopt.GetoptError as err:
        logging.error(str(err))
        return usage(2)
    for opt, arg in opts:
        if opt == '-h':
            return usage(0)
        if opt == "-d":
            helphtmlfolder = arg
            print(helphtmlfolder)
    # check supplied helphtml folder argument
    if not helphtmlfolder:
        logging.error('No helphtml folder is provided. Exiting')
        return usage(2)
    if not os.path.exists(helphtmlfolder):
        logging.error('folder "%s" does not exist. Exiting', helphtmlfolder)
        return usage(2)
    helphtmlfolder = os.path.normpath(helphtmlfolder)


    create_html_list(helphtmlfolder)

    return 0
if __name__ == '__main__':
    sys.exit(main(sys.argv))
