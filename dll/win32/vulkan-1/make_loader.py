#!/usr/bin/env python3
#
# PROJECT:     ReactOS Vulkan loader
# LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
# PURPOSE:     Generate the vulkan-1 export spec and dispatch trampolines
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
#

import pathlib
import re
import sys

SPECIAL = {
    "vkCreateInstance", "vkDestroyInstance", "vkEnumerateInstanceExtensionProperties",
    "vkEnumerateInstanceLayerProperties", "vkEnumerateInstanceVersion", "vkGetInstanceProcAddr",
    "vkGetDeviceProcAddr", "vkEnumeratePhysicalDevices", "vkEnumeratePhysicalDeviceGroups",
    "vkCreateDevice", "vkDestroyDevice", "vkGetDeviceQueue", "vkGetDeviceQueue2",
    "vkAllocateCommandBuffers", "vkEnumerateDeviceLayerProperties", "vkEnumerateDeviceExtensionProperties",
}
INSTANCE_HANDLES = {"VkInstance", "VkPhysicalDevice"}
DEVICE_HANDLES = {"VkDevice", "VkQueue", "VkCommandBuffer"}


def read_exports(spec_path):
    exports = []
    for line in spec_path.read_text().splitlines():
        m = re.match(r"@\s+stdcall\s+(vk\w+)\(([^)]*)\)", line)
        if m:
            exports.append((m.group(1), m.group(2).split()))
            continue
        m = re.match(r"@\s+stub\s+(vk\w+)", line)
        if m:
            exports.append((m.group(1), None))
    return exports


def spec_args(params, handles):
    args = []
    for t, _, a in params:
        base = t.replace("const", "").replace("struct", "").strip()
        if "*" in t or a or base in INSTANCE_HANDLES or base in DEVICE_HANDLES:
            args.append("ptr")
        elif base in ("uint64_t", "int64_t", "VkDeviceSize", "VkDeviceAddress") or base in handles:
            args.append("int64")
        elif base == "float":
            args.append("float")
        elif base == "double":
            args.append("double")
        else:
            args.append("long")
    return args


def read_prototypes(headers):
    protos = {}
    pattern = re.compile(r"VKAPI_ATTR\s+([\w\s\*]+?)\s+VKAPI_CALL\s+(vk\w+)\s*\(([^;]*?)\)\s*;", re.S)
    for header in headers:
        for m in pattern.finditer(header.read_text()):
            params = []
            body = " ".join(m.group(3).split())
            if body and body != "void":
                for p in body.split(","):
                    p = p.strip()
                    nm = re.match(r"(.*?)(\w+)\s*(\[[^\]]*\])?$", p)
                    params.append((nm.group(1).strip(), nm.group(2), nm.group(3) or ""))
            protos[m.group(2)] = (" ".join(m.group(1).split()), params)
    return protos


def main():
    spec_in, core_h, win32_h, out_dir = map(pathlib.Path, sys.argv[1:5])
    exports = read_exports(spec_in)
    protos = read_prototypes([core_h, win32_h])
    handles = set(re.findall(r"VK_DEFINE_NON_DISPATCHABLE_HANDLE\((\w+)\)", core_h.read_text()))
    stubs = [name for name, args in exports if args is None and name not in protos]
    exports = [(name, args if args is not None else spec_args(protos[name][1], handles))
               for name, args in exports if name not in stubs]
    missing = [name for name, _ in exports if name not in protos]
    if missing:
        sys.exit("no prototype for: " + ", ".join(missing))

    spec = ["@ stdcall %s(%s)" % (name, " ".join(args)) for name, args in exports]
    spec += ["@ stub %s" % name for name in stubs]
    (out_dir / "vulkan-1.spec").write_text("\n".join(spec) + "\n")

    inst, dev = [], []
    for name, _ in exports:
        if name in SPECIAL:
            continue
        first = protos[name][1][0][0] if protos[name][1] else ""
        if first in INSTANCE_HANDLES:
            inst.append(name)
        elif first in DEVICE_HANDLES:
            dev.append(name)
        else:
            sys.exit("unclassified export: " + name)

    extra_inst = ["vkGetDeviceProcAddr", "vkCreateDevice", "vkEnumeratePhysicalDevices",
                  "vkEnumeratePhysicalDeviceGroups", "vkEnumerateDeviceExtensionProperties", "vkDestroyInstance"]
    extra_dev = ["vkDestroyDevice", "vkGetDeviceQueue", "vkGetDeviceQueue2", "vkAllocateCommandBuffers"]
    inst_all = inst + extra_inst
    dev_all = dev + extra_dev

    h = ["#pragma once", "", "enum vk_instance_function", "{"]
    h += ["    VKI_%s," % n for n in inst_all] + ["    VKI_COUNT", "};", "", "enum vk_device_function", "{"]
    h += ["    VKD_%s," % n for n in dev_all] + ["    VKD_COUNT", "};", ""]
    h += ["extern const char *const vk_instance_function_names[VKI_COUNT];",
          "extern const char *const vk_device_function_names[VKD_COUNT];",
          "extern const struct vk_export vk_exports[];",
          "extern const unsigned int vk_export_count;", ""]
    (out_dir / "loader_gen.h").write_text("\n".join(h))

    c = ['#include "vulkan_private.h"', ""]
    c += ["const char *const vk_instance_function_names[VKI_COUNT] =", "{"]
    c += ['    "%s",' % n for n in inst_all] + ["};", ""]
    c += ["const char *const vk_device_function_names[VKD_COUNT] =", "{"]
    c += ['    "%s",' % n for n in dev_all] + ["};", ""]
    for name in inst + dev:
        ret, params = protos[name]
        decl = ", ".join("%s %s%s" % (t, n, a) for t, n, a in params)
        args = ", ".join(n for _, n, _ in params)
        table = "vk_instance_table" if name in inst else "vk_device_table"
        index = ("VKI_" if name in inst else "VKD_") + name
        call = "((PFN_%s)%s(%s)->Functions[%s])(%s)" % (name, table, params[0][1], index, args)
        c += ["VKAPI_ATTR %s VKAPI_CALL %s(%s)" % (ret, name, decl), "{"]
        c += ["    %s%s;" % ("" if ret == "void" else "return ", call), "}", ""]
    c += ["const struct vk_export vk_exports[] =", "{"]
    c += ['    { "%s", (PFN_vkVoidFunction)%s },' % (n, n) for n, _ in sorted(exports)] + ["};", ""]
    c += ["const unsigned int vk_export_count = sizeof(vk_exports) / sizeof(vk_exports[0]);", ""]
    (out_dir / "loader_gen.c").write_text("\n".join(c))


if __name__ == "__main__":
    main()
