# drogon 插件：`drogon_onebot::OneBotClient`

[English](../en/drogon-plugin.md) · 简体中文

本文整理 drogon-onebot 作为 drogon 插件的接入方式、配置、生命周期和使用方法，以及与 drogon 线程模型相关的设计约束。协议是 OneBot v11，实现了两部分：

- 主动调用动作：HTTP API，`OneBot` 客户端。
- 接收事件：HTTP POST 上报，插件注册的事件路由。

对接的实现以 NapCat 为准。

## 概览

`drogon_onebot::OneBotClient` 继承 `drogon::Plugin<OneBotClient>`，负责三件事：

1. 在 drogon 启动时从 config.json 读取并校验配置（`OneBotConfig`）。
2. 配置了 `event_path` 时，在该路径注册 `POST` 路由，接收 OneBot 实现上报的事件，并分发给 `OnMessage` 等方法注册的回调。
3. 通过 `NewOneBotClient()` 生成绑定该配置的 `OneBot` 客户端，用来调用动作，例如回复消息。

`OneBot` 的方法名与 OneBot v11 的动作名一一对应（`SendGroupMsg` ↔ `send_group_msg`），注册回调的方法名沿用 aiocqhttp（`on_message` ↔ `OnMessage`）：

```cpp
auto* const plugin{ drogon::app().getPlugin<drogon_onebot::OneBotClient>() };
plugin->OnMessage([bot = plugin->NewOneBotClient()](drogon_onebot::MessageEvent event) -> drogon::Task<> {
    if (event.message_type == "private") {
        co_await bot.SendPrivateMsg({ .user_id = event.user_id, .message = event.message });
    }
});
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
            "access_token_env": "ONEBOT_ACCESS_TOKEN",
            "event_path": "/onebot/event",
            "secret_env": "ONEBOT_SECRET"
        }
    }
]
```

| 配置项 | 必填 | 说明 |
|---|---|---|
| `base_url` | 是 | OneBot 实现的 HTTP API 地址，可带路径前缀。动作发往 `<base_url>/<action>`。 |
| `access_token_env` | 否 | 存放 access token 的**环境变量名**。省略时不发送 `Authorization` 头。 |
| `event_path` | 否 | 接收事件的路由路径，必须以 `/` 开头。省略时不注册路由，插件只能发不能收。 |
| `secret_env` | 否 | 存放上报签名密钥的**环境变量名**。省略时不校验签名，接受未签名的事件。 |

- 两个密钥都不写进 config.json，只从环境变量读取。
- access token 以 `Authorization: Bearer <token>` 发送，需与 OneBot 实现 HTTP API 的 token 一致。
- secret 用来校验每个事件的 `X-Signature: sha1=<HMAC-SHA1(secret, body) 的十六进制>`。
- 出现以下任一情况时，`initAndStart` 抛 `std::invalid_argument`：
  - `base_url` 缺失或为空；
  - `event_path` 不以 `/` 开头；
  - 设置了 `access_token_env` 或 `secret_env`，但对应的环境变量未设置或为空。

  异常从 `app().run()` 抛出，不捕获时程序终止。

### NapCat 配置

NapCat 的 OneBot 配置（`onebot11_<QQ号>.json`）需要两个网络端点：

- HTTP 服务器：供本库调用动作。
- HTTP 客户端：把事件 POST 给本插件。

```json
"network": {
    "httpServers": [
        { "name": "api", "enable": true, "host": "127.0.0.1", "port": 3000, "messagePostFormat": "array", "token": "<ONEBOT_ACCESS_TOKEN 的值>" }
    ],
    "httpClients": [
        { "name": "bot", "enable": true, "url": "http://127.0.0.1:8080/onebot/event", "messagePostFormat": "array", "reportSelfMessage": false, "token": "<ONEBOT_SECRET 的值>" }
    ]
}
```

- `httpServers[].token` 是 access token：NapCat 校验 `Authorization: Bearer` 头，不符时返回 403。
- `httpClients[].token` 是上报签名的密钥：NapCat 用它对请求体计算 HMAC-SHA1，并放进 `x-signature` 头。
- 两个 token 可以不同，分别对应 `access_token_env` 和 `secret_env`。
- 两个端点的 `messagePostFormat` 都设为 `array`：
  - HTTP 客户端的设置作用于上报的事件。设为 `string` 时 `message` 是 CQ 码字符串，事件路由会以 400 拒收。
  - HTTP 服务器的设置作用于 `get_msg` 返回的 `message`。设为 `string` 时 `GetMsg` 以 `ResponseValidation` 失败。

## 生命周期

```
app().loadConfigFile("config.json")       读取 plugins 配置段
app().run()
 ├─ OneBotClient::initAndStart(config)    解析并校验配置，注册 event_path 路由，失败时抛异常
 ├─ 初始化路由                             此后不能再注册路由
 ├─ beginning advices                     从这里开始 getPlugin<OneBotClient>() 可用，在这里注册回调
 ├─ startListening()                      各 IO loop 异步开始监听，事件开始到达
 └─ 事件循环运行，处理请求
app().quit()                              drogon 调用 OneBotClient::shutdown()
```

- `getPlugin` 返回 `OneBotClient*`，由 drogon 持有，不要 delete。
- 事件路由在 `initAndStart` 里注册。drogon 先初始化插件、再初始化路由（`HttpAppFrameworkImpl.cc:663-671`），drogon 自带的 `PromExporter` 插件也是这样注册路由的（`PromExporter.cc:18`）。
- 静态链接时，应用代码必须引用 `OneBotClient`（调用一次 `getPlugin<drogon_onebot::OneBotClient>()` 即可），否则链接器丢掉 `onebot_client.o`，drogon 打印 `Plugin drogon_onebot::OneBotClient undefined!` 后继续运行。

## 调用动作

- `OneBot` 是值句柄，内部唯一的共享状态是一个 `std::shared_ptr<details::Transport>`。拷贝只是引用计数加一，传进协程时按值传递。
- 每个方法返回 `drogon::Task<Result<T>>`，`Result<T>` 是 `std::expected<T, Error>`。动作失败**不会抛异常**。
- 消息使用数组格式：`Message` 即 `std::vector<MessageSegment>`，每段是 `type` 加 `Json::Value data`。暂不支持 CQ 码字符串和 `auto_escape`。

| 方法 | 动作 | 返回 |
|---|---|---|
| `CallAction(CallActionParams)` | 任意动作，含实现扩展 | 响应的 `data`；`async` 时为 null |
| `GetLoginInfo(GetLoginInfoParams)` | `get_login_info` | `LoginInfo`（`user_id`、`nickname`） |
| `GetStrangerInfo(GetStrangerInfoParams)` | `get_stranger_info` | `StrangerInfo`（`user_id`、`nickname`、`sex`、`age`） |
| `SendLike(SendLikeParams)` | `send_like` | 无（`Result<void>`） |
| `GetFriendList(GetFriendListParams)` | `get_friend_list` | `std::vector<FriendInfo>`（`user_id`、`nickname`、`remark`） |
| `SendPrivateMsg(SendPrivateMsgParams)` | `send_private_msg` | `SentMessage`（`message_id`） |
| `SendGroupMsg(SendGroupMsgParams)` | `send_group_msg` | `SentMessage`（`message_id`） |
| `SendMsg(SendMsgParams)` | `send_msg` | `SentMessage`（`message_id`） |
| `DeleteMsg(DeleteMsgParams)` | `delete_msg` | 无（`Result<void>`） |
| `GetMsg(GetMsgParams)` | `get_msg` | `MessageInfo`（`time`、`message_type`、`message_id`、`real_id`、`group_id`、`sender`、`message`、`raw_message`） |
| `GetForwardMsg(GetForwardMsgParams)` | `get_forward_msg` | `ForwardMessage`（`messages`，每条是一个 `MessageInfo`） |
| `GetGroupInfo(GetGroupInfoParams)` | `get_group_info` | `GroupInfo`（`group_id`、`group_name`、`member_count`、`max_member_count`） |
| `GetGroupList(GetGroupListParams)` | `get_group_list` | `std::vector<GroupInfo>` |
| `GetGroupMemberInfo(GetGroupMemberInfoParams)` | `get_group_member_info` | `GroupMemberInfo`（`card`、`role`、`level`、`title` 及标准里的其他字段） |
| `GetGroupMemberList(GetGroupMemberListParams)` | `get_group_member_list` | `std::vector<GroupMemberInfo>` |
| `GetGroupHonorInfo(GetGroupHonorInfoParams)` | `get_group_honor_info` | `GroupHonorInfo`（`current_talkative` 和五个 `GroupHonorMember` 荣誉列表） |
| `SetGroupKick(SetGroupKickParams)` | `set_group_kick` | 无（`Result<void>`） |
| `SetGroupBan(SetGroupBanParams)` | `set_group_ban` | 无（`Result<void>`） |
| `SetGroupWholeBan(SetGroupWholeBanParams)` | `set_group_whole_ban` | 无（`Result<void>`） |
| `SetGroupAdmin(SetGroupAdminParams)` | `set_group_admin` | 无（`Result<void>`） |
| `SetGroupCard(SetGroupCardParams)` | `set_group_card` | 无（`Result<void>`） |
| `SetGroupName(SetGroupNameParams)` | `set_group_name` | 无（`Result<void>`） |
| `SetGroupLeave(SetGroupLeaveParams)` | `set_group_leave` | 无（`Result<void>`） |
| `SetGroupSpecialTitle(SetGroupSpecialTitleParams)` | `set_group_special_title` | 无（`Result<void>`） |
| `SetFriendAddRequest(SetFriendAddRequestParams)` | `set_friend_add_request` | 无（`Result<void>`） |
| `SetGroupAddRequest(SetGroupAddRequestParams)` | `set_group_add_request` | 无（`Result<void>`） |
| `GetCookies(GetCookiesParams)` | `get_cookies` | `Cookies`（`cookies`） |
| `GetCsrfToken(GetCsrfTokenParams)` | `get_csrf_token` | `CsrfToken`（`token`） |
| `GetCredentials(GetCredentialsParams)` | `get_credentials` | `Credentials`（`cookies`、`csrf_token`） |
| `GetRecord(GetRecordParams)` | `get_record` | `FileInfo`（`file`） |
| `GetImage(GetImageParams)` | `get_image` | `FileInfo`（`file`） |
| `CanSendImage(CanSendImageParams)` | `can_send_image` | `CanSend`（`yes`） |
| `CanSendRecord(CanSendRecordParams)` | `can_send_record` | `CanSend`（`yes`） |
| `GetStatus(GetStatusParams)` | `get_status` | `Status`（`online`、`good`） |
| `GetVersionInfo(GetVersionInfoParams)` | `get_version_info` | `VersionInfo`（`app_name`、`app_version`、`protocol_version`） |
| `SetRestart(SetRestartParams)` | `set_restart` | 无（`Result<void>`） |
| `CleanCache(CleanCacheParams)` | `clean_cache` | 无（`Result<void>`） |

类型化动作读取 `data` 的规则：

- ID 和主要的名称或内容字段是必需的：视动作而定，包括 `user_id`、`group_id`、`nickname`、`group_name`、`message_type`、`message_id`、`message`、`messages`、`cookies`、`token`、`csrf_token`、`file`、`yes`、`good`、`app_name` 和 `sender.user_id`。
- 必需字段缺失或为 null，或任一字段类型不对（包括 `async` 时的 null `data`）时，返回 `ResponseValidation` 错误，`body` 是该 `data`。列表类动作的错误会指出是哪一项，例如 `data[1].nickname is missing`。
- 其他字段在实现没有给出时读作空字符串、0 或 false。
- 整数字段同时接受 JSON 数字和十进制字符串。
- `StrangerInfo`、`MessageInfo`、`GroupInfo`、`GroupMemberInfo`、`FriendInfo`、`Cookies`、`FileInfo`、`Status`、`VersionInfo` 在 `raw` 里保留完整的 `data` 或列表项，没有单独成员的字段从这里读取，例如 NapCat 的 `qid`、`group_remark`、`is_robot`。

NapCat 在这些动作上的行为：

- `SendMsgParams` 的 `message_type`、`user_id`、`group_id` 只在设置了时才发送。不带 `message_type` 时，NapCat 的 `send_msg` 在设置了 `group_id` 时发到群里，否则发给 `user_id`。`message_type` 为 `private` 时，如果 `user_id` 不是好友，NapCat 通过 `group_id` 发临时会话。
- `SendMsgParams` 的字段与 `MessageEvent` 对应，回调可以用同一种写法回复私聊和群消息：`bot.SendMsg({ .message_type = event.message_type, .user_id = event.user_id, .group_id = event.group_id, .message = reply })`。
- `no_cache` 是 `std::optional<bool>`，只在设置了时才发送。不带 `no_cache` 时，NapCat 的 `get_group_member_info` 会跳过缓存、重新获取，尽管标准的默认值是 false。NapCat 的 `get_group_info` 忽略 `no_cache`。`GetFriendListParams::no_cache`、`GetGroupListParams::no_cache` 和 `GetGroupMemberListParams::no_cache` 是 NapCat 的扩展，标准的 `get_friend_list`、`get_group_list`、`get_group_member_list` 没有 `no_cache`。
- NapCat 只在内存里保存消息 ID，默认 5000 条。对 NapCat 上次重启之前的 ID，`GetMsg`、`DeleteMsg` 和 `GetForwardMsg` 以 `Action` 失败。
- `GetForwardMsg` 接受收到的 `forward` 消息段的 `id`。NapCat 上报的事件默认只带这个 `id`，转发的消息要用这个动作获取。
- NapCat 对 `get_forward_msg` 返回 go-cqhttp 的 `messages`，每项是一条完整消息，形状与 `get_msg` 的相同，而不是标准里由 `node` 消息段组成的 `message`。`GetForwardMsg` 按 NapCat 的形状读取，遇到标准的形状时以 `ResponseValidation` 失败。
- 嵌套在合并转发里的合并转发直接内嵌：它的 `forward` 消息段在 `data["content"]` 里以同样的形状带着内层消息。NapCat 不能按 `id` 获取嵌套的合并转发；转发消息的 `message_id` 只用来标识，NapCat 不能对它执行操作。
- `GroupMemberInfo::nickname` 是 QQ 昵称，群名片是 `card`。NapCat 对部分成员不给 `role`，所以它可能为空。NapCat 总是把 `unfriendly` 发成 false、`card_changeable` 发成 true、`title_expire_time` 发成 0。
- `GetGroupMemberList` 返回的是 NapCat 缓存里的成员，没有 `get_group_member_info` 额外获取的资料详情，所以 `area` 等资料字段在列表里可能为空。
- `GetGroupHonorInfo` 发送 `type`，默认为 `all`，缺少的列表读作空。NapCat 从 QQ 的网页读取群荣誉，读取失败时把列表留空，而不是报错。它的 `current_talkative` 是 `talkative_list` 的第一项，所以 `day_count` 为 0；没有龙王时是空对象，读作空的 `std::optional`。`strong_newbie_list` 总是为空。
- `SendLike` 总是发送 `times`，默认为 1；标准规定每个好友每天最多 10 次。NapCat 对成功的 `send_like` 不返回 `data`，QQ 拒绝的点赞以 `Action` 报告。
- 群设置类动作（`SetGroup*`）对标准规定了默认值的参数总是按该默认值发送，结果不受实现自身默认值的影响。
- NapCat 不检查 QQ 对 `set_group_kick`、`set_group_admin` 和 `set_group_leave` 的处理结果，所以 QQ 拒绝的操作（例如权限不够）也会报告成功。
- `SetGroupBan` 以秒为单位发送 `duration`，默认 30 分钟，与标准一致；0 表示解除禁言。NapCat 自己的默认值是 0。对不在群里的成员、群主，以及机器人账号不是群主时的管理员，NapCat 以 `Action` 失败。
- `SetGroupAdmin` 发送 `enable`，默认为 true，与标准一致。不带它时，NapCat 的 `set_group_admin` 反而会取消管理员。
- `SetGroupCardParams::card` 为空时删除群名片。NapCat 的 `set_group_card` 设置后会在约 3 秒内读回群名片，到时仍未改变就以 `Action` 失败。
- `SetGroupSpecialTitleParams::special_title` 为空时删除专属头衔。NapCat 通过 packetBackend 设置头衔：它不可用时，`set_group_special_title` 以 `Action` 失败，`retcode` 为 400。NapCat 忽略 `duration`。
- `SetFriendAddRequest` 接受好友请求事件的 `flag`，从 `RequestEvent::raw` 读取（`event.raw["flag"].asString()`）。`remark` 只在不为空时发送。没有 `flag` 对应的待处理请求时，NapCat 以 `Action` 失败；否则它不等 QQ 的处理结果，即使 QQ 拒绝，动作也会成功。
- `SetGroupAddRequest` 以同样的方式接受群请求事件的 `flag` 和 `sub_type`，`sub_type` 和 `reason` 只在不为空时发送。NapCat 忽略 `sub_type`；没有 `reason` 时用一个空格代替，因为 QQ 可能拒绝空的理由。在机器人收到的邀请和最近 100 条群通知里都找不到 `flag` 对应的请求时，NapCat 以 `Action` 失败；否则它同样不检查 QQ 的处理结果。
- `GetCookies` 总是发送 `domain`，默认为空，与标准一致，因为不带它时 NapCat 的 `get_cookies` 以 `Action` 失败，`retcode` 为 400。NapCat 按这个域名登录 QQ 来获取 cookies，所以要传 cookies 所属的域名，例如 `qun.qq.com`。NapCat 还会返回 `bkn`，即这些 cookies 对应的 CSRF Token，以字符串形式放在 `Cookies::raw` 里。
- NapCat 的 `get_csrf_token` 用账号的 `skey` 计算 token，`skey` 通过登录 QQ 获取，获取不到时以 `Action` 失败。
- `GetCredentials` 与 `GetCookies` 一样总是发送 `domain`。NapCat 的 `get_credentials` 和 `get_csrf_token` 一样把 token 放在 `token` 里，而不是标准的 `csrf_token`；`GetCredentials` 两种都能读取。NapCat 用 cookies 里的 `skey` 计算 token，没有 `skey` 时发 0。
- `GetRecord` 接受收到的 `record` 消息段的 `file`，并总是发送 `out_format`，因为不带它时 NapCat 的 `get_record` 以 `Action` 失败，`retcode` 为 400；`out_format` 为空时 NapCat 不转换格式。NapCat 用 ffmpeg 在原文件旁边转换语音，还会把转换后的内容以 `base64` 放在 `FileInfo::raw` 里。
- `GetImage` 接受收到的 `image` 消息段的 `file`。NapCat 下载图片，只有开启了 `enableLocalFile2Url` 设置（默认关闭）时才返回 `base64`。它的 `url` 在 NapCat 能获取到时是 QQ 上的图片地址，否则是本地路径。
- `FileInfo::file` 是 NapCat 主机上的路径。NapCat 跑在容器里时，改从 `base64` 读取内容，或者从 `url` 下载图片。
- NapCat 在它收到的消息的媒体里查找 `file`，这些记录在内存里保留到最后一次使用后一天；找不到时按这个名字在 QQ 的文件助手里搜索，仍找不到时以 `Action` 失败。
- NapCat 的 `can_send_image` 和 `can_send_record` 总是回答 `yes`。
- NapCat 的 `get_status` 总是把 `good` 答作 true，账号离线时也一样，所以要看 `online`。QQ 让账号下线时，NapCat 把 `online` 置为 false。标准规定实现无法查询在线状态时 `online` 为 null，这时读作空的 `std::optional`。
- NapCat 的 `get_version_info` 回答的 `app_name` 是 `NapCat.Onebot`，`protocol_version` 是 `v11`，`app_version` 是 NapCat 自己的版本。
- `SetRestart` 总是发送 `delay`，默认 0，与标准一致；NapCat 忽略它。NapCat 向主进程发出重启请求后立即回答 `ok`，而标准规定回答 `async`（两者都算成功）。随后它的工作进程退出，约 3 秒后新进程启动，这期间 HTTP API 和事件上报都会中断。以单进程运行（`NAPCAT_DISABLE_MULTI_PROCESS=1`）或使用 Framework 版本时，NapCat 无法自行重启，以 `Action` 失败。
- NapCat 的 `clean_cache` 删除它的临时目录，以及 QQ 的 `Pic`、`Ptt`、`Video`、`File`、`log` 数据目录里直接存放的文件，不删除其中的子目录。删除不了的文件只记日志，只有读不了临时目录时才以 `Action` 失败。临时目录里还有 NapCat 正在发送的消息下载的媒体文件，同时清理可能让这次发送失败。

### 消息段

`MessageSegment` 为常用的消息段类型提供静态函数，命名沿用 NoneBot2 和 aiocqhttp 的 `MessageSegment`：

| 函数 | 消息段 |
|---|---|
| `Text(text)` | `{"type": "text", "data": {"text": text}}` |
| `At(user_id)`、`AtAll()` | `{"type": "at", "data": {"qq": "<user_id>"}}`，或 `"qq": "all"` |
| `Reply(message_id)` | `{"type": "reply", "data": {"id": "<message_id>"}}` |
| `Image(file)` | `{"type": "image", "data": {"file": file}}` |
| `Face(id)` | `{"type": "face", "data": {"id": "<id>"}}` |
| `Record(file)` | `{"type": "record", "data": {"file": file}}` |
| `Video(file)` | `{"type": "video", "data": {"file": file}}` |
| `ContactFriend(user_id)`、`ContactGroup(group_id)` | `{"type": "contact", "data": {"type": "qq", "id": "<user_id>"}}`，或 `"type": "group"` 加 `"id": "<group_id>"` |
| `Json(data)` | `{"type": "json", "data": {"data": data}}` |
| `Music(type, id)` | `{"type": "music", "data": {"type": type, "id": id}}` |
| `Rps()`、`Dice()` | `{"type": "rps", "data": {"result": ""}}`，或 `"type": "dice"` |
| `Node(message_id)` | `{"type": "node", "data": {"id": "<message_id>", "nickname": "", "content": null}}` |
| `NodeCustom(user_id, nickname, content)` | `{"type": "node", "data": {"user_id": "<user_id>", "nickname": nickname, "content": [...]}}` |
| `Forward(res_id)` | `{"type": "forward", "data": {"id": res_id}}` |

```cpp
auto const sent{ co_await bot.SendGroupMsg({
    .group_id = *event.group_id,
    .message = {
        drogon_onebot::MessageSegment::Reply(event.message_id),
        drogon_onebot::MessageSegment::At(event.user_id),
        drogon_onebot::MessageSegment::Text(" 收到"),
    },
}) };
if (!sent) {
    LOG_ERROR << "send_group_msg failed: " << sent.error().message;
}
```

- ID 以字符串发送，与标准示例的写法一致。NapCat 字符串和数字都接受。
- `Image`、`Record` 和 `Video` 接受 `http://` 或 `https://` URL、运行 OneBot 实现的主机上的 `file://` URI 或路径，或 `base64://` 加文件数据。NapCat 跑在 Docker 里时，路径指的是 NapCat 容器内的文件系统。`Record` 和 `Video` 还接受收到的同类消息段的 `file`。
- NapCat 用 ffmpeg 把语音转换成 QQ 的 SILK 格式。获取或转换文件失败时，整条消息以 `Action` 发送失败。
- NapCat 用 ffmpeg 从视频里截取封面，截取不了时用默认封面。要发送别的封面，设置 NapCat 的扩展字段 `thumb`，例如 `video.data["thumb"] = "https://example.com/cover.jpg";`。
- 对于 `contact` 消息段，NapCat 发送 QQ 为该用户或群生成的推荐卡片。
- `Json` 接受字符串形式的 JSON 卡片内容，NapCat 原样交给 QQ。
- `Music` 接受音乐平台和歌曲 ID。NapCat 支持 `qq`、`163`、`kugou`、`kuwo`、`migu`，不支持标准里的 `xm`。它把歌曲的 `type` 和 `id` 发给外部服务为卡片签名：NapCat 配置里 `musicSignUrl` 设置的服务，默认是一个公共服务。签名失败时，NapCat 丢弃该消息段。
- `Rps` 和 `Dice` 发送空的 `result`。标准不带数据，但 NapCat 拒绝没有 `result` 的这两种消息段。NapCat 忽略它的值，结果由 QQ 决定。
- 由 `Node` 消息段组成的消息，通过 `SendMsg`、`SendGroupMsg` 或 `SendPrivateMsg` 发送时，会作为一条合并转发发出，内容是它们引用的消息；这种消息里的其他消息段被 NapCat 忽略。`Node` 还发送空的 `nickname` 和 null 的 `content`，NapCat 要求每个节点都带这两个字段，设置了 `id` 时忽略它们。NapCat 跳过它不认识的消息的节点。
- `NodeCustom` 用自己给定的发送者和消息构造节点，`content` 全是节点时构成嵌套的合并转发。只有 NapCat 的 packet 后端可用时，才会显示这个发送者。没有 packet 后端时，机器人账号先把内容发给自己再转发，发送者显示为机器人。
- NapCat 通过 packet 后端发出合并转发时，`SentMessage::res_id` 是这条合并转发的 ID。其他消息，以及没有 packet 后端时发出的合并转发，它为空。
- `Forward` 按此前发送时得到的 `SentMessage::res_id` 再次发送合并转发。标准里 `forward` 消息段只在接收时出现，收到的 `forward` 的 `id` 是给 `GetForwardMsg` 用的消息 ID，`Forward` 用不了。NapCat 发出的卡片显示它自己的占位预览，而不是消息内容。
- NapCat 会静默丢弃某些消息段，其余照常发送：私聊里的 `at`，以及 ID 不认识的 `reply` 或 `face`。
- NapCat 先按自己的 schema 检查每个消息段。类型不认识，或缺少 NapCat 要求的字段时，整条消息以 `Action` 发送失败，`retcode` 为 400。
- 其他消息段类型或额外字段，直接构造或修改 `type` 和 `data`，例如 `image.data["summary"] = "[图片]";`。
- 收到的消息段里 ID 同样是字符串。收到的 `reply` 消息段的 `id` 是十进制字符串，传给 `GetMsg` 前要转成整数。

## 接收事件

插件按 `post_type` 分发，四个方法各自追加回调，同一类型可注册多个，按注册顺序启动：

| 方法 | `post_type` | 事件类型 | 带类型的字段 |
|---|---|---|---|
| `OnMessage(MessageHandler)` | `message` | `MessageEvent` | `message_type`、`sub_type`、`message_id`、`user_id`、`group_id`（仅群消息）、`message`、`raw_message`、`font`、`sender` |
| `OnNotice(NoticeHandler)` | `notice` | `NoticeEvent` | `notice_type`、`sub_type`、`detail`（见“通知详情”） |
| `OnRequest(RequestHandler)` | `request` | `RequestEvent` | `request_type`、`sub_type` |
| `OnMetaEvent(MetaEventHandler)` | `meta_event` | `MetaEvent` | `meta_event_type`、`sub_type` |

- 每种事件都有 `time`、`self_id`，以及完整原始事件 `raw`（`Json::Value`）。没有单独成员的字段从 `raw` 读取，例如请求事件的 `flag`、心跳的 `interval`。
- 回调类型是 `std::function<drogon::Task<>(Event)>`。
- 其他 `post_type`，例如开启 `reportSelfMessage` 后 NapCat 上报的 `message_sent`，回 204 后丢弃。
- NapCat 有时把 `user_id`、`group_id` 发成字符串，解析时按数字读取。

### 通知详情

`NoticeEvent::detail` 以 `std::variant`（`NoticeDetail`）保存下表各类通知的专有字段，每类通知一个结构体。其他通知，例如 NapCat 的 `group_card`、`essence`，在这里是 `std::monostate`，字段从 `raw` 读取。

| `notice_type` | `detail` | 字段 |
|---|---|---|
| `group_upload` | `GroupUploadNotice` | `group_id`、`user_id`、`file`（`id`、`name`、`size`、`busid`） |
| `group_admin` | `GroupAdminNotice` | `group_id`、`user_id`；`sub_type` 为 `set` 或 `unset` |
| `group_decrease` | `GroupDecreaseNotice` | `group_id`、`operator_id`、`user_id`；`sub_type` 为 `leave`、`kick`、`kick_me` 或 NapCat 的 `disband` |
| `group_increase` | `GroupIncreaseNotice` | `group_id`、`operator_id`、`user_id`；`sub_type` 为 `approve` 或 `invite` |
| `group_ban` | `GroupBanNotice` | `group_id`、`operator_id`、`user_id`、`duration`；`sub_type` 为 `ban` 或 `lift_ban` |
| `friend_add` | `FriendAddNotice` | `user_id` |
| `group_recall` | `GroupRecallNotice` | `group_id`、`user_id`、`operator_id`、`message_id` |
| `friend_recall` | `FriendRecallNotice` | `user_id`、`message_id` |

```cpp
plugin->OnNotice([](drogon_onebot::NoticeEvent event) -> drogon::Task<> {
    if (auto const* upload{ std::get_if<drogon_onebot::GroupUploadNotice>(&event.detail) }) {
        LOG_INFO << upload->user_id << " uploaded " << upload->file.name;
    }
    co_return;
});
```

- 表中字段是必需的，只有文件的 `size` 和 `busid` 可以缺省，读作 0。所列类型的通知缺少必需字段或字段类型不对时返回 400，回调不运行。

### 路由的应答

| 情况 | 状态码 | 回调 |
|---|---|---|
| 配置了 secret 但没有 `X-Signature` | 401 | 不运行 |
| `X-Signature` 与请求体不符 | 403 | 不运行 |
| 请求体不是 JSON 对象；缺少 `post_type` 或所需的类型字段；整数字段不是整数；`message` 不是消息段数组 | 400，响应体说明原因 | 不运行 |
| 合法事件 | 204 | 应答之后在后台启动 |

先应答、后运行回调：NapCat 会一直挂着上报请求，直到收到响应，而回调可能要等好几秒（例如调用 LLM）。所以快速操作（在响应体里返回 `reply` 等字段）不支持，回复一律走 `SendPrivateMsg`、`SendGroupMsg` 等动作。

### 回调的约束

- **注册时机**：只能在插件初始化之后、开始监听之前注册，也就是在 beginning advice 里，或在把本插件列进 `dependencies` 的另一个插件的 `initAndStart` 里。路由在 IO 线程上读取回调列表，没有加锁。
- **生命周期**：每个事件对每个回调各启动一个 `drogon::async_run` 任务，任务帧里各拷贝一份回调和事件。所以回调 lambda 的捕获（例如 `bot`）一直存活到该任务结束。
- **异常**：回调抛出的异常由库捕获并打 ERROR 日志，不影响其他回调和后续事件。`AsyncTask` 里未捕获的异常会直接 `std::terminate`（`coroutine.h:397-401`）。
- **线程**：回调在收到该请求的 IO 线程上开始运行。在回调里调用 `OneBot` 的方法时，请求也在这个线程的事件循环上发出，回调恢复后仍在同一线程。
- 没有配置 `event_path` 时调用 `On*`，会打一条 WARN 日志，回调永远不会运行。

## 错误处理

OneBot v11 的 HTTP API 只在请求本身有问题时返回非 200（400、401、403、404、406），动作执行失败也返回 200，靠响应体里的 `status` 区分。

| `ErrorKind` | 含义 | `status_code` | `retcode` | `body` |
|---|---|---|---|---|
| `Connection` | 没有收到响应：DNS、TLS、连接被拒等 | 0 | 0 | 空 |
| `Timeout` | 在 `timeout` 秒内没有收到响应 | 0 | 0 | 空 |
| `Status` | 非 2xx：未带 token（401）、token 不符（NapCat 为 403）、动作不存在（404）等 | HTTP 状态码 | 0 | 原始响应体 |
| `ResponseValidation` | 2xx 响应不是 JSON、没有 `status` 字符串或 `status` 未知；或类型化动作的 `data` 缺必需字段或字段类型不对 | 0 | 0 | 响应内容，或 `data` |
| `Action` | 响应的 `status` 为 `failed` | 0 | 响应的 `retcode` | 响应内容 |

- `status` 为 `ok` 或 `async` 都算成功；`async` 时 `data` 为 null，执行结果无法获知。
- `message` 会带上响应里的 `message`，为空时用 `wording`。这两个字段是 NapCat 等实现的扩展，v11 标准只有 `status`、`retcode`、`data`。
- NapCat 对不存在的动作返回 HTTP 200、`status` 为 `failed`、`retcode` 200，因此是 `Action` 错误而不是 `Status` 404。
- 在 Windows 上，连接被拒要约 2 秒才报告，因为系统会先重试连接。`timeout` 比这更短时，请求会以 `Timeout` 而不是 `Connection` 失败。Linux 上连接被拒会立即失败。

## 线程与事件循环

- 只提供协程接口。drogon 文档建议优先使用异步接口或协程，同步 API 会阻塞 IO 线程，在 client 所在的 loop 上同步等待还会死锁。
- `Transport::Call` 为每个请求在调用线程的事件循环上新建 `HttpClient`（调用线程没有 loop 时用主 loop），并在响应到达前一直留在协程帧里。drogon 的 `HttpClient` 默认 pipelining 深度为 0，共享一个 client 会让并发请求串行执行。
- 协程按值接收参数；每个动作方法在第一次 `co_await` 之前把 `m_transport` 拷进协程帧，即使 `OneBot` 在请求途中被销毁，请求也能完成。

## 已知限制

- 只实现了 HTTP API 和 HTTP POST 上报，没有正向 WebSocket 和反向 WebSocket。
- 不支持上报响应里的快速操作。
- 类型化动作见“调用动作”一节的表格，其余动作先用 `CallAction`。
- `set_group_anonymous` 和 `set_group_anonymous_ban` 没有类型化方法，因为 NapCat 没有注册这两个动作。
- `shake`、`anonymous`、`share`、`poke`、`xml`、`location` 和自定义 `music` 没有 `MessageSegment` 函数，因为 NapCat 发不出这些消息段：它拒绝 `shake`、`anonymous`、`share` 和自定义 `music`，丢弃 `poke` 和 `xml`，发送 `location` 时不带坐标。其他实现可以通过 `type` 和 `data` 构造。NapCat 用自己的动作戳一戳，例如 `group_poke`，可以通过 `CallAction` 调用。
- 只有“通知详情”里列出的通知有自己的类型化字段。其他通知，以及请求、元事件，只有公共字段和子类型是类型化的，其余字段从 `raw` 读取。
- `MessageEvent::anonymous`，`Sender` 的 `sex`、`age`、`area`、`level`、`title`，以及 `notify` 通知里的 `lucky_king` 和 `honor` 没有类型化成员，因为 NapCat 从不发送它们。其他实现发送的这些字段从 `raw` 读取。

## 测试

测试使用 drogon 自带的测试框架 `drogon/drogon_test.h`（随 drogon 安装，不需要额外依赖）。每个 `tests/test_*.cpp` 编译成一个程序，并注册进 CTest。

- 运行全部测试：`just test`，等价于 `ctest --preset linux-debug`。
- 运行单个程序：`just run test_events`。
- 运行单个用例：`build/linux-debug/tests/test_events -r RejectsWrongSignature`。加 `-s` 打印通过的断言，加 `-l` 列出用例。

全部测试离线运行。需要服务端的测试由同一个 app 充当 NapCat。

| 程序 | 覆盖内容 |
|---|---|
| `test_config` | 不启动 app，直接调用 `initAndStart`：必填项、两个 `_env` 的读取与报错、`event_path` 校验，以及仓库里的 config.json 能通过校验。 |
| `test_actions` | 假的 NapCat HTTP API。`CallAction` 的请求体、`Authorization`、`base_url` 路径前缀；`async`；全部五种 `ErrorKind`（`failed`、只有 `wording`、404、403、非 JSON、无 `status`、未知 `status`、超时、连接失败）；类型化动作在 NapCat 形状的 `data` 下成功、`data` 缺字段或字段类型不对、`Action` 错误，以及 `no_cache` 和 `send_msg` 的目标只在设置时发送。 |
| `test_events` | 带签名的事件路由。RFC 2202 向量校验测试自带的 HMAC；用 Python 独立算出的 NapCat 签名；大写签名；字符串 ID 的群消息；401、403、400 各种拒收且回调不运行；四种事件的分发；类型化的通知详情，以及缺少类型字段的通知；丢弃 `message_sent`；多个回调都运行；回调抛异常后服务继续；应答不等慢回调。 |
| `test_events_unsigned` | 去掉 `secret_env` 的事件路由：接受无签名事件，忽略任意 `X-Signature`。 |
| `test_segments` | 不启动 app：每个 `MessageSegment` 函数构造的消息段，包括以字符串发送的 ID，以及混合构造和手写消息段的消息。 |
