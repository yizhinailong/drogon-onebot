# AGENTS.md

## Project state

Drogon plugin library for the OneBot v11 protocol, consumed by other Drogon apps (e.g. `../assistantbot`). It implements the HTTP API (calling actions) and HTTP POST reporting (receiving events), tested against NapCat's behavior. WebSocket transports are not implemented.

- The library target is `drogon-onebot` (alias `drogon-onebot::drogon-onebot`). It is static by default; pass `-DBUILD_SHARED_LIBS=ON` for a shared build.
- Public headers live in `include/drogon_onebot/` and are installed through the `HEADERS` file set. Consumers include `<drogon_onebot/...>`. Private headers go in `src/`, which is only on the library's own include path.
- The plugin class is `drogon_onebot::OneBotClient` (`drogon::Plugin<OneBotClient>`). Apps register it by class name under `plugins` in their config.json. Drogon calls `initAndStart(config)` inside `app().run()`, before beginning advices, so `app().getPlugin<drogon_onebot::OneBotClient>()` only works after `run()` starts.
- Registration relies on drogon's static `DrObject<T>::alloc_` initializer. With the static library, the linker only pulls in `onebot_client.o` when the app references the class. If the app lists the plugin in config.json but never calls `getPlugin<OneBotClient>()` (or otherwise uses the class), drogon logs `Plugin drogon_onebot::OneBotClient undefined!` and keeps running without it.
- Plugin config keys:
  - `base_url` (the OneBot implementation's HTTP API, e.g. `http://127.0.0.1:3000`) is required.
  - `event_path` (e.g. `/onebot/event`) is optional and must start with `/`; without it no event route is registered.
  - `access_token_env` and `secret_env` are optional, and each names an environment variable. Secrets are never read from config.json. The first holds the HTTP API access token (sent as `Authorization: Bearer`); the second holds the secret that signs posted events.

  `initAndStart` throws `std::invalid_argument` if `base_url` is empty, if `event_path` is invalid, or if a named variable is unset or empty. Add new keys to `OneBotConfig`, `initAndStart`, the class doc comment, `config.json`, and the table in `docs/drogon-plugin.md`.
- `OneBotClient::NewOneBotClient()` returns a `OneBot` by value. `OneBot` is a value handle whose only state is one `std::shared_ptr<details::Transport>`, so copies are cheap and go into coroutines by value. Its methods are coroutines returning `drogon::Task<Result<T>>` (`std::expected<T, Error>`, in `error.hpp`); there is no sync API.
- `OneBot` methods mirror OneBot v11 action names (`SendPrivateMsg` calls `send_private_msg`), as the common OneBot SDKs (NoneBot2, aiocqhttp) do, so there are no API group members. Request and response structs live in `types/`, one header per category; the method bodies live in `src/actions/<category>.cpp`. `CallAction(CallActionParams)` reaches any action, including implementation extensions, and yields the envelope's raw `data`.
- The typed actions `GetLoginInfo`, `SendPrivateMsg`, and `SendGroupMsg` call `details::Transport::Call` and read their result from `data` with the helpers in `src/details/json.hpp`. A `data` without the expected field (including the null `data` of an `async` reply) is `ResponseValidation`, with `data` as the error body. Integers are read with `details::read_integer<T>`, which accepts JSON numbers and decimal strings, because NapCat sends some IDs as strings.
- Messages use array format in both directions: `Message` is a `std::vector<MessageSegment>`, each segment a `type` plus a `Json::Value data`. CQ-code strings and `auto_escape` are not supported.
- Coroutines take their parameters by value, and every `OneBot` method copies `m_transport` into its frame before the first `co_await`. Keep both rules in new actions.

### Transport

`details::Transport::Call(action, params, timeout)` implements the OneBot v11 HTTP API (`communication/http.md` and `authorization.md` in the botuniverse/onebot-11 spec):

- It sends `POST <base_url>/<action>` with the parameters as a JSON body, plus `Authorization: Bearer <access_token>` when the token is non-empty. A path in `base_url` becomes a prefix.
- It creates a new `HttpClient` for each request on the calling thread's loop, or on the main loop when the thread runs no loop, and keeps it in the coroutine frame until the response arrives. Don't cache one client for reuse: drogon's `HttpClient` defaults to pipelining depth 0, so a shared client would serialize concurrent requests.
- Failure mapping:
  - A `drogon::HttpException` with `ReqResult::Timeout` is `Timeout`; other codes are `Connection`.
  - A non-2xx status (the spec uses 400, 401, 403, 404, and 406; NapCat answers a bad token with 403) is `Status`.
  - A 2xx body that is not JSON, or has no `status` string, is `ResponseValidation`.
  - The spec answers every other case with HTTP 200, so a failed action arrives as `"status": "failed"` and maps to `Action` with the envelope's `retcode` and its `message` (or `wording`) text. NapCat reports an unknown action this way too, with `retcode` 200.
  - `ok` and `async` both succeed and yield `data`, which is null for `async`.

### Events

`OneBotClient::initAndStart` creates a `details::EventReceiver` (`src/details/event_receiver.*`). When `event_path` is set, it registers a `POST` route there whose lambda shares that receiver.

- Registering a route from `initAndStart` works because drogon initializes plugins before routers (`HttpAppFrameworkImpl.cc:663-671`; `registerHttpController` asserts `!routersInit_`). Drogon's own `PromExporter` does the same (`PromExporter.cc:18`).
- The wire format follows NapCat's HTTP client (`packages/napcat-onebot/network/http-client.ts`): a JSON body, `x-self-id`, and, when its `token` is set, `x-signature: sha1=<hex HMAC-SHA1 of the body keyed by the token>`.
  - `EventReceiver` implements HMAC-SHA1 on `trantor::utils::sha1`, so the library needs no direct OpenSSL dependency.
  - It compares the hex digits case-insensitively and in constant time.
- The route answers before any handler runs:
  - 401 when a secret is set and `X-Signature` is missing; 403 when it does not match.
  - 400 with a text reason when the body is not a JSON object, a required field is missing, an integer field is not an integer, or `message` is not a segment array (NapCat's `messagePostFormat: string`).
  - 204 otherwise. NapCat keeps its request open until the response, and handlers may await for seconds (an LLM call), so the response never waits for them. This rules out quick operations (a `reply` in the response body).
  - Unknown `post_type` values, such as NapCat's `message_sent`, get 204 and are dropped.
- Dispatch is by `post_type`: `OnMessage`, `OnNotice`, `OnRequest`, and `OnMetaEvent` (aiocqhttp's names) append to per-type handler lists.
  - `MessageEvent` has typed fields. The other three have only `time`, `self_id`, their type, `sub_type`, and the whole event in `raw`.
  - Each handler gets its own `drogon::async_run` task. The task's lambda copies the handler and the event, and `async_run` moves that lambda into its coroutine frame (`coroutine.h:749-762`), so a handler's captures outlive the request.
  - The task catches and logs exceptions, because an exception escaping an `AsyncTask` calls `std::terminate` (`coroutine.h:397-401`).
- Handlers must be registered before the listeners start: in a beginning advice, or in `initAndStart` of a plugin that lists this one under `dependencies`. The lists are read from IO threads without a lock. `On*` logs a warning when `event_path` is not set.
- `On*` must not be called before `initAndStart` has run, because `m_receiver` is created there. That is always true once `getPlugin` works.

## Consuming the library

Keep both ways working when you change `CMakeLists.txt`.

- From source: `add_subdirectory(<path>)`, or CPM with `SOURCE_DIR`/`GIT_REPOSITORY`. `PROJECT_IS_TOP_LEVEL` is false then, so `DROGON_ONEBOT_BUILD_TESTS` and `DROGON_ONEBOT_INSTALL` default to OFF, and neither the tests nor `enable_testing()` reach the consumer. `../assistantbot` with `-DCPM_drogon-onebot_SOURCE=<this checkout>` builds the library with its `-std=gnu++26`.
- Installed: `just publish` installs into `publish/`. Consumers set `CMAKE_PREFIX_PATH` to that directory and call `find_package(drogon-onebot CONFIG REQUIRED)`. The package config calls `find_dependency(Drogon CONFIG)`, so consumers don't need their own `find_package(Drogon)`. Version compatibility is `SameMinorVersion`.
- Either way, link `drogon-onebot::drogon-onebot`. It carries `Drogon::Drogon` publicly.

## Tests

The tests use drogon's own framework, `drogon/drogon_test.h`, which is installed with drogon, so the tests add no dependency.

- `tests/CMakeLists.txt` globs `tests/test_*.cpp` (non-recursive, `CONFIGURE_DEPENDS`) and builds each file into its own executable named after the file stem (`build/<preset>/tests/test_events`). Adding a `tests/test_*.cpp` file is enough. Removing one leaves its old binary until `just clean`.
- Each executable is registered with CTest: `WORKING_DIRECTORY` is the repo root (for `config.json`), `TIMEOUT` is 60, and `RESOURCE_LOCK config_json_listener` serializes them under `ctest -j`, because the app-backed tests share the listener port.
- The executables take drogon's test options: `-r <name>` runs one `DROGON_TEST`, `-s` prints passing checks, and `-l` lists the tests.
- `tests/support.hpp` holds the shared helpers:
  - `run_tests_with_app` follows drogon's `lib/tests/main_CookieSameSite.cc`. It runs `app().run()` on a background thread and waits for a beginning advice plus `LISTEN_DELAY`, because listeners start after the advices. Then it runs the tests on the main thread and quits the app.
  - The test thread runs no event loop, so tests may block on `drogon::sync_wait` (actions) or `HttpClient::sendRequest` (`post_event`).
  - `sendRequestCoro` returns an awaiter, not an awaitable, so `sync_wait` rejects it.
  - `Inbox<T>` passes events from handlers on IO threads to the test thread.
- `config.json` (tracked, no secrets) is the shared fixture. It listens on `127.0.0.1:18080`, which `base_url` points at, and sets `event_path` `/onebot/event` and `secret_env` `DROGON_ONEBOT_TEST_SECRET`. The tests set that variable themselves.

| Test | What it covers |
| --- | --- |
| `test_config` | Calls `initAndStart` on test-built plugins without running the app: required keys, both `_env` keys, `event_path` validation, and the tracked `config.json`. |
| `test_actions` | Serves a fake NapCat HTTP API. Covers `CallAction`'s body, `Authorization`, and path prefix; `async`; every `ErrorKind`; and the typed actions, including missing `data` fields and passed-through `Action` errors. The 404 and non-JSON cases log drogon JSON `Syntax error` lines; that is expected. |
| `test_events` | Exercises the signed event route. RFC 2202 vectors check the test's own HMAC, and one NapCat body carries a signature computed independently with Python. Also covers uppercase signatures, string IDs, the 401/403/400 rejections with no handler run, all four event types, `message_sent` being dropped, several handlers per event, a throwing handler, and the response not waiting for a slow handler. |
| `test_events_unsigned` | Loads `config.json` without `secret_env` (`app().loadConfigJson`) and checks that unsigned events are accepted and any `X-Signature` is ignored. |

## Commands

`just` drives the build. Recipes map onto CMake presets named `<os>-<profile>` (`linux-debug`, `linux-release`, `windows-debug`, `windows-release`), with build trees in `build/<preset>/`.

- `just init`: `vcpkg install drogon` (classic mode, no `vcpkg.json`; `VCPKG_ROOT` must be set).
- `just build [debug|release]`: configure only if `build/<preset>/CMakeCache.txt` is missing, then build, then copy `compile_commands.json` to `build/`. After a failed configure, run `just configure debug --fresh`.
- `just test [profile]`: build, then `ctest --preset <os>-<profile>` (output on failure; no tests is an error).
- `just run <target> [profile]`: build everything, then run `build/<os>-<profile>/tests/<target>`, e.g. `just run test_events`.
- `just publish [profile]`: build (default `release`) and `cmake --install` into `publish/`.
- `just clean [profile]`: delete `build/<os>-<profile>/`, or `build/` and `publish/` when no profile is given.
- Without just: `cmake --workflow --preset linux-debug` configures, builds, and tests.

## Build notes

- Requires CMake >= 4.0, Ninja, and ccache (hard-wired as `CMAKE_CXX_COMPILER_LAUNCHER`). Compiler is `clang++` on Linux, `clang-cl` on Windows.
- Don't set `CMAKE_CXX_STANDARD` here. The library declares `target_compile_features(PUBLIC cxx_std_23)` (the public headers use `std::expected`), which is exported to `find_package` consumers.
- Drogon 1.9.13's `FindFilesystem.cmake` leaves an unset `CMAKE_CXX_STANDARD` defined as `""`, which `target_compile_features` rejects. Both `CMakeLists.txt` and `cmake/drogon-onebot-config.cmake.in` unset the empty value right after finding Drogon. Keep those blocks.
- Sources and public headers are collected with `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`, so adding a file triggers a re-configure on the next build.

## Code style

New C++ code follows the `cpp-style-ref` skill (CMake + vcpkg variant). Drogon's virtual overrides (`initAndStart`, `shutdown`) keep their upstream names.

Format with `clang-format -i <files>` (root `.clang-format`). Check with `clang-format --dry-run -Werror <files>`. clang-format regroups includes: in `src/actions/*.cpp`, `drogon_onebot/onebot_client.hpp` is not treated as the main header, so it sorts after `details/...`.
