COPYRIGHT = """\
/*
 * Copyright (C) 2020 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
"""

import argparse
import xml.etree.ElementTree as ET
from mako.template import Template

TEMPLATE  = Template("""\
/* DO NOT EDIT - This file is generated automatically by vtn_generator_ids.py script */

""" + COPYRIGHT + """\
<%
def get_name(generator):
    name = generator.get('tool').lower()
    name = name.replace('-', '')
    name = name.replace(' ', '_')
    name = name.replace('/', '_')
    return name
%>
enum vtn_generator {
% for generator in root.find("./ids[@type='vendor']").findall('id'):
% if 'tool' in generator.attrib:
   vtn_generator_${get_name(generator)} = ${generator.get('value')},
% endif
% endfor
   vtn_generator_max = 0xffff,
};
""")

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("xml")
    p.add_argument("out")
    pargs = p.parse_args()

    tree = ET.parse(pargs.xml)
    root = tree.getroot()

    with open(pargs.out, 'w', encoding='utf-8') as f:
        f.write(TEMPLATE.render(root=root))
