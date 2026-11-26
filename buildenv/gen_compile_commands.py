#!/usr/bin/env python3
"""Generate compile_commands.json (for CLion / clangd) from the build logs.

The kernel and userland are compiled inside a container by plain Makefiles, so
the IDE has no way of knowing which compiler flags each file uses. Every
compiler command is however printed in the build output. This script reads
those logs, translates the container paths (/linux, /build) back to real paths
on this machine, and writes compile_commands.json in the project root.

usage: gen_compile_commands.py [kernel.log user.log ...]
       (default: every *.log file in <project>/build-logs)
"""
import json
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
UCLINUX = os.path.join(ROOT, "uClinux")
USER = os.path.join(ROOT, "user")
LOG_DIR = os.path.join(ROOT, "build-logs")
IMAGE = "uclinux-buildenv:0.1"
# compiler-internal include dirs that exist only inside the container
# (stddef.h, stdarg.h, ... and the m68k-elf libc headers); copied to the host
TOOLCHAIN_DIR = os.path.join(ROOT, "buildenv", "toolchain-include")
TOOLCHAIN_SRC = (
    ("gcc", "/usr/local/lib/gcc-lib/m68k-elf/2.95.3/include"),
    ("libc", "/usr/local/m68k-elf/include"),
)

# options of the m68k cross compiler that the host compiler used by the IDE
# can't handle (-m68000, -msep-data, -mid-shared-library, ...) or that only
# matter for linking
DROP_RE = re.compile(r"^(-m.*|-Wl,.*|-elf2flt|-Wa,.*)$")
# the host compiler is told to act as a 32 bit m68k as far as possible
HOST_FLAGS = ["-m32", "-D__mc68000__", "-D__m68k__"]

SOURCE_RE = re.compile(r"\.(c|cc|cpp|S)$")
COMPILER_RE = re.compile(r"(^|/)m68k-elf-(gcc|g\+\+|cc)$")
MAKING_RE = re.compile(r"^ ==== making \S+ in (\S+) with ")
ENTERING_RE = re.compile(r"make\[\d+\]: (Entering|Leaving) directory '(.*)'")


def map_path(path, base):
    """container path -> host path"""
    for prefix, target in (("/linux", UCLINUX), ("/build", base)):
        if path == prefix or path.startswith(prefix + "/"):
            return target + path[len(prefix):]
    return path


def map_token(tok, base):
    # handles "-I/linux/foo", "-L/linux/foo", "/linux/foo", "-I/build"
    m = re.match(r"^(-[IL]|-include|-isystem|-iquote)?(/.*)$", tok)
    if m:
        return (m.group(1) or "") + map_path(m.group(2), base)
    return tok


_extra = None


def extra_includes():
    """-isystem flags for the toolchain headers copied out of the image"""
    global _extra
    if _extra is None:
        _extra = []
        for name, _src in TOOLCHAIN_SRC:
            d = os.path.join(TOOLCHAIN_DIR, name)
            if os.path.isdir(d):
                _extra += ["-isystem", d]
    return _extra


def export_toolchain_headers():
    """copy the cross compiler's headers from the docker image to the host"""
    cid = None
    try:
        cid = subprocess.run(["docker", "create", IMAGE], check=True, capture_output=True,
                             text=True).stdout.strip()
        for name, src in TOOLCHAIN_SRC:
            dst = os.path.join(TOOLCHAIN_DIR, name)
            os.makedirs(dst, exist_ok=True)
            subprocess.run(["docker", "cp", "%s:%s/." % (cid, src), dst], check=True,
                           capture_output=True)
    except (OSError, subprocess.CalledProcessError) as e:
        print("warning: couldn't copy toolchain headers from docker image %s: %s" % (IMAGE, e),
              file=sys.stderr)
    finally:
        if cid:
            subprocess.run(["docker", "rm", cid], capture_output=True)


def parse_log(path, entries):
    base = UCLINUX  # container /build or cwd of the top level make
    stack = []
    with open(path, errors="replace") as f:
        pending = ""
        for line in f:
            # docker -t terminates lines with \r\n, make prints multi-line
            # commands with backslash continuations
            line = line.rstrip("\r\n")
            if pending:
                line = pending + " " + line.lstrip()
                pending = ""
            if line.endswith("\\"):
                pending = line[:-1]
                continue

            m = MAKING_RE.match(line)
            if m:
                base = os.path.join(USER, m.group(1))
                stack = []
                continue
            if line.startswith(" ==== building libraries"):
                base = UCLINUX
                stack = []
                continue

            m = ENTERING_RE.search(line)
            if m:
                if m.group(1) == "Entering":
                    stack.append(map_path(m.group(2), base))
                elif stack:
                    stack.pop()
                continue

            if " -c " not in line:
                continue
            try:
                tokens = shlex.split(line.strip())
            except ValueError:
                continue
            # the compiler may be preceded by e.g. "ccache" or "sh -c"
            start = next((i for i, t in enumerate(tokens[:3]) if COMPILER_RE.search(t)), None)
            if start is None:
                continue
            tokens = tokens[start:]
            src = next((t for t in reversed(tokens) if SOURCE_RE.search(t)), None)
            if src is None:
                continue

            cwd = os.path.normpath(stack[-1] if stack else base)
            mapped = [map_token(t, base) for t in tokens[1:]]
            args = ["gcc"] + HOST_FLAGS + extra_includes()
            args += [t for t in mapped if not DROP_RE.match(t)]
            src_abs = os.path.normpath(os.path.join(cwd, map_token(src, base)))
            if not os.path.exists(src_abs):
                continue
            entries[(cwd, src_abs)] = {
                "directory": cwd,
                "arguments": args,
                "file": src_abs,
            }


def main():
    logs = sys.argv[1:]
    if not logs:
        if not os.path.isdir(LOG_DIR):
            sys.exit("no logs given and %s doesn't exist, run ./build-all.sh first" % LOG_DIR)
        logs = sorted(os.path.join(LOG_DIR, n) for n in os.listdir(LOG_DIR) if n.endswith(".log"))

    if not os.path.isdir(os.path.join(TOOLCHAIN_DIR, "gcc")):
        export_toolchain_headers()

    entries = {}
    for log in logs:
        parse_log(log, entries)

    # the bootloader installer is a normal program for your PC, compiled with the host gcc
    boot = os.path.join(ROOT, "bootloader")
    inst = os.path.join(boot, "installer.c")
    if os.path.exists(inst):
        entries[(boot, inst)] = {
            "directory": boot,
            "arguments": ["gcc", "-std=gnu11", "-c", "installer.c", "-o", "installer.o"],
            "file": inst,
        }

    out = os.path.join(ROOT, "compile_commands.json")
    with open(out, "w") as f:
        json.dump(list(entries.values()), f, indent=1)
    print("wrote %s (%d source files)" % (out, len(entries)))


if __name__ == "__main__":
    main()
