"""
SCons helpers shared by the SDK SConstruct and the project SConstruct. Import from SCons only.
"""

import contextlib
import json
import os
import re
import shutil
import subprocess
import sys

import SCons.Script
from SCons.Defaults import processDefines
from SCons.Tool import Tool
from SCons.Variables import Variables

from . import manifest as mf

# godot-cpp hardcodes C++17. EnTT needs C++20, so the SDK and every project needs to replace that flag.
CXX_STANDARD = "20"
STANDARD_FLAG = re.compile(r"^(-std=(?:c|gnu)\+\+|/std:c\+\+)(\w+)$")
SOURCE_EXTENSIONS = (".c", ".cc", ".cpp", ".cxx")


@contextlib.contextmanager
def godot_cpp_directory(env, godot_cpp_dir):
    """Make both SCons and the process treat the godot-cpp directory as the current one.

    The godot-cpp tool finds its platform tools relative to where it runs: godot-cpp 4.5 and
    later use env.Dir("tools"), which follows SCons's own current directory, while 4.4 and
    earlier use the plain relative path "tools", which follows the process working directory.
    Called from anywhere else both look in the wrong place, so change both, as SCons itself does
    when it reads godot-cpp's own SConstruct.
    """
    previous = env.fs.getcwd()
    env.fs.chdir(env.Dir(godot_cpp_dir), change_os_dir=True)
    try:
        yield
    finally:
        env.fs.chdir(previous, change_os_dir=True)

def load_godot_cpp_tool(godot_cpp_dir):
    # godotcpp.py imports binding_generator and friends from the godot-cpp root.
    if godot_cpp_dir not in sys.path:
        sys.path.append(godot_cpp_dir)
    return Tool("godotcpp", toolpath=[os.path.join(godot_cpp_dir, "tools")])


def unique(items):
    return list(dict.fromkeys(items))


def probe_declared_options(env, godot_cpp_dir, customs):
    """Declare the godot-cpp options on a scratch environment.

    Returns (option names, arguments nobody recognised). The names come from the tool itself,
    so nothing is listed here that has to be kept in step with godot-cpp.
    """
    probe_env = env.Clone()
    variables = Variables(customs, SCons.Script.ARGUMENTS)
    with godot_cpp_directory(probe_env, godot_cpp_dir):
        tool = load_godot_cpp_tool(godot_cpp_dir)
        tool.options(variables, probe_env)
        variables.Update(probe_env)
    return unique(variables.keys()), variables.UnknownVariables()

def use_cxx_standard(env, standard=CXX_STANDARD):
    """Replace godot-cpp's C++ standard flag in place, or add one when it set none."""
    flags = [str(flag) for flag in env.get("CXXFLAGS", [])]
    found = False
    for i, flag in enumerate(flags):
        match = STANDARD_FLAG.match(flag)
        if match:
            flags[i] = match.group(1) + standard
            found = True
    if not found:
        flags.append(("/std:c++" if env.get("is_msvc", False) else "-std=c++") + standard)
    env["CXXFLAGS"] = flags


def cxx_standard(env):
    for flag in reversed([str(flag) for flag in env.get("CXXFLAGS", [])]):
        match = STANDARD_FLAG.match(flag)
        if match:
            return match.group(2)
    return None

def _plain(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    return str(value)


def effective_options(env, keys):
    """Value of every option after the tool has run.

    generate() derives some values (arch, optimize, debug_symbols) and keeps others as
    attributes on the environment (use_hot_reload) instead of keys, so read both.
    """
    values = {}
    for key in keys:
        value = env.get(key)
        if value is None:
            value = getattr(env, key, None)
        values[key] = _plain(value)
    return values


def normalized_defines(env):
    return sorted(processDefines(env.get("CPPDEFINES", [])))


def compiler_info(env):
    info = {}
    for key in ("CXX", "CXXVERSION", "CC", "CCVERSION", "MSVC_VERSION", "TARGET_ARCH"):
        value = env.get(key)
        if value:
            info[key] = str(value)
    return info


def missing_compiler(env):
    """Name of the C++ compiler the tool chose when it cannot be found, otherwise None.

    The godot-cpp platform tools mostly report that a toolchain exists without looking, so
    this checks the compiler they actually selected, whatever the platform.
    """
    compiler = env.subst("$CXX")
    if compiler and not env.WhereIs(compiler):
        return compiler
    return None


def resolved_config(env, keys):
    # The standard is not a godot-cpp option, but it must match like one, so it is compared with them.
    options = effective_options(env, keys)
    options["cxx_standard"] = cxx_standard(env)

    return {
        "schema": mf.SCHEMA,
        "platform": env["platform"],
        "target": env["target"],
        "arch": env["arch"],
        "suffix": env["suffix"],
        "options": options,
        "defines": normalized_defines(env),
        "compiler": compiler_info(env),
    }

def get_git_commit(directory):
    if not os.path.exists(os.path.join(directory, ".git")):
        return None
    try:
        result = subprocess.run(["git", "-C", directory, "rev-parse", "HEAD"], capture_output=True, text=True)
    except OSError:
        return None
    if result.returncode != 0:
        return None
    return result.stdout.strip() or None


def write_text_if_changed(path, text):
    try:
        with open(path, "r") as f:
            if f.read() == text:
                return
    except OSError:
        pass

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    temp = path + ".gdn-tmp"
    with open(temp, "w") as f:
        f.write(text)
    os.replace(temp, path)


def write_manifest(target, source, env):
    """Command action. The last source is a Value node holding the manifest as JSON."""
    data = json.loads(source[-1].read())
    write_text_if_changed(str(target[0]), json.dumps(data, indent=2, sort_keys=True) + "\n")
    return 0

def atomic_install(target, source, env):
    """Copy to a temporary name beside the target, then rename over it.

    The rename gives the library a new file, so a process that has the old one mapped keeps a
    consistent image instead of having it overwritten underneath it. Mark the target Precious,
    otherwise SCons deletes it before this runs.
    """
    destination = str(target[0])
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    temp = destination + ".gdn-tmp"
    shutil.copy2(str(source[0]), temp)
    os.replace(temp, destination)
    return 0

# This function must not be used by the CLI. Only inside SCons
def collect_sources(src_dir, variant_dir=None, exclude_dirs=(), extensions=SOURCE_EXTENSIONS):
    """
    Recursively collects the C/C++ sources under src_dir as SCons File nodes.

    src_dir       Folder to scan, relative to the calling SConstruct (or absolute).
    variant_dir   When set, each source is returned under this folder instead, keeping its relative
                  path (src/ecs/world.cpp -> <variant_dir>/ecs/world.cpp). The caller must have declared
                  env.VariantDir(variant_dir, src_dir, ...) so SCons maps it back to the real file.
    exclude_dirs  Folders to skip. A plain name ("gen") is skipped at any depth. A relative path
                  ("thirdparty/foo") is skipped only at that location under src_dir.
                  Folders starting with "." are always skipped.
    extensions    File extensions to compile, matched case-insensitively.

    The result is sorted so the build order is the same on every machine.
    """
    # Imported here, not at module level: this module may also be loaded outside SCons (e.g. by the gdn CLI)
    from SCons.Script import Dir, File

    src_root = Dir(src_dir).srcnode().abspath
    if not os.path.isdir(src_root):
        return []

    excluded = {os.path.normpath(d) for d in exclude_dirs}
    base = variant_dir if variant_dir else src_dir
    found = []

    for dirpath, dirnames, filenames in os.walk(src_root):
        rel_dir = os.path.relpath(dirpath, src_root)

        # Editing dirnames in place controls which folders os.walk goes into
        kept = []
        for name in dirnames:
            rel_path = os.path.normpath(os.path.join(rel_dir, name))
            if name.startswith(".") or name in excluded or rel_path in excluded:
                continue
            kept.append(name)
        dirnames[:] = sorted(kept)

        for name in sorted(filenames):
            if name.lower().endswith(extensions):
                found.append(File(os.path.normpath(os.path.join(base, rel_dir, name))))

    return found