# drogon-onebot

English · [简体中文](README.zh-CN.md)

A [OneBot v11](https://github.com/botuniverse/onebot-11) plugin library for [drogon](https://github.com/drogonframework/drogon), used as a plugin by other drogon apps. It implements three parts:

- **Calling actions**: the OneBot v11 HTTP API.
- **Receiving events**: HTTP POST reporting.
- **Both over one connection**: the reverse WebSocket, which the OneBot implementation connects to.

Its behavior is tested against [NapCat](https://github.com/NapNeko/NapCatQQ).

Every API is a drogon coroutine. Actions return `drogon::Task<Result<T>>`, where `Result<T>` is `std::expected<T, Error>`. Events are dispatched by `post_type` to the callbacks registered with `OnMessage` and the other `On*` methods.

[Quick start](#quick-start) · [Documentation index](docs/en/README.md) · [Plugin guide](docs/en/drogon-plugin.md) · [Version tags, CI, and releases](docs/en/ci.md) · [Doxygen guide](docs/en/doxygen.md)

## Requirements

| Dependency | Version | Notes |
| --- | --- | --- |
| CMake | ≥ 4.0 | The build uses the `<os>-<profile>` presets in `CMakePresets.json` |
| Ninja, ccache | Any recent version | The presets always use the Ninja generator and the ccache compiler launcher |
| Compiler | `clang++` on Linux, `clang-cl` on Windows | The public headers use `std::expected`, which needs C++23 |
| [vcpkg](https://github.com/microsoft/vcpkg) | Classic mode | Provides drogon; `VCPKG_ROOT` must be set |
| drogon | 1.9.13 | Installed by vcpkg; `just init` runs `vcpkg install drogon` |

[Linux CI](.github/workflows/linux-ci.yml) and [Windows CI](.github/workflows/windows-ci.yml) each run a debug and a release configuration, on Ubuntu 24.04 and Windows Server 2025:

- Tools: CMake 4.2.3, LLVM 21 on Linux and the runner image's LLVM on Windows, with vcpkg pinned to the commit that provides drogon 1.9.13.
- Steps: check the formatting (on Linux), build, and run all tests.
- On a version tag, the [Release](.github/workflows/release.yml) workflow runs both and publishes their release builds as a GitHub Release.

See [Version tags, CI, and releases](docs/en/ci.md).

<a id="quick-start"></a>

## Quick start

From the repository root, install the dependencies, build, and run the tests:

```sh
just init
just build
just test
```

`just test` uses CTest to run every test program built from `tests/test_*.cpp`. The tests use drogon's own test framework. The tests that need a server use the same app as a stand-in for NapCat, so they need neither network access nor a real OneBot implementation. Without just, `cmake --workflow --preset linux-debug` configures, builds, and tests in one go.

## Using it in a project

From source (CPM shown here; `add_subdirectory` works too):

```cmake
CPMAddPackage(
    NAME drogon-onebot
    GIT_REPOSITORY https://github.com/yizhinailong/drogon-onebot.git
    GIT_TAG master
)

target_link_libraries(app PRIVATE drogon-onebot::drogon-onebot)
```

- `GIT_TAG` can also name a version tag. When `VERSION` in the `project()` call of `CMakeLists.txt` changes, a `v<VERSION>` tag is created automatically.
- As a subproject, the tests and install rules are off by default.
- Alternatively, run `just publish` first, then use `find_package(drogon-onebot CONFIG REQUIRED)` in the consuming project.
- Each version also has prebuilt Linux and Windows packages of the release build on its GitHub Release. Extract one and add it to `CMAKE_PREFIX_PATH` for `find_package`; drogon still comes from vcpkg. See the Releases section of [Version tags, CI, and releases](docs/en/ci.md) for the requirements.

Register the plugin in the app's config.json. In this example the secrets are read only from environment variables:

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

Here is a complete program that sends every private message back to its sender:

- The callback is registered in a beginning advice. At that point the plugin is initialized, but the listeners have not started yet.
- The event route answers 204 first and then runs the callbacks in the background, so a callback can safely await slow operations.

```cpp
/**
 * @file main.cpp
 * @brief Send every private message back to its sender.
 */
#include <drogon/drogon.h>

#include <drogon_onebot/onebot_client.hpp>

/**
 * @brief Register the message callback and start drogon.
 * @return Zero after drogon exits.
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

NapCat needs two network endpoints:

- An HTTP server, which this library calls actions on. Its `token` matches `ONEBOT_ACCESS_TOKEN`.
- An HTTP client whose `url` points at `event_path`, whose `token` matches `ONEBOT_SECRET`, and whose `messagePostFormat` is `array`.

With the reverse WebSocket instead, set `ws_path` in place of `base_url` and `event_path`, and give NapCat one WebSocket client whose `url` points at `ws_path`, whose `token` matches `ONEBOT_ACCESS_TOKEN`, and whose `messagePostFormat` is `array`. Then raise `client_max_websocket_message_size` in the `app` section of config.json, because drogon closes a connection on any message above 128 KiB by default.

The [plugin guide](docs/en/drogon-plugin.md) covers the configuration keys, the reverse WebSocket, the event types, when to register callbacks and how long they live, the error kinds, and the threading model.

## Development and documentation

The user documentation comes in English (`docs/en/`) and Simplified Chinese (`docs/zh-CN/`), with matching file names, and API identifiers, commands, and protocol values left as they are. Write a new page in both languages and add it to each [documentation index](docs/en/README.md). [README.zh-CN.md](README.zh-CN.md) is the Simplified Chinese version of this file; when you change one, update the other. The development conventions are in [AGENTS.md](AGENTS.md):

- the code style;
- checking the formatting with `clang-format --dry-run -Werror`;
- updating the documentation with every behavior change.

With Doxygen installed, `just docs` generates the API documentation. Its home page is `build/doxygen/html/index.html`. On pushes to `master`, the Documentation workflow generates the documentation and publishes it to GitHub Pages. The [Doxygen guide](docs/en/doxygen.md) describes the settings needed the first time.

## License

Released under the [MIT License](LICENSE).
