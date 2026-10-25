# drogon 插件：`drogon_onebot::OneBotClient`

本文整理 drogon-onebot 作为 drogon 插件的接入方式、配置、生命周期、使用方法，以及与 drogon 线程模型相关的设计约束。协议是 OneBot v11，当前只实现了 HTTP API（主动调用动作），接收事件尚未设计。

## 概览

`drogon_onebot::OneBotClient` 继承 `drogon::Plugin<OneBotClient>`，职责只有两件：

1. 在 drogon 启动时从 config.json 读取并校验 OneBot 实现的 HTTP API 地址（`OneBotConfig`）。
2. 通过 `NewOneBotClient()` 生成绑定该配置的 `OneBot` 客户端。

真正调用动作的是 `OneBot`，方法名与 OneBot v11 的动作名一一对应（`SendGroupMsg` ↔ `send_group_msg`）：

```cpp
auto const bot{ drogon::app().getPlugin<drogon_onebot::OneBotClient>()->NewOneBotClient() };
auto const info{ co_await bot.CallAction({ .action = "get_login_info" }) };
```

## 接入

库需要 C++23（公开头文件使用 `std::expected`），并通过 `drogon-onebot::drogon-onebot` 传递 `Drogon::Drogon` 依赖，消费方不需要再 `find_package(Drogon)`。两种方式任选：

```cmake
# 源码方式
add_subdirectory(path/to/drogon-onebot)

# 安装方式：先在 drogon-onebot 中执行 just publish，再把 publish/ 加进 CMAKE_PREFIX_PATH
find_package(drogon-onebot CONFIG REQUIRED)

target_link_libraries(app PRIVATE drogon-onebot::drogon-onebot)
```

## 注册与配置

```json
"plugins": [
    {
        "name": "drogon_onebot::OneBotClient",
        "config": {
            "base_url": "http://127.0.0.1:3000",
            "access_token_env": "ONEBOT_ACCESS_TOKEN"
        }
    }
]
```

| 配置项 | 必填 | 说明 |
|---|---|---|
| `base_url` | 是 | OneBot 实现（NapCat、Lagrange.OneBot 等）的 HTTP API 地址，可带路径前缀。动作发往 `<base_url>/<action>`。 |
| `access_token_env` | 否 | 存放 access token 的**环境变量名**。省略时不发送 `Authorization` 头。 |

- access token 永远不写进 config.json，只从 `access_token_env` 指定的环境变量读取，以 `Authorization: Bearer <token>` 发送，需与 OneBot 实现配置的 token 一致。
- `base_url` 缺失或为空，或设置了 `access_token_env` 但该环境变量未设置或为空时，`initAndStart` 抛 `std::invalid_argument`。异常从 `app().run()` 抛出，不捕获时程序终止。

## 生命周期

```
app().loadConfigFile("config.json")       读取 plugins 配置段
app().run()
 ├─ OneBotClient::initAndStart(config)    解析并校验配置，失败时抛异常
 ├─ beginning advices                     从这里开始 getPlugin<OneBotClient>() 可用
 ├─ startListening()                      各 IO loop 异步开始监听
 └─ 事件循环运行，处理请求
app().quit()                              drogon 调用 OneBotClient::shutdown()
```

- `getPlugin` 返回 `OneBotClient*`，由 drogon 持有，不要 delete。
- 静态链接时，应用代码必须引用 `OneBotClient`（调用一次 `getPlugin<drogon_onebot::OneBotClient>()` 即可），否则链接器丢掉 `onebot_client.o`，drogon 打印 `Plugin drogon_onebot::OneBotClient undefined!` 后继续运行。

## 使用

- `OneBot` 是值句柄，内部唯一的共享状态是一个 `std::shared_ptr<details::Transport>`。拷贝只是引用计数加一，传进协程时按值传递。
- 每个方法返回 `drogon::Task<Result<T>>`，`Result<T>` 是 `std::expected<T, Error>`。动作失败**不会抛异常**。
- 消息使用数组格式：`Message` 即 `std::vector<MessageSegment>`，每段是 `type` 加 `Json::Value data`。暂不支持 CQ 码字符串和 `auto_escape`。

| 方法 | 动作 | 状态 |
|---|---|---|
| `CallAction(CallActionParams)` | 任意动作，含实现扩展 | 可用，返回响应的 `data` |
| `GetLoginInfo(GetLoginInfoParams)` | `get_login_info` | TODO，目前返回 `Action` 错误 |
| `SendPrivateMsg(SendPrivateMsgParams)` | `send_private_msg` | TODO，目前返回 `Action` 错误 |
| `SendGroupMsg(SendGroupMsgParams)` | `send_group_msg` | TODO，目前返回 `Action` 错误 |

## 错误处理

OneBot v11 的 HTTP API 只在请求本身有问题时返回非 200（400、401、403、404、406），动作执行失败也返回 200，靠响应体里的 `status` 区分。

| `ErrorKind` | 含义 | `status_code` | `retcode` | `body` |
|---|---|---|---|---|
| `Connection` | 没有收到响应：DNS、TLS、连接被拒等 | 0 | 0 | 空 |
| `Timeout` | 在 `timeout` 秒内没有收到响应 | 0 | 0 | 空 |
| `Status` | 非 2xx：未带 token（401）、token 不符（403）、动作不存在（404）等 | HTTP 状态码 | 0 | 原始响应体 |
| `ResponseValidation` | 2xx 响应不是 JSON，或没有 `status` 字符串 | 0 | 0 | 响应内容 |
| `Action` | 响应的 `status` 为 `failed` | 0 | 响应的 `retcode` | 响应内容 |

`status` 为 `ok` 或 `async` 都算成功；`async` 时 `data` 为 null，执行结果无法获知。`message` 会带上响应里的 `message`（没有时用 `wording`），这两个字段是 NapCat 等实现的扩展，v11 标准只有 `status`、`retcode`、`data`。

## 线程与事件循环

- 只提供协程接口。drogon 文档建议优先使用异步接口或协程，同步 API 会阻塞 IO 线程，在 client 所在的 loop 上同步等待还会死锁。
- `Transport::Call` 为每个请求在调用线程的事件循环上新建 `HttpClient`（调用线程没有 loop 时用主 loop），并在响应到达前一直留在协程帧里。drogon 的 `HttpClient` 默认 pipelining 深度为 0，共享一个 client 会让并发请求串行执行。
- 协程按值接收参数；`OneBot::CallAction` 在第一次 `co_await` 之前把 `m_transport` 拷进协程帧，即使 `OneBot` 在请求途中被销毁，请求也能完成。

## 已知限制

- 只实现了 HTTP API 的主动调用。接收事件（消息、通知、请求、元事件）需要 HTTP POST 上报、正向 WebSocket 或反向 WebSocket 之一，尚未设计。
- 类型化动作只有三个，且是 TODO 存根；其余动作先用 `CallAction`。

## 测试程序

| 程序 | 作用 |
|---|---|
| `tests/test_config.cpp` | 加载 config.json，打印插件配置后退出。`just run test_config` |
| `tests/test_actions.cpp` | 由同一个 app 提供假的 OneBot HTTP API，通过 `CallAction` 检查成功、`failed`（`Action`，retcode 100）和不存在的动作（`Status` 404）三种结果，不需要网络。`just run test_actions` |
