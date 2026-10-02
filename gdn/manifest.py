"""Variant manifests: what an SDK variant was built with, and how a request is compared to it.

Assumed static layout inside the SDK root:

    bin/<platform>/lib<name><suffix>.a        the SDK static library
    bin/<platform>/manifest<suffix>.json      one manifest per built variant
    godot-cpp/bin/libgodot-cpp<suffix>.a      the godot-cpp static library

<suffix> must be the string computed by godot-cpp from platform, target, arch, precision, dev_build, threads and so on. It is unique per variant by construction.
"""

import json
from collections import namedtuple
from pathlib import Path

SCHEMA = 1

# Exit codes the SConstruct files use, so the CLI can tell the failures apart.
EXIT_VARIANT_MISSING = 40
EXIT_VARIANT_MISMATCH = 41
EXIT_TOOLCHAIN = 44

# Options that cannot change the compiled output. Everything else counts, including options a
# future godot-cpp adds, so the failure mode is an extra rebuild prompt, never a silent mismatch.
IGNORED_OPTIONS = frozenset({"verbose", "compiledb", "compiledb_file", "generate_bindings"})

Variant = namedtuple("Variant", "state built diffs closest")


def manifest_path(sdk_root, platform, suffix):
    return Path(sdk_root) / "bin" / platform / "manifest{}.json".format(suffix)


def load(path):
    try:
        with open(str(path), "r") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return None
    if not isinstance(data, dict) or data.get("schema") != SCHEMA:
        return None
    return data


def all_variants(sdk_root):
    found = []
    for path in sorted((Path(sdk_root) / "bin").glob("*/manifest*.json")):
        data = load(path)
        if data is not None:
            found.append(data)
    return found


def libraries_present(sdk_root, built):
    return all((Path(sdk_root) / rel).is_file() for rel in built.get("libraries", []))


def differences(requested, built):
    """Map of option name to (requested, built) for every significant difference."""
    diffs = {}
    req = requested.get("options", {})
    blt = built.get("options", {})
    for key in sorted(set(req) | set(blt)):
        if key in IGNORED_OPTIONS:
            continue
        if req.get(key) != blt.get(key):
            diffs[key] = (req.get(key), blt.get(key))

    req_defines = set(requested.get("defines", []))
    blt_defines = set(built.get("defines", []))
    if req_defines != blt_defines:
        diffs["defines"] = (sorted(req_defines - blt_defines), sorted(blt_defines - req_defines))
    return diffs


def check_variant(sdk_root, resolved):
    """Compare a resolved request with what the SDK has built.

    state is "ok" (usable), "mismatch" (same suffix, different settings, so a rebuild would
    replace it) or "missing" (nothing built, or its libraries are gone from disk).
    """
    built = load(manifest_path(sdk_root, resolved["platform"], resolved["suffix"]))
    if built is not None and not libraries_present(sdk_root, built):
        built = None

    state = "missing"
    diffs = {}
    if built is not None:
        diffs = differences(resolved, built)
        state = "mismatch" if diffs else "ok"

    closest = []
    if state != "ok":
        for other in all_variants(sdk_root):
            if other.get("suffix") == resolved["suffix"]:
                continue
            closest.append((other, differences(resolved, other)))
        closest.sort(key=lambda item: len(item[1]))

    return Variant(state, built, diffs, closest[:3])


def label(config):
    options = config.get("options", {})
    text = "{} / {} / {}".format(config.get("platform"), config.get("target"), config.get("arch"))
    extras = []
    if options.get("precision") == "double":
        extras.append("double precision")
    if options.get("dev_build"):
        extras.append("dev build")
    if options.get("threads") is False:
        extras.append("no threads")
    if extras:
        text += " ({})".format(", ".join(extras))
    return text


def describe_difference(key, pair):
    if key == "defines":
        requested_only, built_only = pair
        parts = []
        if requested_only:
            parts.append("requested only: " + ", ".join(requested_only))
        if built_only:
            parts.append("built only: " + ", ".join(built_only))
        return "defines ({})".format("; ".join(parts))
    return "{}: requested {!r}, built {!r}".format(key, pair[0], pair[1])
