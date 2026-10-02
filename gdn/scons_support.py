"""
SCons helpers shared by the SDK SConstruct and the project SConstruct. Import from SCons only.
"""

import contextlib
import json
import os
import shutil
import subprocess
import sys

import SCons.Script
from SCons.Defaults import processDefines
from SCons.Tool import Tool
from SCons.Variables import Variables

from . import manifest as mf


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
    return {
        "schema": mf.SCHEMA,
        "platform": env["platform"],
        "target": env["target"],
        "arch": env["arch"],
        "suffix": env["suffix"],
        "options": effective_options(env, keys),
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
