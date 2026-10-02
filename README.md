# GodotNativeSDK

A reusable C++ SDK for building reloadable Godot GDExtensions with project-specific native code.

The SDK is built once per variant (platform, target, arch, and so on) into static libraries. A project's
`native/` folder is then compiled into the shared library Godot loads, linking those static libraries.

The `gdn` command line tool handles the building process. Build options are never listed in `gdn`: anything written as
`key=value` is passed to the godot-cpp build, which decides what is valid and what the defaults are.

## Setup

Clone with godot-cpp in `godot-cpp/` (git submodule), then install the CLI in editable mode:

```bash
python -m pip install -e .
```

## Use

From the root of a Godot project:

```bash
gdn init                      # scaffold native/ and the editor plugin
gdn build                     # build the project library
gdn build target=template_release
gdn resolve                   # show which SDK variant a build needs, without building
gdn options                   # list every option godot-cpp accepts
```

`gdn build` checks that the SDK has the variant the build needs. If it does not, it shows what differs and
asks before building it. Without a terminal it exits with code 42 unless `--yes` is given.

Build the SDK directly with `gdn sdk build [key=value ...]` and list what exists with `gdn sdk list`.

## Notes

* Path-valued options such as `build_profile` must be absolute.
* One manifest per SDK variant is written to `bin/<platform>/manifest<suffix>.json`, only after a successful build.
* The installed library is replaced by rename, so a running editor never sees a half-written file.
* `gdn init` never overwrites files in `native/`. Delete `native/SConstruct` and re-run it to pick up a newer template.
* Exit codes from the build scripts: 40 variant missing, 41 variant mismatch, 44 toolchain missing.
