"""

    gdn init             setup the current Godot project
    gdn build            build the project native library and its dependencies (SDK variant)
    gdn resolve          show which SDK variant a build request maps to, without building
    gdn sdk build        build an SDK variant
    gdn sdk list         list the SDK variants that are built
    gdn options          list every build option the godot-cpp tool accepts

All build option arguments are handed to SCons. The toolset from godot-cpp is used to determine what is valid and what the defaults are.
IMPORTANT: Path-valued options must be absolute !
"""

import argparse
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
 
from . import manifest as mf
 
EXIT_OK = 0
EXIT_ERROR = 1
EXIT_USAGE = 2
EXIT_CONFIRMATION_REQUIRED = 42
 
OVERRIDE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=")
 
DEFAULT_LIBRARY_NAME = "MyNativeLib"
LIBRARY_NAME = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
 
 
def error(message):
    print("ERROR: " + message, file=sys.stderr)
 
 
# Split between the overrides passed to SCons and the other argumetns used by the CLI
def split_overrides(argv):
    overrides, rest = [], []
    for token in argv:
        (overrides if OVERRIDE.match(token) else rest).append(token)
    return overrides, rest
 
 
#
# Locations
#
 
 
def find_sdk_root(explicit=None):
    candidate = explicit or os.environ.get("GDN_SDK_ROOT") or Path(__file__).resolve().parent.parent
    root = Path(candidate).resolve()
    missing = [name for name in ("SConstruct", "godot-cpp/SConstruct", "templates") if not (root / name).exists()]
    if missing:
        error("{} is not a usable SDK root, missing: {}".format(root, ", ".join(missing)))
        return None
    return root
 
 
def find_project(start):
    for directory in [start, *start.parents]:
        if (directory / "project.godot").is_file():
            return directory
    return None
 
 
def require_project(explicit=None):
    if explicit:
        project = Path(explicit).resolve()
        if not (project / "project.godot").is_file():
            error("{} is not a Godot project (no project.godot found).".format(project))
            return None
    else:
        project = find_project(Path.cwd())
    if project is None:
        error("Not inside a Godot project (no project.godot found).")
        return None
    if not (project / "native" / "SConstruct").is_file():
        error("{} has no native/SConstruct. Run 'gdn init' first.".format(project))
        return None
    return project
 
 
#
# SCons
#
 
 
def scons_available():
    if importlib.util.find_spec("SCons") is None:
        error("SCons is not installed for this Python. Run: {} -m pip install scons".format(sys.executable))
        return False
    return True
 
 
def run_scons(cwd, args, capture=False):
    command = [sys.executable, "-m", "SCons"] + list(args)
    if capture:
        return subprocess.run(command, cwd=str(cwd), capture_output=True, text=True)
    return subprocess.run(command, cwd=str(cwd))
 
 
def jobs_args(jobs):
    return ["-j", str(jobs)] if jobs else []
 
 
def resolve_request(native_dir, sdk, overrides):
    # Ask the project SConstruct what the godot-cpp tool makes of this request:
    with tempfile.TemporaryDirectory() as temp:
        out = Path(temp) / "resolved.json"
        result = run_scons(native_dir, ["-Q", "--gdn-resolve=" + str(out), "sdk_path=" + str(sdk)] + overrides, capture=True)
        if result.returncode != 0 or not out.is_file():
            sys.stdout.write(result.stdout)
            sys.stderr.write(result.stderr)
            if result.returncode == mf.EXIT_TOOLCHAIN:
                error("The toolchain for this platform is not available on this machine.")
            return None
        with open(str(out), "r") as f:
            return json.load(f)
 
 
def confirm_dialog(question, assume_yes):
    #Returns True or False when answered, None when there is nobody to ask
    if assume_yes:
        return True
    if not sys.stdin.isatty():
        return None
    return input(question + " [y/N] ").strip().lower() in ("y", "yes")
 
 
def show_variant_state(resolved, check, heading=True):
    if heading:
        print("Requested: " + mf.label(resolved))
    if check.state == "mismatch":
        print("The SDK has a build for this platform, target and arch, but with different settings:")
        for key, pair in check.diffs.items():
            print("  " + mf.describe_difference(key, pair))
        # 
        # TODO: Variants should not override each other
        #
        print("Building it again replaces that variant.")
        return
    print("The SDK has no build for the current settings")
    if check.closest:
        print("Closest variants:")
        for other, diffs in check.closest:
            print("  {}  (differs in: {})".format(mf.label(other), ", ".join(diffs) or "nothing"))
 
 
def build_sdk(sdk, overrides, jobs):
    return run_scons(sdk, jobs_args(jobs) + overrides).returncode
 
 
def ensure_sdk_variant(sdk, resolved, overrides, assume_yes, jobs):
    """Return EXIT_OK when a matching SDK variant exists, building it first if the user agrees."""
    check = mf.check_variant(sdk, resolved)
    if check.state == "ok":
        return EXIT_OK
 
    show_variant_state(resolved, check)
    answer = confirm_dialog("Build this SDK variant now? It compiles godot-cpp and can take several minutes.", assume_yes)
    if answer is None:
        print("Not building: confirmation is required. Re-run with --yes, or run 'gdn sdk build' yourself.")
        return EXIT_CONFIRMATION_REQUIRED
    if not answer:
        print("Cancelled, nothing was built.")
        return EXIT_ERROR
 
    code = build_sdk(sdk, overrides, jobs)
    if code != 0:
        error("The SDK build failed.")
        return code
 
    check = mf.check_variant(sdk, resolved)
    if check.state != "ok":
        error("The SDK built, but not the variant the project asked for. Check custom_overrides.py in the SDK root.")
        show_variant_state(resolved, check)
        return EXIT_ERROR
    return EXIT_OK
 
 
#
# Commands
#
 
 
def cmd_build(args, overrides):
    if not scons_available():
        return EXIT_ERROR
    sdk = find_sdk_root(args.sdk_root)
    project = require_project(args.project)
    if sdk is None or project is None:
        return EXIT_ERROR
    native = project / "native"
 
    resolved = resolve_request(native, sdk, overrides)
    if resolved is None:
        return EXIT_ERROR
 
    code = ensure_sdk_variant(sdk, resolved, overrides, args.yes, args.jobs)
    if code != EXIT_OK:
        return code
 
    print("Building {} against the SDK".format(mf.label(resolved)))
    return run_scons(native, jobs_args(args.jobs) + ["sdk_path=" + str(sdk)] + overrides).returncode
 
 
def cmd_resolve(args, overrides):
    if not scons_available():
        return EXIT_ERROR
    sdk = find_sdk_root(args.sdk_root)
    project = require_project(args.project)
    if sdk is None or project is None:
        return EXIT_ERROR
 
    resolved = resolve_request(project / "native", sdk, overrides)
    if resolved is None:
        return EXIT_ERROR
    check = mf.check_variant(sdk, resolved)
 
    if args.json:
        print(json.dumps({"resolved": resolved, "sdk_state": check.state}, indent=2, sort_keys=True))
        return EXIT_OK
 
    print("Request:   " + mf.label(resolved))
    print("Suffix:    " + resolved["suffix"])
    print("SDK build: " + {"ok": "available", "mismatch": "built with different settings", "missing": "not built"}[check.state])
    if check.state != "ok":
        show_variant_state(resolved, check, heading=False)
    return EXIT_OK
 
 
def cmd_sdk_build(args, overrides):
    if not scons_available():
        return EXIT_ERROR
    sdk = find_sdk_root(args.sdk_root)
    if sdk is None:
        return EXIT_ERROR
    code = build_sdk(sdk, overrides, args.jobs)
    if code == 0:
        print("SDK build finished. 'gdn sdk list' shows the variants available.")
    return code
 
 
def cell(value):
    if isinstance(value, str):
        return value
    return {True: "yes", False: "no", None: "-"}.get(value, str(value))
 
 
def cmd_sdk_list(args, overrides):
    sdk = find_sdk_root(args.sdk_root)
    if sdk is None:
        return EXIT_ERROR
    variants = mf.all_variants(sdk)
    if not variants:
        print("No SDK variants built yet. Run 'gdn sdk build'.")
        return EXIT_OK
 
    rows = [("platform", "target", "arch", "precision", "threads", "dev", "suffix")]
    for data in variants:
        options = data.get("options", {})
        rows.append((
            cell(data.get("platform")), cell(data.get("target")), cell(data.get("arch")),
            cell(options.get("precision")), cell(options.get("threads")), cell(options.get("dev_build")),
            cell(data.get("suffix")),
        ))
    widths = [max(len(row[i]) for row in rows) for i in range(len(rows[0]))]
    for row in rows:
        print("  ".join(value.ljust(widths[i]) for i, value in enumerate(row)).rstrip())
    return EXIT_OK
 
 
def cmd_options(args, overrides):
    if not scons_available():
        return EXIT_ERROR
    sdk = find_sdk_root(args.sdk_root)
    if sdk is None:
        return EXIT_ERROR
    return run_scons(sdk, ["-Q", "-h"]).returncode
 
 
def copy_without_overwriting(source, destination, skip=()):
    kept = []
    for path in sorted(source.rglob("*")):
        relative = path.relative_to(source)
        if path.is_dir() or relative.name in skip:
            continue
        target = destination / relative
        if target.exists():
            kept.append(relative)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(str(path), str(target))
    return kept
 
 
def godot_string(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'
 
 
SECTION_HEADER = re.compile(r"^\[[\w./-]+\]$")
 
 
def set_project_settings(project_file, section, values):
    """Set keys in one section of project.godot, leaving everything else as it is."""
    lines = project_file.read_text(encoding="utf-8").splitlines()
    rendered = {key: "{}={}".format(key, godot_string(value)) for key, value in values.items()}
    header = "[{}]".format(section)
 
    start = next((i for i, line in enumerate(lines) if line.strip() == header), None)
    if start is None:
        if lines and lines[-1].strip():
            lines.append("")
        lines += [header, ""] + list(rendered.values())
    else:
        end = next((i for i in range(start + 1, len(lines)) if SECTION_HEADER.match(lines[i].strip())), len(lines))
        body = lines[start + 1:end]
        remaining = dict(rendered)
        for i, line in enumerate(body):
            key = line.split("=", 1)[0].strip()
            if "=" in line and key in remaining:
                body[i] = remaining.pop(key)
        last = max((i for i, line in enumerate(body) if line.strip()), default=-1)
        body[last + 1:last + 1] = list(remaining.values())
        lines[start + 1:end] = body
 
    project_file.write_text("\n".join(lines) + "\n", encoding="utf-8")
 
 
def cmd_init(args, overrides):
    name = args.name or DEFAULT_LIBRARY_NAME
    if not LIBRARY_NAME.match(name):
        error("Invalid library name '{}'. Use letters, digits and underscores, starting with a letter.".format(name))
        return EXIT_USAGE
 
    project = Path.cwd()
    if not (project / "project.godot").is_file():
        error("Current directory is not a Godot project. Run 'gdn init' from the folder holding project.godot.")
        return EXIT_ERROR
    sdk = find_sdk_root(args.sdk_root)
    if sdk is None:
        return EXIT_ERROR
    templates = sdk / "templates"
 
    plugin_src = templates / "plugin"
    if not plugin_src.is_dir() or not (templates / "native" / "SConstruct.template").is_file():
        error("The SDK templates are incomplete: {}".format(templates))
        return EXIT_ERROR
 
    # The plugin belongs to the SDK and is refreshed, project sources belong to the user and never are.
    shutil.copytree(str(plugin_src), str(project / "addons" / "GodotNativeSDK"), dirs_exist_ok=True)
 
    native = project / "native"
    kept = copy_without_overwriting(templates / "native", native, skip=("SConstruct.template",))
    if not (native / "SConstruct").exists():
        template = (templates / "native" / "SConstruct.template").read_text(encoding="utf-8")
        (native / "SConstruct").write_text(template.replace("@LIBRARY_NAME@", name), encoding="utf-8")
        print("Library name: {}".format(name))
    elif args.name:
        print("native/SConstruct already exists, so the name '{}' was not applied. Edit libname in that file.".format(args.name))
 
    # The editor plugin reads these to find the SDK and the Python that has gdn installed.
    set_project_settings(project / "project.godot", "native_builder", {
        "sdk_path": sdk.as_posix(),
        "python": Path(sys.executable).as_posix(),
    })
    print("Set native_builder/sdk_path and native_builder/python in project.godot")
 
    print("Initialized {}".format(project))
    for relative in kept:
        print("  kept existing native/{}".format(relative))
    print("Next: gdn build")
    return EXIT_OK
 
 
#
# Entry point
#
 
 
def build_parser():
    parser = argparse.ArgumentParser(
        prog="gdn",
        description="Godot native SDK tool. Anything written as key=value is passed to the godot-cpp build.",
    )
    sub = parser.add_subparsers(dest="command")
 
    def command(parent, name, handler, help_text, jobs=False, yes=False, as_json=False, project=False):
        sp = parent.add_parser(name, help=help_text)
        sp.add_argument("--sdk-root", help="SDK location, default: the SDK this gdn belongs to (or GDN_SDK_ROOT)")
        if project:
            sp.add_argument("-p", "--project", help="Godot project folder, default: the one containing the current directory")
        if jobs:
            sp.add_argument("-j", "--jobs", type=int, help="parallel compile jobs")
        if yes:
            sp.add_argument("-y", "--yes", action="store_true", help="build a missing SDK variant without asking")
        if as_json:
            sp.add_argument("--json", action="store_true", help="machine readable output")
        sp.set_defaults(handler=handler)
        return sp
 
    init = command(sub, "init", cmd_init, "Initialize the current Godot project [name]")
    init.add_argument("name", nargs="?", help="name of the native library, default: " + DEFAULT_LIBRARY_NAME)
    command(sub, "build", cmd_build, "Build the project's native library [key=value ...]", jobs=True, yes=True, project=True)
    command(sub, "resolve", cmd_resolve, "Show which SDK variant a build maps to [key=value ...]", as_json=True, project=True)
    command(sub, "options", cmd_options, "List every build option the godot-cpp tool accepts")
 
    sdk = sub.add_parser("sdk", help="Manage SDK builds")
    sdk_sub = sdk.add_subparsers(dest="sdk_command")
    command(sdk_sub, "build", cmd_sdk_build, "Build an SDK variant [key=value ...]", jobs=True)
    command(sdk_sub, "list", cmd_sdk_list, "List built SDK variants")
    sdk.set_defaults(handler=lambda args, overrides: (sdk.print_help(), EXIT_OK)[1])
    return parser
 
 
TAKES_OVERRIDES = {cmd_build, cmd_resolve, cmd_sdk_build}
 
 
def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    overrides, rest = split_overrides(argv)
    parser = build_parser()
    args = parser.parse_args(rest)
 
    handler = getattr(args, "handler", None)
    if handler is None:
        parser.print_help()
        return EXIT_OK
    if overrides and handler not in TAKES_OVERRIDES:
        error("This command does not take key=value options: " + " ".join(overrides))
        return EXIT_USAGE
    return handler(args, overrides)
