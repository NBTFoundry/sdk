# SPDX-License-Identifier: MPL-2.0
"""The OpenGL ES 2.0 commands of Khronos' gl.xml, for the generators of the
module GL table (gen_gl_table.py) and the guest GL stream (gen_gl_stream.py)."""
import xml.etree.ElementTree as ET


class Param:
    """One parameter: its C declaration, name, type and gl.xml length."""

    def __init__(self, node):
        self.text = "".join(node.itertext()).strip()
        self.name = node.find("name").text
        self.ctype = self.text[: -len(self.name)].strip()
        ptype = node.find("ptype")
        self.base = ptype.text if ptype is not None else "void"
        self.pointer = "*" in self.ctype
        self.const = self.ctype.startswith("const")
        self.len = node.get("len")


class Command:
    def __init__(self, name, ret, params):
        self.name = name
        self.ret = ret
        self.params = params

    def decl(self):
        """The parameter list, for a definition."""
        return ", ".join(p.text for p in self.params) or "void"

    def types(self):
        """The parameter types, for a function pointer."""
        return ", ".join(p.ctype for p in self.params) or "void"

    def args(self):
        return ", ".join(p.name for p in self.params)


def gles2_commands(path):
    """The OpenGL ES 2.0 commands, sorted by name. The set is frozen, so the
    order (and an index into it) never changes."""
    root = ET.parse(path).getroot()
    names = set()
    for feature in root.findall("feature"):
        if feature.get("api") == "gles2" and feature.get("name") == "GL_ES_VERSION_2_0":
            for req in feature.findall("require"):
                names.update(c.get("name") for c in req.findall("command"))
    commands = {}
    for node in root.find("commands").findall("command"):
        proto = node.find("proto")
        name = proto.find("name").text
        if name in names:
            ret = "".join(proto.itertext()).strip()[: -len(name)].strip()
            commands[name] = Command(name, ret, [Param(p) for p in node.findall("param")])
    missing = names - set(commands)
    if missing:
        raise SystemExit("gl.xml lacks: " + ", ".join(sorted(missing)))
    return [commands[n] for n in sorted(names)]
