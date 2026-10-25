# AGENTS.md

## Project state

Drogon plugin library for the OneBot v11 protocol, consumed by other Drogon apps (e.g. `../assistant-bot`). It is a skeleton: the plugin, the configuration, the HTTP transport, and `CallAction` work; the typed actions are stubs, and receiving events is not designed yet.

- The library target is `drogon-onebot` (alias `drogon-onebot::drogon-onebot`). It is static by default; pass `-DBUILD_SHARED_LIBS=ON` for a shared build.
- Public headers live in `include/drogon_onebot/` and are installed through the `HEADERS` file set. Consumers include `<drogon_onebot/...>`. Private headers go in `src/`, which is only on the library's own include path.
- The plugin class is `drogon_onebot::OneBotClient` (`drogon::Plugin<OneBotClient>`). Apps register it by class name under `plugins` in their config.json. Drogon calls `initAndStart(config)` inside `app().run()`, before beginning advices, so `app().getPlugin<drogon_onebot::OneBotClient>()` only works after `run()` starts.
- Registration relies on drogon's static `DrObject<T>::alloc_` initializer. With the static library, the linker only pulls in `onebot_client.o` when the app references the class. If the app lists the plugin in config.json but never calls `getPlugin<OneBotClient>()` (or otherwise uses the class), drogon logs `Plugin drogon_onebot::OneBotClient undefined!` and keeps running without it.
- Plugin config keys: `base_url` (the OneBot implementation's HTTP API, e.g. `http://127.0.0.1:3000`) is required, and `initAndStart` throws `std::invalid_argument` if it is empty. The access token is never read from config.json. The optional `access_token_env` names the environment variable that holds it, and `initAndStart` throws if that variable is unset or empty. Without `access_token_env`, `access_token` stays empty and no `Authorization` header is sent. Add new keys to `OneBotConfig`, `initAndStart`, the class doc comment, `config.json`, and the table in `docs/drogon-plugin.md`.
- `OneBotClient::NewOneBotClient()` returns a `OneBot` by value. `OneBot` is a value handle whose only state is one `std::shared_ptr<details::Transport>`, so copies are cheap and go into coroutines by value. Its methods are coroutines returning `drogon::Task<Result<T>>` (`std::expected<T, Error>`, in `error.hpp`); there is no sync API.
- `OneBot` methods mirror OneBot v11 action names (`SendPrivateMsg` calls `send_private_msg`), as the common OneBot SDKs (NoneBot2, aiocqhttp) do, so there are no API group members. Request and response structs live in `types/`, one header per action category; the method bodies live in `src/actions/<category>.cpp`. `CallAction(CallActionParams)` reaches any action, including implementation extensions, and yields the envelope's raw `data`.
- `GetLoginInfo`, `SendPrivateMsg`, and `SendGroupMsg` are declared and documented, but their bodies are `TODO` stubs that return an `ErrorKind::Action` error. Implement them on top of `details::Transport::Call`.
- Messages are sent in array format: `Message` is a `std::vector<MessageSegment>`, each segment a `type` plus a `Json::Value data`. CQ-code strings and `auto_escape` are not supported.
- Coroutines take their parameters by value, and `OneBot::CallAction` copies `m_transport` into its frame before the first `co_await`. Keep both rules in new actions.

### Transport

`details::Transport::Call(action, params, timeout)` implements the OneBot v11 HTTP API (`communication/http.md` and `authorization.md` in the botuniverse/onebot-11 spec):

- It sends `POST <base_url>/<action>` with the parameters as a JSON body, plus `Authorization: Bearer <access_token>` when the token is non-empty. A path in `base_url` becomes a prefix.
- It creates a new `HttpClient` for each request on the calling thread's loop, or on the main loop when the thread runs no loop, and keeps it in the coroutine frame until the response arrives. Don't cache one client for reuse: drogon's `HttpClient` defaults to pipelining depth 0, so a shared client would serialize concurrent requests.
- Failure mapping: a `drogon::HttpException` with `ReqResult::Timeout` is `Timeout`, other codes are `Connection`. A non-2xx status (the spec uses 400, 401, 403, 404, and 406) is `Status`. A 2xx body that is not JSON or has no `status` string is `ResponseValidation`. The spec answers every other case with HTTP 200, so a failed action arrives as `"status": "failed"` and maps to `Action` with the envelope's `retcode` and its `message` (or `wording`) text. `ok` and `async` both succeed and yield `data`, which is null for `async`.

## Consuming the library

Keep both ways working when you change `CMakeLists.txt`.

- From source: `add_subdirectory(<path>)`, or CPM with `SOURCE_DIR`/`GIT_REPOSITORY`. `PROJECT_IS_TOP_LEVEL` is false then, so `DROGON_ONEBOT_BUILD_TESTS` and `DROGON_ONEBOT_INSTALL` default to OFF.
- Installed: `just publish` installs into `publish/`. Consumers set `CMAKE_PREFIX_PATH` to that directory and call `find_package(drogon-onebot CONFIG REQUIRED)`. The package config calls `find_dependency(Drogon CONFIG)`, so consumers don't need their own `find_package(Drogon)`. Version compatibility is `SameMinorVersion`.
- Either way, link `drogon-onebot::drogon-onebot`. It carries `Drogon::Drogon` publicly.

## Tests

`tests/CMakeLists.txt` globs `tests/test_*.cpp` (non-recursive, `CONFIGURE_DEPENDS`) and builds each file into its own executable named after the file stem (`tests/test_config.cpp` → `build/<preset>/tests/test_config`). Adding a `tests/test_*.cpp` file is enough. They are plain programs that report through their exit code: there is no test framework and no CTest registration.

- `tests/test_config.cpp` loads `config.json` relative to the working directory (`just run` uses the repo root), prints the plugin's settings from a beginning advice, then calls `app().quit()`.
- `tests/test_actions.cpp` serves a fake OneBot HTTP API from the same app (the listener in `config.json`, which `base_url` points at) and calls `CallAction` three times: `get_login_info` succeeds, `delete_msg` fails with `Action` and `retcode` 100, and `no_such_action` fails with `Status` 404. It runs offline. It waits `LISTEN_DELAY` first: beginning advices run before `startListening()`, so an immediate request fails with `Bad server address`. The 404 run logs a drogon JSON `Syntax error` line, because the transport tries to read an error message from drogon's HTML 404 page.

## Commands

`just` drives the build. Recipes map onto CMake presets named `<os>-<profile>` (`linux-debug`, `linux-release`, `windows-debug`, `windows-release`), with build trees in `build/<preset>/`.

- `just init`: `vcpkg install drogon` (classic mode, no `vcpkg.json`; `VCPKG_ROOT` must be set).
- `just build [debug|release]`: configure only if `build/<preset>/CMakeCache.txt` is missing, then build, then copy `compile_commands.json` to `build/`. After a failed configure, run `just configure debug --fresh`.
- `just run <target> [profile]`: build everything, then run `build/<os>-<profile>/tests/<target>`, e.g. `just run test_actions`.
- `just publish [profile]`: build (default `release`) and `cmake --install` into `publish/`.
- `just clean [profile]`: delete `build/<os>-<profile>/`, or `build/` and `publish/` when no profile is given.

## Build notes

- Requires CMake >= 4.0, Ninja, and ccache (hard-wired as `CMAKE_CXX_COMPILER_LAUNCHER`). Compiler is `clang++` on Linux, `clang-cl` on Windows.
- Don't set `CMAKE_CXX_STANDARD` here. The library declares `target_compile_features(PUBLIC cxx_std_23)` (the public headers use `std::expected`), which is exported to `find_package` consumers.
- Drogon 1.9.13's `FindFilesystem.cmake` leaves an unset `CMAKE_CXX_STANDARD` defined as `""`, which `target_compile_features` rejects. Both `CMakeLists.txt` and `cmake/drogon-onebot-config.cmake.in` unset the empty value right after finding Drogon. Keep those blocks.
- Sources and public headers are collected with `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`, so adding a file triggers a re-configure on the next build.

## Code style

New C++ code follows the `cpp-style-ref` skill (CMake + vcpkg variant). Drogon's virtual overrides (`initAndStart`, `shutdown`) keep their upstream names.

Format with `clang-format -i <files>` (root `.clang-format`). Check with `clang-format --dry-run -Werror <files>`.
