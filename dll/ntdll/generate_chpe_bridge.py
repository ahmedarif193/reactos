#!/usr/bin/env python3
"""Audit native NTDLL exports and maintain the ARM64EC bridge in one pass.

This is a source-maintenance tool, not part of the target build. It uses the
actual native .def, the existing bridge .spec, and Clang's AST with the bridge's
compile flags. Public declarations are type-checked; native source definitions
and reviewed syscall ABI slots cover otherwise undeclared names. ABI-sensitive
entry points and callbacks still need hand-written wrappers.
"""

import argparse
import json
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
BRIDGE_SPEC = ROOT / "dll/ntdll/chpebridge.spec"
GENERATED = ROOT / "dll/ntdll/chpebridge_generated.inc"
BEGIN = "# BEGIN typed bridge wrappers (generate_chpe_bridge.py)"
END = "# END typed bridge wrappers"
PROBE_INCLUDES = """#include <ntdll.h>
#include <ndk/lpcfuncs.h>
#include <ndk/sefuncs.h>
#include <ndk/dbgkfuncs.h>
#include <ndk/pofuncs.h>
#include <reactos/subsys/csr/csr.h>
#include <delayloadhandler.h>
#include <evntprov.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <md4.h>
#include <md5.h>
#include <sha1.h>
BOOLEAN NTAPI ChpeCanContinueToGuest(VOID);
DECLSPEC_NORETURN VOID NTAPI ChpeContinueToGuest(PVOID);
DECLSPEC_NORETURN VOID NTAPI ChpeContinueToGuestEx(PVOID, BOOLEAN);
NTSTATUS WINAPI RtlWow64GetCurrentCpuArea(USHORT *, void **, void **);
"""

# These parameters contain x64 callbacks or an AMD64 context. They require
# hand-written bridges with emulator dispatch or context conversion.
SPECIAL = {
    "NtCreateThread",
    "NtFsControlFile",
    "NtSetTimer",
    "RtlCreateUserThread",
    "RtlQueueWorkItem",
    "RtlRegisterWait",
}

# Native exports whose public name is an alias rather than a C declaration.
ALIASES = {
    "RtlAnsiStringToUnicodeSize": "RtlxAnsiStringToUnicodeSize",
    "RtlOemStringToUnicodeSize": "RtlxOemStringToUnicodeSize",
    "RtlUnicodeStringToAnsiSize": "RtlxUnicodeStringToAnsiSize",
    "RtlUnicodeStringToOemSize": "RtlxUnicodeStringToOemSize",
    "RtlCopyMemory": "memmove",
}

# These syscall declarations are absent from the user-mode NDK. The native
# .spec supplies the register classes and arity, and NT syscalls return status.
SPEC_TYPED_SYSCALLS = {
    "NtAccessCheckByTypeAndAuditAlarm",
    "NtAccessCheckByTypeResultListAndAuditAlarm",
    "NtAccessCheckByTypeResultListAndAuditAlarmByHandle",
    "NtCreateWnfStateName", "NtDeleteWnfStateData", "NtDeleteWnfStateName",
    "NtQueryDebugFilterState", "NtQueryWnfStateData",
    "NtQueryWnfStateNameInformation", "NtSetDebugFilterState",
    "NtSubscribeWnfStateChange", "NtSystemDebugControl",
    "NtUnsubscribeWnfStateChange", "NtUpdateWnfStateData",
    "ZwGetNlsSectionPtr",
}


def native_exports(path):
    result = {}
    for line in path.read_text().splitlines():
        match = re.match(r"\s*([^;=\s]+)(?:=\S+)?\s+@(\d+)(.*)", line)
        if match:
            result[match.group(1)] = {
                "ordinal": int(match.group(2)),
                "data": "DATA" in match.group(3).split(),
            }
    return result


def spec_entries(path):
    entries = {}
    for line in path.read_text().splitlines():
        clean = line.split("#", 1)[0].split(";", 1)[0].strip()
        if not clean.startswith("@ "):
            continue
        match = re.match(
            r"^@\s+(\w+)\s+((?:-\S+\s+)*)"
            r"([A-Za-z_$][\w$@?]*)\s*(?:\(([^)]*)\))?(?:\s+\S+)?$",
            clean,
        )
        if match:
            entries.setdefault(match.group(3), []).append({
                "kind": match.group(1),
                "options": match.group(2).split(),
                "arguments": match.group(4),
                "line": line,
            })
    return entries


def ast_declarations(build_dir):
    command_lines = subprocess.check_output(
        ["ninja", "-t", "commands", "ntdll_chpe"], cwd=build_dir, text=True
    ).splitlines()
    source = str(ROOT / "dll/ntdll/chpebridge.c")
    line = next(line for line in command_lines if " -c " + source in line)
    command = shlex.split(line)
    for flag in ("-c", "-o", "-MF", "-MT"):
        if flag in command:
            index = command.index(flag)
            del command[index:index + 2]
    command = [arg for arg in command if arg not in ("-MD", "-pipe", source)]

    with tempfile.TemporaryDirectory(prefix="chpe-bridge-ast-") as directory:
        probe = pathlib.Path(directory) / "probe.c"
        probe.write_text(PROBE_INCLUDES)
        command.extend(("-I", str(ROOT / "sdk/lib/cryptlib"), "-Werror",
                        "-Xclang", "-ast-dump=json", "-fsyntax-only", str(probe)))
        run = subprocess.run(command, cwd=build_dir, text=True, capture_output=True)
        if run.returncode:
            raise RuntimeError("Clang AST probe failed:\n" + run.stderr[:4000])
        tree = json.loads(run.stdout)

    declarations = {}

    def visit(node):
        if isinstance(node, list):
            for item in node:
                visit(item)
        elif isinstance(node, dict):
            if node.get("kind") == "FunctionDecl" and node.get("name"):
                params = [item["type"]["qualType"] for item in node.get("inner", [])
                          if item.get("kind") == "ParmVarDecl"]
                previous = declarations.get(node["name"])
                if previous is None or len(params) >= len(previous["params"]):
                    declarations[node["name"]] = {
                    "type": node["type"]["qualType"],
                    "params": params,
                    }
            visit(node.get("inner", []))

    visit(tree)
    return declarations, pathlib.Path(command[0]).parent


def native_source_declarations(build_dir, names):
    """Read definitions with the same Clang flags used to build native NTDLL."""
    if not names:
        return {}
    command_lines = subprocess.check_output(
        ["ninja", "-t", "commands", "ntdll"], cwd=build_dir, text=True
    ).splitlines()
    commands = {}
    for line in command_lines:
        if " -c " not in line:
            continue
        command = shlex.split(line)
        if "-c" not in command:
            continue
        source = command[command.index("-c") + 1]
        if source.endswith(".c"):
            commands[source] = command

    sources = {}
    for directory in (ROOT / "dll/ntdll", ROOT / "sdk/lib/rtl"):
        for path in directory.rglob("*.c"):
            source = str(path)
            if source not in commands:
                continue
            body = path.read_text(errors="replace")
            for match in re.finditer(r"\b[A-Za-z_$][\w$]*\b", body):
                if match.group() in names:
                    sources.setdefault(match.group(), []).append(source)

    decoder = json.JSONDecoder()
    result = {}
    for name in sorted(names):
        for source in sources.get(name, []):
            command = commands[source].copy()
            for flag in ("-c", "-o", "-MF", "-MT"):
                if flag in command:
                    index = command.index(flag)
                    del command[index:index + 2]
            command = [arg for arg in command if arg not in ("-MD", "-pipe", source)]
            command.extend(("-Xclang", "-ast-dump=json", "-Xclang",
                            "-ast-dump-filter", "-Xclang", name,
                            "-fsyntax-only", source))
            run = subprocess.run(command, cwd=build_dir, text=True, capture_output=True)
            if run.returncode:
                raise RuntimeError("Clang source probe failed for %s:\n%s" %
                                   (name, run.stderr[:4000]))
            offset = 0
            while True:
                offset = run.stdout.find("{", offset)
                if offset < 0:
                    break
                try:
                    node, offset = decoder.raw_decode(run.stdout, offset)
                except ValueError:
                    break
                if (node.get("kind") == "FunctionDecl" and node.get("name") == name
                        and any(item.get("kind") == "CompoundStmt"
                                for item in node.get("inner", []))):
                    result[name] = {
                        "type": node["type"]["qualType"],
                        "params": [item["type"]["qualType"] for item in node.get("inner", [])
                                   if item.get("kind") == "ParmVarDecl"],
                        "source": pathlib.Path(source).relative_to(ROOT).as_posix(),
                    }
                    break
            if name in result:
                break
    return result


def exported_spec_name(line):
    match = re.match(
        r"^(?:@|\d+)\s+\w+\s+(?:-\S+\s+)*([A-Za-z_$][\w$@?]*)", line.strip()
    )
    return match.group(1) if match else None


def pe_imports(executable, readobj):
    output = subprocess.check_output(
        [str(readobj), "--coff-imports", str(executable)], text=True
    )
    imports = set()
    current_dll = None
    for line in output.splitlines():
        line = line.strip()
        if line == "Import {":
            current_dll = None
        elif line.startswith("Name: "):
            current_dll = line.removeprefix("Name: ").lower()
        elif current_dll == "ntdll.dll" and line.startswith("Symbol: "):
            imports.add(line.removeprefix("Symbol: ").split(" (", 1)[0])
    return imports


def pe_exports(module, readobj):
    output = subprocess.check_output(
        [str(readobj), "--coff-exports", str(module)], text=True
    )
    return {name: int(ordinal) for ordinal, name in re.findall(
        r"Export \{\s*Ordinal: (\d+)\s*Name: ([^\r\n]+)", output
    )}


def assign_manual_ordinals(text, native):
    """Keep original NTDLL ordinals; place bridge-only helpers after them."""
    extra = max(item["ordinal"] for item in native.values()) + 1
    lines = []
    for line in text.splitlines():
        name = exported_spec_name(line)
        if name:
            ordinal = native[name]["ordinal"] if name in native else extra
            if name not in native:
                extra += 1
            line = re.sub(r"^(\s*)(?:@|\d+)",
                          lambda match: match.group(1) + str(ordinal), line, count=1)
        lines.append(line)
    return "\n".join(lines) + "\n"


def wrapper(name, declaration, target=None):
    target = target or name
    signature = declaration["type"]
    opening = signature.find("(")
    if "..." in signature or opening < 0:
        return None
    result_type = signature[:opening].strip().replace("__size_t", "size_t")
    if not result_type or "(" in result_type:
        return None
    params = declaration["params"]
    arguments = ", ".join("a%d" % index for index in range(len(params)))
    formal = ", ".join(parameter_declaration(typ, index)
                       for index, typ in enumerate(params)) or "VOID"
    call = "%s(%s)" % (target, arguments)
    body = "    %s;\n" % call if result_type in ("VOID", "void") else "    return %s;\n" % call
    return (
        "%s NTAPI ChpeAuto%s(%s)\n{\n%s}\n"
        "_Static_assert(__builtin_types_compatible_p(__typeof__(&ChpeAuto%s), "
        "__typeof__(&%s)), \"%s bridge signature mismatch\");\n"
        % (result_type, name, formal, body, name, target, name)
    )


def parameter_declaration(typ, index):
    name = "a%d" % index
    if "(*)" in typ:
        return typ.replace("(*)", "(*%s)" % name, 1)
    return "%s %s" % (typ, name)


STUB_ARGUMENT_TYPES = {
    "ptr": "PVOID",
    "long": "ULONG",
    "str": "PCSTR",
    "wstr": "PCWSTR",
    "int64": "ULONGLONG",
    "double": "double",
}


def source_wrapper(name, spec, declaration):
    """Use a native definition's return type and the reviewed spec ABI slots."""
    signature = declaration["type"]
    result_type = signature.split("(", 1)[0].strip()
    arguments = (spec["arguments"] or "").split()
    if ("..." in signature or len(arguments) != len(declaration["params"])
            or any(arg not in STUB_ARGUMENT_TYPES for arg in arguments)):
        return None
    if result_type == "TRACEHANDLE":
        result_type = "ULONGLONG"
    elif result_type.startswith("P") and result_type != "PVOID":
        result_type = "PVOID"
    if result_type == "void":
        result_type = "VOID"
    if result_type not in ("VOID", "PVOID", "NTSTATUS", "BOOLEAN", "BOOL",
                           "ULONG", "LONG", "UCHAR", "ULONGLONG"):
        return None
    scalar = {"BOOLEAN", "BOOL", "UCHAR", "USHORT", "SHORT", "ULONG", "LONG",
              "UINT", "DWORD", "NTSTATUS", "SIZE_T", "ULONG_PTR", "ULONGLONG",
              "LONGLONG", "double", "float"}
    param_types = []
    for spec_arg, native_type in zip(arguments, declaration["params"]):
        if native_type == "TRACEHANDLE":
            param_types.append("ULONGLONG")
        elif native_type in scalar:
            param_types.append(native_type)
        else:
            param_types.append(STUB_ARGUMENT_TYPES[spec_arg])
    formal = ", ".join("%s a%d" % (typ, index)
                       for index, typ in enumerate(param_types)) or "VOID"
    actual = ", ".join("a%d" % index for index in range(len(arguments)))
    convention = "CDECL" if spec["kind"] == "cdecl" else "NTAPI"
    invocation = "%s(%s)" % (name, actual)
    body = "    %s;" % invocation if result_type == "VOID" else "    return %s;" % invocation
    code = ("/* Native definition: %s */\n"
            "#ifdef %s\n#undef %s\n#endif\n"
            "%s %s %s(%s);\n"
            "%s %s ChpeAuto%s(%s)\n{\n%s\n}\n" %
            (declaration["source"], name, name, result_type, convention, name, formal,
             result_type, convention, name, formal, body))
    return code


def stub_wrapper(name, spec):
    """Mirror a native spec2def stub, which returns zero without using args."""
    if spec["kind"] == "stub" and spec["arguments"] is None:
        return "stub %s %s" % (retained_options(spec), name), None
    if spec["kind"] not in ("stdcall", "cdecl") or "-stub" not in spec["options"]:
        return None
    arguments = (spec["arguments"] or "").split()
    try:
        formal = ", ".join("%s a%d" % (STUB_ARGUMENT_TYPES[arg], index)
                            for index, arg in enumerate(arguments)) or "VOID"
    except KeyError:
        return None
    convention = "CDECL" if spec["kind"] == "cdecl" else "NTAPI"
    code = "ULONG_PTR %s ChpeStub%s(%s)\n{\n    return 0;\n}\n" % (
        convention, name, formal)
    return "%s %s %s(%s) ChpeStub%s" % (
        spec["kind"], retained_options(spec), name, spec["arguments"], name), code


def retained_options(spec):
    return " ".join(option for option in spec["options"]
                    if not option.startswith("-arch=") and option != "-stub")


def syscall_wrapper(name, spec):
    arguments = (spec["arguments"] or "").split()
    if spec["kind"] != "stdcall" or any(arg not in STUB_ARGUMENT_TYPES for arg in arguments):
        return None
    target = "Nt" + name[2:] if name.startswith("Zw") else name
    formal = ", ".join("%s a%d" % (STUB_ARGUMENT_TYPES[arg], index)
                       for index, arg in enumerate(arguments)) or "VOID"
    actual = ", ".join("a%d" % index for index in range(len(arguments)))
    return ("/* No public prototype: ABI slots and arity from native ntdll.spec. */\n"
            "NTSTATUS NTAPI %s(%s);\n"
            "NTSTATUS NTAPI ChpeAuto%s(%s)\n{\n    return %s(%s);\n}\n" %
            (target, formal, name, formal, target, actual))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=pathlib.Path, required=True)
    parser.add_argument("--scope", choices=("all", "umbrella"), default="all")
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--verify-pe", action="store_true",
                        help="compare the built native and bridge PE export tables")
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    native = native_exports(build_dir.parent / "dll/ntdll/ntdll.def")
    bridge_text = BRIDGE_SPEC.read_text()
    manual_text = bridge_text.split(BEGIN, 1)[0]
    manual = set(spec_entries_from_text(manual_text))
    native_spec = spec_entries(ROOT / "dll/ntdll/def/ntdll.spec")
    declarations, tool_dir = ast_declarations(build_dir)
    candidates = set(native) - manual
    if args.scope == "umbrella":
        tests_dir = build_dir.parent / "_fex_amd64_tests/modules/rostests"
        readobj = tool_dir / "llvm-readobj"
        imports = set()
        for executable in (
            tests_dir / "apitests/ntdll/ntdll_apitest.exe",
            tests_dir / "winetests/ntdll/ntdll_winetest.exe",
        ):
            imports.update(pe_imports(executable, readobj))
        candidates &= imports

    generated = []
    unresolved = {}
    callback_review = {}
    source_declarations = {}
    if args.scope == "all":
        source_names = {name for name in candidates if name not in declarations
                        and name not in SPECIAL and not native[name]["data"]}
        source_declarations = native_source_declarations(build_dir.parent, source_names)
    for name in sorted(candidates, key=lambda item: native[item]["ordinal"]):
        if native[name]["data"] or name == "_fltused":
            generated.append((name, native[name]["ordinal"],
                              "@ extern %s ntdll.%s" % (name, name), None))
            continue
        if name in SPECIAL:
            unresolved[name] = "callback or AMD64 context; hand-written bridge required"
            continue
        spec = choose_spec(native_spec.get(name, []))
        if spec is None:
            unresolved[name] = "no ARM64 native spec entry"
            continue
        stub = stub_wrapper(name, spec)
        if stub is not None:
            spec_line, code = stub
            generated.append((name, native[name]["ordinal"], "@ " + spec_line, code))
            continue
        target = ALIASES.get(name, name)
        declaration = declarations.get(target)
        if declaration is None and name.startswith("Zw"):
            alias = "Nt" + name[2:]
            alias_declaration = declarations.get(alias)
            if alias_declaration is not None and len(alias_declaration["params"]) == len((spec["arguments"] or "").split()):
                declaration = alias_declaration
                target = alias
        if declaration is None:
            if name in SPEC_TYPED_SYSCALLS or (name.startswith("Zw") and "Nt" + name[2:] in SPEC_TYPED_SYSCALLS):
                code = syscall_wrapper(name, spec)
                if code is not None:
                    options = retained_options(spec)
                    prefix = "@ %s %s " % (spec["kind"], options) if options else "@ %s " % spec["kind"]
                    generated.append((name, native[name]["ordinal"],
                                      prefix + "%s(%s) ChpeAuto%s" %
                                      (name, spec["arguments"], name), code))
                    continue
            native_declaration = source_declarations.get(name)
            code = (source_wrapper(name, spec, native_declaration)
                    if native_declaration else None)
            if code is not None:
                options = retained_options(spec)
                prefix = "@ %s %s " % (spec["kind"], options) if options else "@ %s " % spec["kind"]
                generated.append((name, native[name]["ordinal"],
                                  prefix + "%s(%s) ChpeAuto%s" %
                                  (name, spec["arguments"], name), code))
                continue
            unresolved[name] = "no compatible declaration or source definition"
            continue
        code = wrapper(name, declaration, target)
        if code is None:
            unresolved[name] = "variadic or unsupported declaration"
            continue
        if not spec or spec["arguments"] is None or spec["kind"] in ("stub", "extern"):
            unresolved[name] = "no callable ARM64 native spec entry"
            continue
        options = retained_options(spec)
        prefix = "@ %s %s " % (spec["kind"], options) if options else "@ %s " % spec["kind"]
        generated.append((name, native[name]["ordinal"],
                          prefix + "%s(%s) ChpeAuto%s" % (name, spec["arguments"], name), code))
        callbacks = [typ for typ in declaration["params"]
                     if re.search(r"\(\s*\*\s*\)|CALLBACK|ROUTINE|PROBER",
                                  typ, re.IGNORECASE)]
        if callbacks:
            callback_review[name] = callbacks

    generated_spec = "\n".join(
        [BEGIN] + [str(item[1]) + item[2][1:] for item in generated] + [END, ""]
    )
    generated_code = (
        "/* Generated by generate_chpe_bridge.py; edit the helper or manual bridge instead. */\n\n"
        + "\n".join(item[3] for item in generated if item[3] is not None)
    )
    report = {
        "native_exports": len(native),
        "manual_bridge_exports": len(manual),
        "scope": args.scope,
        "candidate_exports": len(candidates),
        "generated": [item[0] for item in generated],
        "unresolved": unresolved,
        "generated_callback_review": callback_review,
    }
    missing_pe = set()
    ordinal_mismatches = {}
    if args.verify_pe:
        readobj = tool_dir / "llvm-readobj"
        native_pe = pe_exports(build_dir.parent / "dll/ntdll/ntdll.dll", readobj)
        bridge_pe = pe_exports(build_dir / "dll/ntdll/ntdll_chpe.dll", readobj)
        missing_pe = native_pe.keys() - bridge_pe.keys()
        ordinal_mismatches = {
            name: {"native": native_pe[name], "bridge": bridge_pe[name]}
            for name in sorted(native_pe.keys() & bridge_pe.keys())
            if native_pe[name] != bridge_pe[name]
        }
        report["native_pe_exports"] = len(native_pe)
        report["bridge_pe_exports"] = len(bridge_pe)
        report["missing_pe_exports"] = sorted(missing_pe)
        report["bridge_only_pe_exports"] = sorted(bridge_pe.keys() - native_pe.keys())
        report["ordinal_mismatches"] = ordinal_mismatches
    if args.report:
        args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print("native=%d manual=%d candidates=%d generated=%d unresolved=%d" %
          (len(native), len(manual), len(candidates), len(generated), len(unresolved)))
    if args.verify_pe:
        print("PE exports: native=%d bridge=%d missing=%d ordinal_mismatches=%d" %
              (len(native_pe), len(bridge_pe), len(missing_pe), len(ordinal_mismatches)))
    if args.write:
        if BEGIN in bridge_text:
            bridge_text = bridge_text.split(BEGIN, 1)[0]
        BRIDGE_SPEC.write_text(assign_manual_ordinals(bridge_text.rstrip(), native)
                               + "\n" + generated_spec)
        GENERATED.write_text(generated_code)
    return 0 if not unresolved and not missing_pe and not ordinal_mismatches else 1


def spec_entries_from_text(text):
    for line in text.splitlines():
        name = exported_spec_name(line)
        if name:
            yield name


def choose_spec(entries):
    for entry in entries:
        arch = next((value.split("=", 1)[1] for value in entry["options"]
                     if value.startswith("-arch=")), None)
        if arch is None or "arm64" in arch.split(","):
            return entry
    return None


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, StopIteration) as error:
        print("bridge generation failed:", error, file=sys.stderr)
        sys.exit(2)
