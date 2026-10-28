# 版本标签、CI 与发布

[English](../en/ci.md) · 简体中文

[文档索引](README.md) · [项目首页](../../README.zh-CN.md)

仓库有五个工作流，逻辑都直接写在 YAML 中，不依赖额外脚本：

| 工作流 | 作用 |
| --- | --- |
| [linux-ci.yml](../../.github/workflows/linux-ci.yml)（Linux CI） | 在分支推送和 PR 上于 Linux 构建并运行测试；在版本标签上还会打包 release 构建 |
| [windows-ci.yml](../../.github/workflows/windows-ci.yml)（Windows CI） | 在 Windows 上做同样的事 |
| [release.yml](../../.github/workflows/release.yml)（Release） | 在版本标签上运行两个 CI 工作流，再把它们的包发布为 GitHub Release |
| [version-tag.yml](../../.github/workflows/version-tag.yml)（Create Version Tag） | `project()` 的 `VERSION` 变化时创建带注释的标签，并在该标签上启动 Release |
| [docs.yml](../../.github/workflows/docs.yml)（Documentation） | 生成 Doxygen 文档并发布到 GitHub Pages，见 [Doxygen 使用说明](doxygen.md) |

从版本变化到发布：

1. 推送到 `master` 的提交修改了 `project()` 的 `VERSION`。
2. Create Version Tag 创建标签 `v<VERSION>`，并在该标签上启动 Release。
3. Release 在该标签上运行 Linux CI 和 Windows CI，两者各自构建、测试，再打包 release 构建。
4. 两者都通过后，Release 发布 GitHub Release `v<VERSION>`，附带两个包。

## 版本标签

[version-tag.yml](../../.github/workflows/version-tag.yml) 在推送到 `master` 且修改了 `CMakeLists.txt` 时运行。

它读取推送前后两个提交中 `CMakeLists.txt` 第一个 `project()` 调用的 `VERSION`：

- 读取时先去掉 `#` 行注释，再取 `VERSION` 后面的字面量。
- 版本号格式为 `major[.minor[.patch[.tweak]]]`，不能是变量。
- 一次推送包含多个提交时，比较推送的两个端点。

版本变化时，在推送的最终提交上创建带注释的标签。例如版本从 0.1.0 变成 0.2.0：

- 标签为 `v0.2.0`，注释为 `drogon-onebot 0.2.0`；
- 创建者为 GitHub Actions 机器人。

不打标签的情况：

- 版本不变，只改了依赖、选项、格式或注释。
- 推送前的提交读不出版本，例如首次创建分支。这时它只作为比较基准。

已有标签不会被覆盖：

- 版本号已标记在其他提交上时，工作流失败，需要改用未占用的版本号。
- 重试同一次推送时复用已有标签，并再次在该标签上启动 Release。

工作流不提交文件，也不会自动递增版本。

关于手动运行和并发：

- 支持在默认分支手动运行，此时比较所选提交及其第一父提交；版本没变仍不打标签。
- 多提交推送应重跑原工作流，以保留原始比较端点。
- 同一提交的并发尝试串行执行，不同版本的变更不会合并成一组。

创建标签后，工作流用内置令牌执行 `gh workflow run release.yml --ref <tag>`。原因是 `GITHUB_TOKEN` 发起的推送不会启动新的 push 工作流，但可以触发 `workflow_dispatch`，见 [GitHub 触发规则](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/trigger-a-workflow)。

标签任务请求 `contents: write` 和 `actions: write`，仓库规则必须允许它创建版本标签。版本工作流成功只表示标签存在、且已启动 Release，不能据此判断测试通过或已经发布；要看该标签上的 Release 运行结果。

## Linux 构建和测试

[linux-ci.yml](../../.github/workflows/linux-ci.yml) 的触发方式：

- 向任意分支推送提交；
- 创建、更新或重新打开 PR；
- 从 Actions 页面手动运行；
- 由 Release 工作流在版本标签上作为可复用工作流（`workflow_call`）调用。

它的 `push` 触发只覆盖分支，推送标签不会直接启动它；在标签上它只作为 Release 的一部分运行。

运行规则：

- 删除分支时跳过测试。
- 同一引用的重复运行会取消较早、尚未完成的运行。
- 每个任务检出触发事件对应的提交；PR 验证 GitHub 生成的合并提交。

`debug` 和 `release` 是两个独立的矩阵任务，一个失败不影响另一个完成验证。它们分别使用 `linux-debug` 和 `linux-release` 预设，在 `ubuntu-24.04` 上依次执行以下步骤：

1. 下载 CMake 4.2.3 官方 Linux 包，按固定的 SHA-256 校验。
2. 从 [apt.llvm.org](https://apt.llvm.org/) 安装 LLVM 21 的 `clang` 和 `clang-format`，再从 Ubuntu 软件源安装 Ninja 和 ccache。
   - 添加软件源前，先核对 LLVM 签名密钥的指纹。
   - 把 `/usr/lib/llvm-21/bin` 加入 `PATH`，让预设中的 `clang++` 指向 LLVM 21。
3. 仅 `debug` 任务：对所有被跟踪的 `.cpp` 和 `.hpp` 执行 `clang-format --dry-run -Werror`，规则来自根目录的 `.clang-format`。
4. 浅克隆 vcpkg 的固定提交 `67972e92`（提供 drogon 1.9.13 port-version 2，与本地验证的版本一致），执行 `bootstrap-vcpkg.sh` 和 `vcpkg install drogon`，并设置 `VCPKG_ROOT`。
   - vcpkg 的二进制缓存目录由 `actions/cache` 保存。
   - 缓存键包含运行器镜像、vcpkg 提交、构建 port 用的 GCC 版本和 CMake 版本，其中任一项变化都会重建依赖。
   - 缓存在安装成功后立即保存，所以后续步骤失败也不影响缓存。
   - 依赖安装失败时，输出本次的 vcpkg 构建日志。
5. `cmake --preset`，然后 `cmake --build --preset`，最后 `ctest --preset`。
   - test 预设在失败时输出测试日志，没有找到测试时也算失败。
   - 测试全部离线运行，见 [插件使用说明](drogon-plugin.md) 的“测试”一节。
6. 在标签上且仅 `release` 任务：用 `cmake --install` 把构建安装到 `drogon-onebot-<version>-linux-x64/`，加入 `LICENSE`，打包为 `drogon-onebot-<version>-linux-x64.tar.gz`，并上传为 `drogon-onebot-linux-x64` 构建产物，保留 7 天。

在标签上运行时，配置之后会检查标签与 `CMakeCache.txt` 中的 `CMAKE_PROJECT_VERSION` 一致。这个值由 CMake 自己解析，不依赖版本标签工作流用的文本解析。

## Windows 构建和测试

[windows-ci.yml](../../.github/workflows/windows-ci.yml) 的触发方式、运行规则和标签检查与 Linux CI 相同。它的 `debug` 和 `release` 矩阵任务分别使用 `windows-debug` 和 `windows-release` 预设，在 `windows-2025` 上用 PowerShell 依次执行以下步骤：

1. 用 `ilammy/msvc-dev-cmd` 设置 x64 MSVC 环境，`clang-cl` 需要它来找到 MSVC 的头文件和库。
2. 下载 CMake 4.2.3 官方 Windows 包和 ccache 4.14.2，分别按固定的 SHA-256 校验。
   - 预设用 ccache 作编译器启动器，而镜像没有 ccache。
   - 它们在 MSVC 环境设置之后加入 `PATH`，所以这个 CMake 排在 Visual Studio 自带的旧版 CMake 前面。
   - LLVM（`clang-cl`）和 Ninja 使用镜像自带的版本，该步骤会输出它们的版本。20261021 版镜像中是 LLVM 20.1.8 和 Ninja 1.13.2。
3. 浅克隆同一个固定的 vcpkg 提交，执行 `bootstrap-vcpkg.bat`，用 `x64-windows-static-md` triplet（静态库、动态 CRT，与 `just init` 和 `windows-base` 预设一致）执行 `vcpkg install drogon`，并设置 `VCPKG_ROOT`。
   - 二进制缓存的做法与 Linux 相同。vcpkg 用 MSVC 和它自己的 CMake 构建 port，所以缓存键包含运行器镜像、triplet、vcpkg 提交和 MSVC 工具集版本（`VCToolsVersion`）。
4. 与 Linux 相同，`cmake --preset`，然后 `cmake --build --preset`，最后 `ctest --preset`。
5. 在标签上且仅 `release` 任务：把构建安装到 `drogon-onebot-<version>-windows-x64/`，加入 `LICENSE`，打包为 `drogon-onebot-<version>-windows-x64.zip`，并上传为 `drogon-onebot-windows-x64` 构建产物，保留 7 天。

Windows CI 不检查格式，格式由 Linux 的 `debug` 任务检查。

## 发布

[release.yml](../../.github/workflows/release.yml) 在推送 `v*` 标签时运行，也可以手动运行。Create Version Tag 用 `gh workflow run release.yml --ref <tag>` 启动它。

- 它把 Linux CI 和 Windows CI 作为可复用工作流调用，所以发布的包来自运行测试的同一批任务。
- 发布任务只在以 `v` 开头的标签上运行，并且要等两个 CI 工作流都通过。在分支上手动运行时，只会运行两个 CI 工作流。
- 发布任务下载 `drogon-onebot-*` 构建产物，创建 GitHub Release `v<VERSION>`，标题为 `drogon-onebot <VERSION>`，说明由 GitHub 自动生成。`--verify-tag` 保证它不会自己创建缺失的标签。
- Release 已存在时（例如重试），发布任务替换其中的包文件（`--clobber`），保留原有说明。
- 同一标签的多次运行依次执行，新的运行不会取消正在进行的运行。
- 只有发布任务拥有 `contents: write`，CI 任务仍是只读权限。

每个平台一个包，只含 release 构建：

```text
drogon-onebot-<version>-<os>-x64/
├── include/drogon_onebot/...
├── lib/
│   ├── libdrogon-onebot.a（Linux）或 drogon-onebot.lib（Windows）
│   └── cmake/drogon-onebot/   供 find_package 使用的包配置
└── LICENSE
```

使用时解压，把解压出的目录加入 `CMAKE_PREFIX_PATH`，再调用 `find_package(drogon-onebot CONFIG REQUIRED)`。包里不含 drogon：

- 使用方仍需从 vcpkg 安装 drogon 1.9.13；Windows 上要用 `x64-windows-static-md` triplet。
- Linux 包在 Ubuntu 24.04 上用 LLVM 21 和 libstdc++ 14 构建，使用方需要兼容的 libstdc++。
- Windows 包使用动态 CRT（`/MD`）。使用方的 Debug 构建用 `/MDd`，链接时会报 `LNK2038`；Debug 构建请改用源码方式。

## 其他约定

- 测试任务只需要仓库只读权限，`actions/checkout` 不保留凭据。
- 第三方 action 固定到提交 SHA，并在注释中保留对应版本；更新时同时核对版本与提交。
- 升级 CMake 或 ccache 时，必须同时更新版本与校验和。CMake 在两个 CI 工作流中都有。
- 升级 vcpkg 提交会改变 drogon 等依赖的版本。应先在本地用同一提交验证，再同步更新两个 CI 工作流中的提交和 [AGENTS.md](../../AGENTS.md) 中记录的 drogon 版本。

## 本地验证

在仓库根目录执行：

```sh
actionlint
just test
just test release
```

不带参数时，actionlint 检查 `.github/workflows/` 中的全部工作流。shellcheck 在 `PATH` 上时，它会用 shellcheck 检查 bash 脚本；Windows CI 的 PowerShell 脚本不在检查范围内。
