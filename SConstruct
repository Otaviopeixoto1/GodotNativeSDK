#!/usr/bin/env python
import json
import os
import sys

from methods import print_error
from gdn import manifest as gdn_manifest
from gdn import scons_support



sdk_root = Dir(".").srcnode().abspath
sys.path.insert(0, sdk_root)

EnsureSConsVersion(4, 0)

sdk_version = "0.1"
libname = "GodotNativeSDK"
godot_cpp_dir = os.path.join(sdk_root, "godot-cpp")

localEnv = Environment(tools=["default"], PLATFORM="")

# Build profiles can be used to decrease compile times.
# You can either specify "disabled_classes", OR
# explicitly specify "enabled_classes" which disables all other classes.
# Modify the example file as needed and pass build_profile=<absolute path> to scons or gdn.

customs = [os.path.abspath("custom_overrides.py")]

if not (os.path.isdir(godot_cpp_dir) and os.listdir(godot_cpp_dir)):
    print_error("""godot-cpp is not available within this folder, as Git submodules haven't been initialized.
Run the following command to download godot-cpp:

    git submodule update --init --recursive""")
    Exit(1)

# Every option comes from the godot-cpp tool. Reject arguments it does not know, because SCons
# would otherwise ignore a typo such as plaftorm=linux and build for the wrong target
declared_options, unknown = scons_support.probe_declared_options(localEnv, godot_cpp_dir, customs)
if unknown:
    print_error("Unknown build option(s): {}. Run 'gdn options' to list the valid ones.".format(", ".join(sorted(unknown))))
    Exit(2)

# godot-cpp SConstruct resolves every default including: platform, arch and target. This will also build the godot-cpp static lib.
env = SConscript("godot-cpp/SConstruct", {"env": localEnv, "customs": customs})

# godot-cpp tries to compile as C++17 and EnTT needs C++20, so replace the flag in the environment it builds with.
scons_support.use_cxx_standard(env)

missing = scons_support.missing_compiler(env)
if missing and not GetOption("help"):
    print_error("The compiler '{}' for platform '{}' was not found on PATH.".format(missing, env["platform"]))
    Exit(gdn_manifest.EXIT_TOOLCHAIN)

env.Append(CPPPATH=["include/"])
sources = Glob("src/*.cpp")
sources += Glob("src/ecs/*.cpp")
sources += Glob("src/ecs/debug/*.cpp")

# Third party sources, each assumed to be entirely in its own subfolder under thirdparty/.
env.Append(CPPPATH=["thirdparty/"])
sources += Glob("thirdparty/*/*.cpp")
for entry in Glob("thirdparty/*"):  # TODO: only add folders that hold headers
    env.Append(CPPPATH=[str(entry)])

# Build docs
if env["target"] in ["editor", "template_debug"]:
    try:
        doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
        sources.append(doc_data)
    except AttributeError:
        print("Not including class reference as we're targeting a pre-4.3 baseline.")

# The full suffix keeps variants that differ only in .dev or .universal from overwriting each other.
suffix = env["suffix"]

lib_filename = "{}{}{}{}".format(env.subst("$LIBPREFIX"), libname, suffix, env.subst("$LIBSUFFIX"))
library = env.StaticLibrary(
    "bin/{}/{}".format(env["platform"], lib_filename),
    source=sources,
)

# godot-cpp hardcodes the "lib" prefix for its own static library but the suffix is still dynamic:
godot_cpp_library = env.File(os.path.join(godot_cpp_dir, "bin", "libgodot-cpp" + suffix + env.subst("$LIBSUFFIX")))


def relative_to_sdk(path):
    return os.path.relpath(path, sdk_root).replace(os.sep, "/")


include_dirs = []
for entry in env["CPPPATH"]:
    path = relative_to_sdk(env.Dir(entry).srcnode().abspath)
    if path not in include_dirs:
        include_dirs.append(path)

# The manifest is a build product of the two libraries, so it only appears once both exist.
manifest_data = scons_support.resolved_config(env, declared_options)
manifest_data.update({
    "library_name": libname,
    "sdk_version": sdk_version,
    "godot_cpp_commit": scons_support.get_git_commit(godot_cpp_dir),
    # Link order matters here: the SDK lib depends on the godot-cpp lib so it must come last.
    "libraries": [relative_to_sdk(library[0].abspath), relative_to_sdk(godot_cpp_library.abspath)],
    "include_dirs": include_dirs,
})

manifest = env.Command(
    "bin/{}/manifest{}.json".format(env["platform"], suffix),
    [library, godot_cpp_library, env.Value(json.dumps(manifest_data, sort_keys=True))],
    Action(scons_support.write_manifest, "Writing manifest $TARGET"),
)

Default(manifest)
