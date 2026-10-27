# drogon-onebot

[English](README.md) · 简体中文

基于 [drogon](https://github.com/drogonframework/drogon) 的 [OneBot v11](https://github.com/botuniverse/onebot-11) 插件库，供其他 drogon 应用以插件形式使用。它实现了两部分：

- **调用动作**：OneBot v11 的 HTTP API。
- **接收事件**：HTTP POST 上报。

行为以 [NapCat](https://github.com/NapNeko/NapCatQQ) 为准测试。

所有接口都是 drogon 协程：动作返回 `drogon::Task<Result<T>>`，其中 `Result<T>` 即 `std::expected<T, Error>`；事件按 `post_type` 分发给 `OnMessage` 等方法注册的回调。

[快速开始](#快速开始) · [文档索引](docs/zh-CN/README.md) · [插件使用说明](docs/zh-CN/drogon-plugin.md) · [版本标签与 Linux CI](docs/zh-CN/ci.md) · [Doxygen 使用说明](docs/zh-CN/doxygen.md)

## 环境与依赖

| 依赖 | 版本 | 说明 |
| --- | --- | --- |
| CMake | ≥ 4.0 | 构建使用 `CMakePresets.json` 中的 `<os>-<profile>` 预设 |
| Ninja、ccache | 任意较新版本 | 预设固定使用 Ninja 生成器和 ccache 编译器启动器 |
| 编译器 | Linux 上为 `clang++`，Windows 上为 `clang-cl` | 公开头文件使用 `std::expected`，需要 C++23 |
| [vcpkg](https://github.com/microsoft/vcpkg) | classic 模式 | 提供 drogon，需设置 `VCPKG_ROOT` |
| drogon | 1.9.13 | 由 vcpkg 安装，`just init` 执行 `vcpkg install drogon` |

[Linux CI](.github/workflows/linux-ci.yml) 在 Ubuntu 24.04 上运行 debug 和 release 两套配置：

- 工具：CMake 4.2.3、LLVM 21，vcpkg 固定在提供 drogon 1.9.13 的提交；
- 内容：检查格式、构建并运行全部测试。

详见 [版本标签与 Linux CI](docs/zh-CN/ci.md)。

<a id="快速开始"></a>

## 快速开始

在仓库根目录安装依赖、构建并运行测试：

```sh
just init
just build
just test
```

`just test` 通过 CTest 运行 `tests/test_*.cpp` 编译出的全部测试程序。测试使用 drogon 自带的测试框架，需要服务端的测试由同一个 app 充当 NapCat，因此不需要网络或真实的 OneBot 实现。不使用 just 时，`cmake --workflow --preset linux-debug` 依次完成配置、构建和测试。

## 在项目中使用

源码方式（以 CPM 为例，也可以用 `add_subdirectory`）：

```cmake
CPMAddPackage(
    NAME drogon-onebot
    GIT_REPOSITORY https://github.com/yizhinailong/drogon-onebot.git
    GIT_TAG master
)

target_link_libraries(app PRIVATE drogon-onebot::drogon-onebot)
```

- `GIT_TAG` 也可以写版本标签：`CMakeLists.txt` 中 `project()` 的 `VERSION` 变化后，会自动创建 `v<VERSION>` 标签。
- 作为子项目时，测试和安装规则默认关闭。
- 也可以先执行 `just publish`，再在消费项目中用 `find_package(drogon-onebot CONFIG REQUIRED)` 引入。

在应用的 config.json 中注册插件。下例中，密钥只从环境变量读取：

```json
"plugins": [
    {
        "name": "drogon_onebot::OneBotClient",
        "config": {
            "base_url": "http://127.0.0.1:3000",
            "access_token_env": "ONEBOT_ACCESS_TOKEN",
            "event_path": "/onebot/event",
            "secret_env": "ONEBOT_SECRET"
        }
    }
]
```

下面是完整程序：把收到的私聊消息原样发回。

- 回调在 beginning advice 里注册，这时插件已经初始化，但还没开始监听。
- 事件路由先回复 204，再在后台运行回调，所以回调里可以放心等待较慢的操作。

```cpp
/**
 * @file main.cpp
 * @brief 把收到的私聊消息原样发回。
 */
#include <drogon/drogon.h>

#include <drogon_onebot/onebot_client.hpp>

/**
 * @brief 注册消息回调并启动 drogon。
 * @return drogon 退出后返回零。
 */
auto main() -> int {
    drogon::app()
        .loadConfigFile("config.json")
        .registerBeginningAdvice([] {
            auto* const plugin{ drogon::app().getPlugin<drogon_onebot::OneBotClient>() };
            plugin->OnMessage([bot = plugin->NewOneBotClient()](drogon_onebot::MessageEvent event) -> drogon::Task<> {
                if (event.message_type != "private") {
                    co_return;
                }
                auto const sent{ co_await bot.SendPrivateMsg({ .user_id = event.user_id, .message = event.message }) };
                if (!sent) {
                    LOG_ERROR << "send_private_msg failed: " << sent.error().message;
                }
            });
        })
        .run();
    return 0;
}
```

NapCat 端需要两个网络端点：

- HTTP 服务器：供本库调用动作，`token` 对应 `ONEBOT_ACCESS_TOKEN`。
- HTTP 客户端：`url` 指向 `event_path`，`token` 对应 `ONEBOT_SECRET`，`messagePostFormat` 为 `array`。

配置项、事件类型、回调的注册时机和生命周期、错误分类与线程模型见 [插件使用说明](docs/zh-CN/drogon-plugin.md)。

## 开发与文档

使用文档分为英文版（`docs/en/`）和简体中文版（`docs/zh-CN/`），两边的文件一一对应，API 标识符、命令和协议值保留原文；新增文档请在两边各写一份，并加入各自的 [文档索引](docs/zh-CN/README.md)。本文是 [README.md](README.md) 的简体中文版，修改其中一份时请同步更新另一份。开发约定见 [AGENTS.md](AGENTS.md)：

- 代码风格；
- 用 `clang-format --dry-run -Werror` 检查格式；
- 每次改动行为时同步更新文档。

安装 Doxygen 后，执行 `just docs` 生成 API 文档，首页为 `build/doxygen/html/index.html`。推送到 `master` 时，Documentation 工作流会自动生成文档并发布到 GitHub Pages。首次启用所需的设置见 [Doxygen 使用说明](docs/zh-CN/doxygen.md)。

## 许可证

采用 [MIT 许可证](LICENSE)。
