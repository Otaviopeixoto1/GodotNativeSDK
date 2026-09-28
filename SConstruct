#!/usr/bin/env python
import os
import sys
import json

from SCons.Variables import BoolVariable
from methods import print_error

sdk_version = 0.1
libname = "GodotNativeSDK"
projectdir = "test-project" #TODO: take as argument
installdir = "{}/addons/{}".format(projectdir, libname)

localEnv = Environment(tools=["default"], PLATFORM="")

# Build profiles can be used to decrease compile times.
# You can either specify "disabled_classes", OR
# explicitly specify "enabled_classes" which disables all other classes.
# Modify the example file as needed and uncomment the line below or
# manually specify the build_profile parameter when running SCons.

# localEnv["build_profile"] = "build_profile.json"

customs = ["custom_overrides.py"]
customs = [os.path.abspath(path) for path in customs]

opts = Variables(customs, ARGUMENTS)
opts.Add(BoolVariable("setup", "Run SDK setup", False))

opts.Update(localEnv)
Help(opts.GenerateHelpText(localEnv))

env = localEnv.Clone()

if not (os.path.isdir("godot-cpp") and os.listdir("godot-cpp")):
    print_error("""godot-cpp is not available within this folder, as Git submodules haven't been initialized.
Run the following command to download godot-cpp:

    git submodule update --init --recursive""")
    sys.exit(1)

env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})

# Store the godot-cpp include directories exposed by its SCons environment.
godot_cpp_root = env.Dir("godot-cpp").srcnode().abspath
godot_cpp_includes = []

for include_dir in env.get("CPPPATH", []):
    include_dir = env.Dir(include_dir).srcnode()

    if not include_dir.abspath.startswith(godot_cpp_root + os.sep):
        continue

    relative_path = os.path.relpath(include_dir.abspath, godot_cpp_root)

    if relative_path in ("include", os.path.join("gen", "include")):
        godot_cpp_includes.append(include_dir)



env.Append(CPPPATH=["include/"])
sources = Glob("src/*.cpp")

# Third party sources, each assumed to be entirely in its own subfolder under thirdparty/.
# TODO: implement recursive glob to really get all files...
sources += Glob("thirdparty/*/*.cpp")
for entry in Glob("thirdparty/*"): # ----------------------> FIX THIS TO ONLY FETCH .h files
    env.Append(CPPPATH=[str(entry)])

# Build docs
if env["target"] in ["editor", "template_debug"]:
    try:
        doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
        sources.append(doc_data)
    except AttributeError:
        print("Not including class reference as we're targeting a pre-4.3 baseline.")

# .dev doesn't inhibit compatibility, so we don't need to key it.
# .universal just means "compatible with all relevant arches" so we don't need to key it.
suffix = env['suffix'].replace(".dev", "").replace(".universal", "")

lib_filename = "{}{}{}{}".format(env.subst("$LIBPREFIX"), libname, suffix, env.subst("$LIBSUFFIX"))
library = env.StaticLibrary(
    "bin/{}/{}".format(env['platform'], lib_filename),
    source=sources,
)

#
# Installing Library inside the project
#
copy = env.Install("{}/bin/{}/".format(installdir, env["platform"]), library)


#
# Installing templates inside the project
#
installed_templates = []


template_plugin_files = Glob("templates/plugin/*")
installed_templates += env.Install(installdir, template_plugin_files)

# Install the .gdextension template file (later it will be rewritten according to the native SConstruct)
template_bin_files = Glob("templates/gdextension/*")
installed_templates += env.Install(installdir, template_bin_files)

# Install the SDK include headers
include_files = Glob("include/*")
installed_templates += env.Install("{}/include".format(installdir), include_files)

# Install godot-cpp tools and include headers
installed_templates += env.Install("{}/godot-cpp/tools".format(installdir), Glob("godot-cpp/tools/*.py"))
installed_templates += env.Install("{}/godot-cpp".format(installdir), Glob("godot-cpp/*.py"))
installed_templates += env.Install("{}/godot-cpp".format(installdir), File("templates/godot-cpp/SConscript"))
installed_templates += env.Install("{}/godot-cpp/gdextension".format(installdir), Glob("godot-cpp/gdextension/*.h"))

# Install godot-cpp compiled library (this must be linked in the user SConstruct file)
godot_cpp_lib = env.File("godot-cpp/bin/libgodot-cpp{}{}".format(env["suffix"], env["LIBSUFFIX"]))
installed_templates += env.Install("{}/bin/{}/".format(installdir, env["platform"]), godot_cpp_lib)


#includes:
for include_dir in godot_cpp_includes:
    source_root = include_dir.srcnode().abspath
    relative_root = os.path.relpath(source_root, godot_cpp_root)

    for root, dirs, files in os.walk(source_root):
        relative_dir = os.path.relpath(root, godot_cpp_root)

        target_dir = os.path.join(installdir, "godot-cpp", relative_dir)

        for filename in files:
            source_file = env.File(os.path.join(root, filename))
            target_file = os.path.join(target_dir, filename)
            installed_templates += env.InstallAs(target_file, source_file)


# Install all the /native templates
template_native_files = Glob("templates/native/*")
for template_file in template_native_files:
    filename = str(template_file)

    if filename.endswith("SConstruct.template"):
        sconstruct_output = "{}/native/SConstruct".format(projectdir)

        # NEVER overwrite an existing project SConstruct
        if not os.path.exists(sconstruct_output):
            os.makedirs(os.path.dirname(sconstruct_output),exist_ok=True)
            installed_sconstruct = env.InstallAs(sconstruct_output, template_file)
            installed_templates += installed_sconstruct
        continue

    # Only build all the other native templates if the setup argument is passed as true
    if env["setup"]:
        installed_templates += env.Install("{}/native".format(projectdir), template_file)


# Generate/update SDK configuration to godot_native_sdk.json
sdk_config_file = "{}/native/godot_native_sdk.json".format(projectdir)

# Load existing file
if os.path.isfile(sdk_config_file):
    with open(sdk_config_file, "r") as f:
        sdk_config = json.load(f)
else:
    sdk_config = {
        "version": sdk_version,
        "root": "../addons/{}".format(libname),
        "include": "include",
        "platforms": {},
    }

sdk_config["version"] = sdk_version
sdk_config["root"] = "../addons/{}".format(libname)
sdk_config["include"] = "include"
sdk_config["library_name"] = libname


#
# TODO: FIX THE JSON TO ALSO SUPPORT MULTIPLE ARCH BUILDS PER PLATFORM !
#

# Add or update the current platform
abi_names = ["precision", "threads", "dev_build", "use_static_cpp", "use_mingw"]  # some only exist on some platforms
options = {n: env[n] for n in abi_names if n in env}
options["use_hot_reload"] = bool(env.use_hot_reload)

sdk_config.setdefault("platforms", {})
sdk_config["platforms"].setdefault(env["platform"], {})[env["target"]] = {
    "library_dir": "bin/{}".format(env["platform"]),
    "libraries": [lib_filename, os.path.basename(str(godot_cpp_lib))],  #apparently order matters, dependents first
    "arch": env["arch"],
    "options": options,
}

os.makedirs(os.path.dirname(sdk_config_file), exist_ok=True)
with open(sdk_config_file, "w") as f:
    json.dump(sdk_config, f, indent=4)
    f.write("\n")


installed_sdk_config = File(sdk_config_file)
installed_templates.append(installed_sdk_config)



default_args = [library, copy] + installed_templates
Default(*default_args)
