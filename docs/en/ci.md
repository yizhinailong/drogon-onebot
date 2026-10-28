# Version tags, CI, and releases

English · [简体中文](../zh-CN/ci.md)

[Documentation index](README.md) · [Project home](../../README.md)

The repository has five workflows. Their logic is written directly in the YAML, with no extra scripts:

| Workflow | Purpose |
| --- | --- |
| [linux-ci.yml](../../.github/workflows/linux-ci.yml) (Linux CI) | Builds and runs the tests on Linux for branch pushes and PRs; on a version tag, also packages the release build |
| [windows-ci.yml](../../.github/workflows/windows-ci.yml) (Windows CI) | The same on Windows |
| [release.yml](../../.github/workflows/release.yml) (Release) | Runs both CI workflows on a version tag, then publishes their packages as a GitHub Release |
| [version-tag.yml](../../.github/workflows/version-tag.yml) (Create Version Tag) | Creates an annotated tag when the `VERSION` of `project()` changes, then starts Release on that tag |
| [docs.yml](../../.github/workflows/docs.yml) (Documentation) | Generates the Doxygen documentation and publishes it to GitHub Pages; see the [Doxygen guide](doxygen.md) |

From a version change to a release:

1. A push to `master` changes the `VERSION` of `project()`.
2. Create Version Tag creates the tag `v<VERSION>` and starts Release on it.
3. Release runs Linux CI and Windows CI on the tag. Each one builds and tests, then packages its release build.
4. When both pass, Release publishes the GitHub Release `v<VERSION>` with the two packages.

## Version tags

[version-tag.yml](../../.github/workflows/version-tag.yml) runs on pushes to `master` that change `CMakeLists.txt`.

It reads the `VERSION` of the first `project()` call in `CMakeLists.txt` at the commits before and after the push:

- It strips `#` line comments first, then takes the literal after `VERSION`.
- The version must have the form `major[.minor[.patch[.tweak]]]` and cannot be a variable.
- When a push contains several commits, it compares the two ends of the push.

When the version changes, it creates an annotated tag on the last commit of the push. For example, when the version goes from 0.1.0 to 0.2.0:

- the tag is `v0.2.0`, with the message `drogon-onebot 0.2.0`;
- the tagger is the GitHub Actions bot.

No tag is created when:

- The version is unchanged, and only dependencies, options, formatting, or comments changed.
- No version can be read from the commit before the push, for example when a branch is first created. That commit then serves only as the baseline.

Existing tags are never overwritten:

- When the version is already tagged on another commit, the workflow fails, and you need to pick an unused version.
- A retry of the same push reuses the existing tag and starts Release on it again.

The workflow commits no files and never bumps the version itself.

Manual runs and concurrency:

- It can be run manually on the default branch. It then compares the selected commit with its first parent; an unchanged version still creates no tag.
- For a multi-commit push, re-run the original workflow run, so the original comparison ends are kept.
- Concurrent attempts for the same commit run one after another; changes to different versions are not merged into one group.

After creating the tag, the workflow runs `gh workflow run release.yml --ref <tag>` with the built-in token. A push made with `GITHUB_TOKEN` does not start new push workflows, but it can trigger `workflow_dispatch`; see [GitHub's trigger rules](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow).

The tag job requests `contents: write` and `actions: write`, and the repository rules must allow it to create version tags. A successful version workflow only means that the tag exists and Release has started. It does not mean the tests passed or the release was published; check the Release run on that tag.

## Building and testing on Linux

[linux-ci.yml](../../.github/workflows/linux-ci.yml) is triggered by:

- a push of commits to any branch;
- a PR being opened, updated, or reopened;
- a manual run from the Actions page;
- the Release workflow, which calls it as a reusable workflow (`workflow_call`) on a version tag.

Its `push` trigger covers branches only, so pushing a tag does not start it directly; on a tag it runs as part of Release.

Run rules:

- Deleting a branch skips the tests.
- A newer run for the same ref cancels an older one that has not finished.
- Each job checks out the commit of the triggering event; for a PR, it verifies the merge commit that GitHub generates.

`debug` and `release` are two independent matrix jobs, so one failing does not stop the other from finishing. They use the `linux-debug` and `linux-release` presets, and run these steps on `ubuntu-24.04`:

1. Download the official CMake 4.2.3 Linux package and verify it against a pinned SHA-256.
2. Install LLVM 21's `clang` and `clang-format` from [apt.llvm.org](https://apt.llvm.org/), then Ninja and ccache from the Ubuntu archive.
   - Before adding the package source, check the fingerprint of the LLVM signing key.
   - Put `/usr/lib/llvm-21/bin` on `PATH`, so the presets' `clang++` resolves to LLVM 21.
3. `debug` job only: run `clang-format --dry-run -Werror` on every tracked `.cpp` and `.hpp` file, with the rules from the root `.clang-format`.
4. Shallow-clone vcpkg at the pinned commit `67972e92` (which provides drogon 1.9.13 port-version 2, the version verified locally), run `bootstrap-vcpkg.sh` and `vcpkg install drogon`, and set `VCPKG_ROOT`.
   - `actions/cache` saves vcpkg's binary cache directory.
   - The cache key includes the runner image, the vcpkg commit, the GCC version used to build the ports, and the CMake version. A change to any of them rebuilds the dependencies.
   - The cache is saved right after a successful install, so a failure in a later step does not lose it.
   - When the dependency install fails, the vcpkg build logs of that run are printed.
5. `cmake --preset`, then `cmake --build --preset`, then `ctest --preset`.
   - The test preset prints the test log on failure, and finding no tests also counts as a failure.
   - All tests run offline; see the Tests section of the [plugin guide](drogon-plugin.md).
6. On a tag, `release` job only: install the build with `cmake --install` into `drogon-onebot-<version>-linux-x64/`, add `LICENSE`, pack it as `drogon-onebot-<version>-linux-x64.tar.gz`, and upload it as the `drogon-onebot-linux-x64` artifact, kept for 7 days.

On a tag, a step after configure checks that the tag matches `CMAKE_PROJECT_VERSION` in `CMakeCache.txt`. CMake parses that value itself, independently of the text parsing in the version tag workflow.

## Building and testing on Windows

[windows-ci.yml](../../.github/workflows/windows-ci.yml) has the same triggers, run rules, and tag check as Linux CI. Its `debug` and `release` matrix jobs use the `windows-debug` and `windows-release` presets, and run these steps on `windows-2025` in PowerShell:

1. Set up the x64 MSVC environment with `ilammy/msvc-dev-cmd`. `clang-cl` needs it to find the MSVC headers and libraries.
2. Download the official CMake 4.2.3 Windows package and ccache 4.14.2, and verify each against a pinned SHA-256.
   - The presets use ccache as the compiler launcher, and the image has no ccache.
   - They go on `PATH` after the MSVC environment is set up, so this CMake comes before the older one that ships with Visual Studio.
   - The image provides LLVM (`clang-cl`) and Ninja, and the step prints their versions. The 20261021 image has LLVM 20.1.8 and Ninja 1.13.2.
3. Shallow-clone vcpkg at the same pinned commit, run `bootstrap-vcpkg.bat` and `vcpkg install drogon` with the `x64-windows-static-md` triplet (static libraries on the dynamic CRT, the same as `just init` and the `windows-base` preset), and set `VCPKG_ROOT`.
   - The binary cache works as on Linux. vcpkg builds the ports with MSVC and its own CMake, so the key includes the runner image, the triplet, the vcpkg commit, and the MSVC toolset version (`VCToolsVersion`).
4. `cmake --preset`, then `cmake --build --preset`, then `ctest --preset`, as on Linux.
5. On a tag, `release` job only: install the build into `drogon-onebot-<version>-windows-x64/`, add `LICENSE`, pack it as `drogon-onebot-<version>-windows-x64.zip`, and upload it as the `drogon-onebot-windows-x64` artifact, kept for 7 days.

Windows CI does not check formatting; the Linux `debug` job does.

## Releases

[release.yml](../../.github/workflows/release.yml) runs when a `v*` tag is pushed, and on manual runs. Create Version Tag starts it with `gh workflow run release.yml --ref <tag>`.

- It calls Linux CI and Windows CI as reusable workflows, so the packages come from the same jobs that run the tests.
- The publish job runs only on a tag that starts with `v`, after both CI workflows pass. A manual run on a branch only runs the two CI workflows.
- The publish job downloads the `drogon-onebot-*` artifacts and creates the GitHub Release `v<VERSION>`, titled `drogon-onebot <VERSION>`, with notes generated by GitHub. `--verify-tag` keeps it from creating a missing tag.
- When the release already exists, for example on a retry, the job replaces its package files (`--clobber`) and keeps the notes.
- Runs for the same tag run one after another; a new run does not cancel one in progress.
- Only the publish job gets `contents: write`; the CI jobs keep read-only access.

Each platform gets one package, with the release build only:

```text
drogon-onebot-<version>-<os>-x64/
├── include/drogon_onebot/...
├── lib/
│   ├── libdrogon-onebot.a (Linux) or drogon-onebot.lib (Windows)
│   └── cmake/drogon-onebot/   package config for find_package
└── LICENSE
```

To use a package, extract it, add the extracted directory to `CMAKE_PREFIX_PATH`, and call `find_package(drogon-onebot CONFIG REQUIRED)`. The package does not contain drogon:

- The consumer still needs drogon 1.9.13 from vcpkg; on Windows, with the `x64-windows-static-md` triplet.
- The Linux package is built on Ubuntu 24.04 with LLVM 21 against libstdc++ 14, so the consumer needs a compatible libstdc++.
- The Windows package is built with the dynamic CRT (`/MD`). A consumer's Debug build uses `/MDd` and cannot link it (`LNK2038`); build from source for Debug builds instead.

## Other conventions

- The test jobs need only read access to the repository, and `actions/checkout` does not keep the credentials.
- Third-party actions are pinned to commit SHAs, with the matching version in a comment; when updating one, check both the version and the commit.
- When upgrading CMake or ccache, update both the version and the checksum. CMake appears in both CI workflows.
- Upgrading the vcpkg commit changes the versions of drogon and the other dependencies. Verify locally on the same commit first, then update the commit in both CI workflows and the drogon version recorded in [AGENTS.md](../../AGENTS.md).

## Local checks

From the repository root:

```sh
actionlint
just test
just test release
```

Without arguments, actionlint checks every workflow in `.github/workflows/`. It checks the bash scripts with shellcheck when shellcheck is on `PATH`; it does not check the PowerShell scripts of Windows CI.
