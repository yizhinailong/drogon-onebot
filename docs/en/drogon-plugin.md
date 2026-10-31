# drogon plugin: `drogon_onebot::OneBotClient`

English · [简体中文](../zh-CN/drogon-plugin.md)

This page describes how to add drogon-onebot to a drogon app as a plugin, its configuration, lifecycle, and usage, and the design constraints that follow from drogon's threading model. The protocol is OneBot v11, and the library implements three parts:

- Calling actions: the HTTP API, through the `OneBot` client.
- Receiving events: HTTP POST reporting, through the event route that the plugin registers.
- Both over one connection: the reverse WebSocket, through the WebSocket route that the plugin registers and that the OneBot implementation connects to.

The reference OneBot implementation is NapCat.

## Overview

`drogon_onebot::OneBotClient` derives from `drogon::Plugin<OneBotClient>` and does four things:

1. When drogon starts, it reads and validates its configuration (`OneBotConfig`) from config.json.
2. When `event_path` is set, it registers a `POST` route at that path. The route receives the events that the OneBot implementation reports and dispatches them to the callbacks registered with `OnMessage` and the other `On*` methods.
3. When `ws_path` is set, it registers a WebSocket route at that path. The OneBot implementation connects to it, pushes events over the connection to the same callbacks, and answers the actions sent to it.
4. `NewOneBotClient()` returns a `OneBot` client bound to that configuration. The client calls actions, for example to reply to a message: over the HTTP API at `base_url`, or over the reverse WebSocket without one.

The `OneBot` method names map one to one onto OneBot v11 action names (`SendGroupMsg` ↔ `send_group_msg`), and the methods that register callbacks take their names from aiocqhttp (`on_message` ↔ `OnMessage`):

```cpp
auto* const plugin{ drogon::app().getPlugin<drogon_onebot::OneBotClient>() };
plugin->OnMessage([bot = plugin->NewOneBotClient()](drogon_onebot::MessageEvent event) -> drogon::Task<> {
    if (event.message_type == "private") {
        co_await bot.SendPrivateMsg({ .user_id = event.user_id, .message = event.message });
    }
});
```

## Integration

The library needs C++23 (the public headers use `std::expected`). `drogon-onebot::drogon-onebot` carries the `Drogon::Drogon` dependency, so consumers don't need their own `find_package(Drogon)`. Use either way:

```cmake
# From source
add_subdirectory(path/to/drogon-onebot)

# Installed: run just publish in drogon-onebot first, then add publish/ to CMAKE_PREFIX_PATH
find_package(drogon-onebot CONFIG REQUIRED)

target_link_libraries(app PRIVATE drogon-onebot::drogon-onebot)
```

## Registration and configuration

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

With the reverse WebSocket, one route carries both directions, and `base_url` can go:

```json
"app": {
    "client_max_websocket_message_size": "64M"
},
"plugins": [
    {
        "name": "drogon_onebot::OneBotClient",
        "config": {
            "ws_path": "/onebot/ws",
            "access_token_env": "ONEBOT_ACCESS_TOKEN"
        }
    }
]
```

| Key | Required | Description |
|---|---|---|
| `base_url` | Unless `ws_path` is set | The HTTP API address of the OneBot implementation, optionally with a path prefix. Actions go to `<base_url>/<action>`. Without it, actions go over the reverse WebSocket connection. |
| `access_token_env` | No | **Name of the environment variable** that holds the access token. Without it, no `Authorization` header is sent or checked; with `ws_path` set, `initAndStart` then logs a WARN line, because anyone who can reach the listener can connect. |
| `event_path` | No | Route path that receives events. It must start with `/`. Without it, no route is registered, and without `ws_path` either, the plugin can send but not receive. |
| `secret_env` | No | **Name of the environment variable** that holds the secret that signs reported events. Without it, signatures are not checked and unsigned events are accepted; with `event_path` set, `initAndStart` then logs a WARN line, because anyone who can reach the listener can post events. |
| `ws_path` | No | Route path of the reverse WebSocket, which the OneBot implementation connects to. It must start with `/`. Without it, no WebSocket route is registered. |

- Neither secret goes into config.json. Both are read only from environment variables.
- The access token is sent to the HTTP API as `Authorization: Bearer <token>`, and connections to `ws_path` must present it the same way. It must match the token of the OneBot implementation's HTTP server and reverse WebSocket client.
- The secret checks the `X-Signature: sha1=<hex of HMAC-SHA1(secret, body)>` header of each event.
- With `ws_path` set, raise `client_max_websocket_message_size` in the `app` section, as above; see "Reverse WebSocket".
- `initAndStart` throws `std::invalid_argument` in any of these cases:
  - both `base_url` and `ws_path` are missing or empty;
  - `event_path` or `ws_path` does not start with `/`;
  - `access_token_env` or `secret_env` is set, but the environment variable it names is unset or empty.

  The exception propagates out of `app().run()`. If nothing catches it, the program terminates.

### NapCat configuration

NapCat's OneBot configuration (`onebot11_<QQ number>.json`) needs two network endpoints:

- An HTTP server, which this library calls actions on.
- An HTTP client, which POSTs events to this plugin.

```json
"network": {
    "httpServers": [
        { "name": "api", "enable": true, "host": "127.0.0.1", "port": 3000, "messagePostFormat": "array", "token": "<value of ONEBOT_ACCESS_TOKEN>" }
    ],
    "httpClients": [
        { "name": "bot", "enable": true, "url": "http://127.0.0.1:8080/onebot/event", "messagePostFormat": "array", "reportSelfMessage": false, "token": "<value of ONEBOT_SECRET>" }
    ]
}
```

- `httpServers[].token` is the access token. NapCat checks the `Authorization: Bearer` header and answers 403 when it does not match.
- `httpClients[].token` is the secret that signs reports. NapCat computes the HMAC-SHA1 of the request body with it and puts the result in the `x-signature` header.
- The two tokens may differ. They correspond to `access_token_env` and `secret_env` respectively.
- Set `messagePostFormat` to `array` on both endpoints:
  - The HTTP client's setting applies to reported events. With `string`, `message` is a CQ-code string, and the event route rejects the event with 400.
  - The HTTP server's setting applies to the `message` that `get_msg` returns. With `string`, `GetMsg` fails with `ResponseValidation`.

For the reverse WebSocket, a single WebSocket client replaces both:

```json
"network": {
    "websocketClients": [
        { "name": "bot", "enable": true, "url": "ws://127.0.0.1:8080/onebot/ws", "messagePostFormat": "array", "reportSelfMessage": false, "reconnectInterval": 5000, "heartInterval": 30000, "token": "<value of ONEBOT_ACCESS_TOKEN>" }
    ]
}
```

- `websocketClients[].token` is the access token. NapCat sends it in the handshake as `Authorization: Bearer <token>`, so it corresponds to `access_token_env`. NapCat sends the header even when its token is empty, which the route rejects with 403 once an access token is set.
- `messagePostFormat` applies to the events and to the `message` that `get_msg` returns on this connection, so set it to `array` here too.
- When the connection drops, NapCat reconnects every `reconnectInterval` milliseconds. On each new connection it sends a lifecycle `connect` event, and then a heartbeat every `heartInterval` milliseconds; 0 turns heartbeats off.
- To keep calling actions over HTTP while events arrive over the WebSocket, keep the HTTP server and `base_url` as well.

## Lifecycle

```
app().loadConfigFile("config.json")       reads the plugins section
app().run()
 ├─ OneBotClient::initAndStart(config)    parses and validates the config, registers the event_path and ws_path routes, throws on failure
 ├─ routers initialized                   no more routes can be registered after this
 ├─ beginning advices                     getPlugin<OneBotClient>() works from here on; register callbacks here
 ├─ startListening()                      each IO loop starts listening asynchronously, and events start to arrive
 └─ event loops run and handle requests
app().quit()                              drogon calls OneBotClient::shutdown()
```

- `getPlugin` returns a `OneBotClient*` that drogon owns. Don't delete it.
- The routes are registered in `initAndStart`. drogon initializes plugins before routers (`HttpAppFrameworkImpl.cc:663-671`), and drogon's own `PromExporter` plugin registers its route the same way (`PromExporter.cc:18`).
- drogon finds WebSocket controllers and middlewares by class name when it initializes the routers. For `ws_path`, the plugin registers its own instances of `details::ReverseWsController` and of `details::ReverseWsHandshake`, the middleware that checks the handshake, with `registerController` and `registerMiddleware`.
- With static linking, the app's code must reference `OneBotClient` (one call to `getPlugin<drogon_onebot::OneBotClient>()` is enough). Otherwise the linker drops `onebot_client.o`, and drogon prints `Plugin drogon_onebot::OneBotClient undefined!` and keeps running.

## Calling actions

- `OneBot` is a value handle whose only shared state is one `std::shared_ptr<details::Transport>`. A copy only increments a reference count, so pass it into coroutines by value.
- Each method reads its parameters and gives the returned task its own share of the transport before it returns. The task starts only when awaited, but it needs neither the `OneBot` nor the parameters to stay alive until then.
- Every method returns `drogon::Task<Result<T>>`, where `Result<T>` is `std::expected<T, Error>`. A failed action **does not throw**.
- Without `base_url`, the client sends each action over the reverse WebSocket connection that is open when the call starts, so one `OneBot` keeps working across NapCat's reconnects; see "Reverse WebSocket".
- Messages use array format: `Message` is `std::vector<MessageSegment>`, and each segment is a `type` plus a `Json::Value data`. CQ-code strings and `auto_escape` are not supported yet.

| Method | Action | Returns |
|---|---|---|
| `CallAction(CallActionParams)` | Any action, including implementation extensions | The response's `data`; null for `async` |
| `GetLoginInfo(GetLoginInfoParams)` | `get_login_info` | `LoginInfo` (`user_id`, `nickname`) |
| `GetStrangerInfo(GetStrangerInfoParams)` | `get_stranger_info` | `StrangerInfo` (`user_id`, `nickname`, `sex`, `age`) |
| `SendLike(SendLikeParams)` | `send_like` | Nothing (`Result<void>`) |
| `GetFriendList(GetFriendListParams)` | `get_friend_list` | `std::vector<FriendInfo>` (`user_id`, `nickname`, `remark`) |
| `SendPrivateMsg(SendPrivateMsgParams)` | `send_private_msg` | `SentMessage` (`message_id`) |
| `SendGroupMsg(SendGroupMsgParams)` | `send_group_msg` | `SentMessage` (`message_id`) |
| `SendMsg(SendMsgParams)` | `send_msg` | `SentMessage` (`message_id`) |
| `DeleteMsg(DeleteMsgParams)` | `delete_msg` | Nothing (`Result<void>`) |
| `GetMsg(GetMsgParams)` | `get_msg` | `MessageInfo` (`time`, `message_type`, `message_id`, `real_id`, `group_id`, `sender`, `message`, `raw_message`) |
| `GetForwardMsg(GetForwardMsgParams)` | `get_forward_msg` | `ForwardMessage` (`messages`, each a `MessageInfo`) |
| `GetGroupInfo(GetGroupInfoParams)` | `get_group_info` | `GroupInfo` (`group_id`, `group_name`, `member_count`, `max_member_count`) |
| `GetGroupList(GetGroupListParams)` | `get_group_list` | `std::vector<GroupInfo>` |
| `GetGroupMemberInfo(GetGroupMemberInfoParams)` | `get_group_member_info` | `GroupMemberInfo` (`card`, `role`, `level`, `title`, and the other fields of the standard) |
| `GetGroupMemberList(GetGroupMemberListParams)` | `get_group_member_list` | `std::vector<GroupMemberInfo>` |
| `GetGroupHonorInfo(GetGroupHonorInfoParams)` | `get_group_honor_info` | `GroupHonorInfo` (`current_talkative` and the five honor lists of `GroupHonorMember`) |
| `SetGroupKick(SetGroupKickParams)` | `set_group_kick` | Nothing (`Result<void>`) |
| `SetGroupBan(SetGroupBanParams)` | `set_group_ban` | Nothing (`Result<void>`) |
| `SetGroupWholeBan(SetGroupWholeBanParams)` | `set_group_whole_ban` | Nothing (`Result<void>`) |
| `SetGroupAdmin(SetGroupAdminParams)` | `set_group_admin` | Nothing (`Result<void>`) |
| `SetGroupCard(SetGroupCardParams)` | `set_group_card` | Nothing (`Result<void>`) |
| `SetGroupName(SetGroupNameParams)` | `set_group_name` | Nothing (`Result<void>`) |
| `SetGroupLeave(SetGroupLeaveParams)` | `set_group_leave` | Nothing (`Result<void>`) |
| `SetGroupSpecialTitle(SetGroupSpecialTitleParams)` | `set_group_special_title` | Nothing (`Result<void>`) |
| `SetFriendAddRequest(SetFriendAddRequestParams)` | `set_friend_add_request` | Nothing (`Result<void>`) |
| `SetGroupAddRequest(SetGroupAddRequestParams)` | `set_group_add_request` | Nothing (`Result<void>`) |
| `GetCookies(GetCookiesParams)` | `get_cookies` | `Cookies` (`cookies`) |
| `GetCsrfToken(GetCsrfTokenParams)` | `get_csrf_token` | `CsrfToken` (`token`) |
| `GetCredentials(GetCredentialsParams)` | `get_credentials` | `Credentials` (`cookies`, `csrf_token`) |
| `GetRecord(GetRecordParams)` | `get_record` | `FileInfo` (`file`) |
| `GetImage(GetImageParams)` | `get_image` | `FileInfo` (`file`) |
| `CanSendImage(CanSendImageParams)` | `can_send_image` | `CanSend` (`yes`) |
| `CanSendRecord(CanSendRecordParams)` | `can_send_record` | `CanSend` (`yes`) |
| `GetStatus(GetStatusParams)` | `get_status` | `Status` (`online`, `good`) |
| `GetVersionInfo(GetVersionInfoParams)` | `get_version_info` | `VersionInfo` (`app_name`, `app_version`, `protocol_version`) |
| `SetRestart(SetRestartParams)` | `set_restart` | Nothing (`Result<void>`) |
| `CleanCache(CleanCacheParams)` | `clean_cache` | Nothing (`Result<void>`) |

How typed actions read `data`:

- The IDs and the main name or content fields are required: `user_id`, `group_id`, `nickname`, `group_name`, `message_type`, `message_id`, `message`, `messages`, `cookies`, `token`, `csrf_token`, `file`, `yes`, `good`, `app_name`, and `sender.user_id`, depending on the action.
- When a required field is missing or null, or any field has the wrong type (including the null `data` of `async`), the action returns a `ResponseValidation` error whose `body` is that `data`. For the list actions, the error names the entry, for example `data[1].nickname is missing`.
- Other fields read as empty, 0, or false when the implementation leaves them out.
- Integer fields accept both JSON numbers and decimal strings.
- `StrangerInfo`, `MessageInfo`, `GroupInfo`, `GroupMemberInfo`, `FriendInfo`, `Cookies`, `FileInfo`, `Status`, and `VersionInfo` keep the whole `data`, or the whole list entry, in `raw`, for the fields that have no member of their own, such as NapCat's `qid`, `group_remark`, and `is_robot`.

NapCat's behavior with these actions:

- `SendMsgParams` sends `message_type`, `user_id`, and `group_id` only when they are set. Without `message_type`, NapCat's `send_msg` sends to `group_id` when it is set and to `user_id` otherwise. With `private`, it sends to a `user_id` that is not a friend as a temporary chat through `group_id`.
- `SendMsgParams` mirrors `MessageEvent`, so a callback can answer private and group messages alike: `bot.SendMsg({ .message_type = event.message_type, .user_id = event.user_id, .group_id = event.group_id, .message = reply })`.
- `no_cache` is a `std::optional<bool>` and is sent only when set. When it is absent, NapCat's `get_group_member_info` skips its cache and fetches fresh data, although the standard's default is false. NapCat's `get_group_info` ignores `no_cache`. `GetFriendListParams::no_cache`, `GetGroupListParams::no_cache`, and `GetGroupMemberListParams::no_cache` are NapCat extensions; the standard's `get_friend_list`, `get_group_list`, and `get_group_member_list` have no `no_cache`.
- NapCat keeps message IDs in memory only, 5000 of them by default. `GetMsg`, `DeleteMsg`, and `GetForwardMsg` fail with `Action` for an ID from before NapCat's last restart.
- `GetForwardMsg` takes the `id` of a received `forward` segment. NapCat reports events with only that `id` by default, so the forwarded messages come from this action.
- NapCat answers `get_forward_msg` with go-cqhttp's `messages`, whole messages shaped like those of `get_msg`, instead of the standard's `message` of `node` segments. `GetForwardMsg` reads NapCat's shape and fails with `ResponseValidation` on the standard one.
- A forward nested in a forward comes inline: its `forward` segment carries the inner messages, in the same shape, in `data["content"]`. NapCat cannot fetch a nested forward by its `id`, and the `message_id` of a forwarded message only identifies it; NapCat cannot act on it.
- `GroupMemberInfo::nickname` is the QQ nickname; the group card is `card`. NapCat leaves out `role` for some members, so it can be empty. NapCat always sends `unfriendly` as false, `card_changeable` as true, and `title_expire_time` as 0.
- `GetGroupMemberList` returns NapCat's cached members, without the profile details that `get_group_member_info` adds, so profile fields such as `area` can be empty there.
- `GetGroupHonorInfo` sends `type`, `all` by default, and reads a missing list as empty. NapCat reads the honors from QQ's web page and, when that fails, leaves the list empty instead of failing. Its `current_talkative` is the first entry of `talkative_list`, so `day_count` is 0, and an empty object when there is none, which reads as an empty `std::optional`. `strong_newbie_list` is always empty.
- `SendLike` always sends `times`, 1 by default; the standard allows 10 likes per friend a day. NapCat answers a successful `send_like` without `data`, and reports a like that QQ refuses as `Action`.
- The group setters (`SetGroup*`) always send the parameters that the standard gives a default, with that default, so the result does not depend on the implementation's own defaults.
- NapCat does not check QQ's answer for `set_group_kick`, `set_group_admin`, and `set_group_leave`, so a change that QQ refuses, for example without the needed rights, still succeeds.
- `SetGroupBan` sends `duration` in seconds, 30 minutes by default as in the standard; 0 lifts the mute. NapCat's own default is 0. NapCat fails with `Action` for a member not in the group, for the owner, and for an admin unless the bot account owns the group.
- `SetGroupAdmin` sends `enable`, true by default as in the standard. Without it, NapCat's `set_group_admin` would remove the admin role instead.
- An empty `SetGroupCardParams::card` removes the card. NapCat's `set_group_card` reads the card back for about 3 seconds after setting it and fails with `Action` when it has not changed by then.
- An empty `SetGroupSpecialTitleParams::special_title` removes the title. NapCat sets titles through its packet backend: without a working one, `set_group_special_title` fails with `Action` and retcode 400. NapCat ignores `duration`.
- `SetFriendAddRequest` takes the `flag` of the friend request event, `RequestEvent::flag`. It sends `remark` only when it is not empty. NapCat fails with `Action` when no pending request has that `flag`; otherwise it does not wait for QQ's answer, so the action succeeds even if QQ refuses.
- `SetGroupAddRequest` takes the `flag` and `sub_type` of the group request event the same way, and sends `sub_type` and `reason` only when they are not empty. NapCat ignores `sub_type`, and replaces a missing `reason` with a single space, because QQ may reject an empty one. NapCat fails with `Action` when it finds no request with that `flag` among the bot's invitations and its latest 100 group notifications; otherwise it does not check QQ's answer either.
- `GetCookies` always sends `domain`, empty by default as in the standard, because NapCat's `get_cookies` fails with `Action` and retcode 400 without it. NapCat builds the cookies from a QQ login for that domain, so pass the domain they are for, such as `qun.qq.com`. It also answers with `bkn`, the CSRF token for these cookies, as a string in `Cookies::raw`.
- NapCat computes the `get_csrf_token` token from the account's `skey`, which it fetches through a QQ login, and fails with `Action` when it cannot get one.
- `GetCredentials` sends `domain` the same way as `GetCookies`. NapCat's `get_credentials` answers with the token as `token`, like `get_csrf_token`, instead of the standard's `csrf_token`; `GetCredentials` reads either. NapCat computes the token from the `skey` among the cookies and sends 0 when there is none.
- `GetRecord` takes the `file` of a received `record` segment and always sends `out_format`, because NapCat's `get_record` fails with `Action` and retcode 400 without it; with an empty one, NapCat skips the conversion. NapCat converts the voice with ffmpeg next to the original and also answers with the converted content as `base64` in `FileInfo::raw`.
- `GetImage` takes the `file` of a received `image` segment. NapCat downloads the image and answers with `base64` only when its `enableLocalFile2Url` setting is on, which it is not by default. Its `url` is the image's URL at QQ when NapCat can get one, and the path otherwise.
- `FileInfo::file` is a path on NapCat's host. When NapCat runs in a container, read the content from `base64` instead, or download the image from `url`.
- NapCat looks `file` up among the media of the messages it has received, kept in memory until a day after their last use, and otherwise searches QQ's file assistant by that name. It fails with `Action` when neither has the file.
- NapCat's `can_send_image` and `can_send_record` always answer `yes`.
- NapCat's `get_status` always answers `good` as true, even while the account is offline, so check `online`. It sets `online` to false when QQ takes the account offline. The standard sends `online` as null when the implementation cannot tell, which reads as an empty `std::optional`.
- NapCat's `get_version_info` answers with `app_name` `NapCat.Onebot`, `protocol_version` `v11`, and its own version as `app_version`.
- `SetRestart` always sends `delay`, 0 by default as in the standard; NapCat ignores it. NapCat answers `ok`, where the standard says `async` (both succeed), as soon as it has asked its master process for the restart. Its worker process then exits and a new one starts about 3 seconds later, so the HTTP API and event posts pause in between. Running in a single process (`NAPCAT_DISABLE_MULTI_PROCESS=1`) or as its framework build, NapCat cannot restart itself and fails with `Action`.
- NapCat's `clean_cache` deletes the files directly inside its temp directory and inside QQ's `Pic`, `Ptt`, `Video`, `File`, and `log` data directories, but not their subdirectories. It only logs the files it cannot delete, and fails with `Action` only when it cannot read its temp directory. That directory also holds the downloaded media of messages that NapCat is still sending, so a clean at the same time may break such a send.

### Message segments

`MessageSegment` has static functions for the common segment types, named after `MessageSegment` in NoneBot2 and aiocqhttp:

| Function | Segment |
|---|---|
| `Text(text)` | `{"type": "text", "data": {"text": text}}` |
| `At(user_id)`, `AtAll()` | `{"type": "at", "data": {"qq": "<user_id>"}}`, or `"qq": "all"` |
| `Reply(message_id)` | `{"type": "reply", "data": {"id": "<message_id>"}}` |
| `Image(file)` | `{"type": "image", "data": {"file": file}}` |
| `Face(id)` | `{"type": "face", "data": {"id": "<id>"}}` |
| `Record(file)` | `{"type": "record", "data": {"file": file}}` |
| `Video(file)` | `{"type": "video", "data": {"file": file}}` |
| `ContactFriend(user_id)`, `ContactGroup(group_id)` | `{"type": "contact", "data": {"type": "qq", "id": "<user_id>"}}`, or `"type": "group"` with `"id": "<group_id>"` |
| `Json(data)` | `{"type": "json", "data": {"data": data}}` |
| `Music(type, id)` | `{"type": "music", "data": {"type": type, "id": id}}` |
| `Rps()`, `Dice()` | `{"type": "rps", "data": {"result": ""}}`, or `"type": "dice"` |
| `Node(message_id)` | `{"type": "node", "data": {"id": "<message_id>", "nickname": "", "content": null}}` |
| `NodeCustom(user_id, nickname, content)` | `{"type": "node", "data": {"user_id": "<user_id>", "nickname": nickname, "content": [...]}}` |
| `Forward(res_id)` | `{"type": "forward", "data": {"id": res_id}}` |

```cpp
auto const sent{ co_await bot.SendGroupMsg({
    .group_id = *event.group_id,
    .message = {
        drogon_onebot::MessageSegment::Reply(event.message_id),
        drogon_onebot::MessageSegment::At(event.user_id),
        drogon_onebot::MessageSegment::Text(" got it"),
    },
}) };
if (!sent) {
    LOG_ERROR << "send_group_msg failed: " << sent.error().message;
}
```

- IDs are sent as strings, the form of the standard's examples. NapCat accepts both strings and numbers.
- `Image`, `Record`, and `Video` take an `http://` or `https://` URL, a `file://` URI or a path on the host that runs the OneBot implementation, or `base64://` followed by the file data. When NapCat runs in Docker, a path refers to the NapCat container's file system. `Record` and `Video` also take the `file` of a received segment of their type.
- NapCat converts a voice to QQ's SILK format with ffmpeg. When it cannot fetch or convert a file, the whole send fails with `Action`.
- NapCat takes a video's cover from the video with ffmpeg, or uses a default cover when it cannot. To send another cover, set NapCat's extension `thumb`, for example `video.data["thumb"] = "https://example.com/cover.jpg";`.
- For a `contact` segment, NapCat sends the recommendation card that QQ builds for that user or group.
- `Json` takes the card's content as a JSON string, which NapCat passes to QQ as is.
- `Music` takes a platform and a song ID. NapCat accepts `qq`, `163`, `kugou`, `kuwo`, and `migu`, but not the standard's `xm`. It sends the song's `type` and `id` to an outside service that signs the card: the one set as `musicSignUrl` in NapCat's config, or a public one by default. When signing fails, NapCat drops the segment.
- `Rps` and `Dice` send an empty `result`. The standard sends no data, but NapCat rejects these segments without `result`. NapCat ignores the value, and QQ picks the result.
- A message of `Node` segments is sent as one merged forward of the messages they reference, through `SendMsg`, `SendGroupMsg`, or `SendPrivateMsg`; NapCat ignores the other segments of such a message. `Node` also sends an empty `nickname` and a null `content`, which NapCat requires on every node and ignores when `id` is set. NapCat skips a node whose message it does not know.
- `NodeCustom` builds a node with a sender and a message of your own, and a content of only nodes nests a forward. NapCat shows that sender only when its packet backend works. Without it, the bot account sends the content to itself and forwards those messages, so the bot appears as their sender.
- When NapCat sends a merged forward through its packet backend, `SentMessage::res_id` holds the forward's ID. It is empty for other messages, and for a forward that NapCat sent without the packet backend.
- `Forward` sends a merged forward again, by the `SentMessage::res_id` of the send that made it. The standard only receives `forward` segments, and the `id` of a received one is a message ID for `GetForwardMsg`, which `Forward` cannot use. NapCat shows the card with its own placeholder preview instead of the messages.
- NapCat silently drops some segments and still sends the rest: `at` in a private message, and `reply` or `face` with an ID it does not know.
- NapCat first checks every segment against its own schema. A segment of a type it does not know, or one without a field that NapCat requires, fails the whole send with `Action` and retcode 400.
- For other segment types or extra fields, build or change `type` and `data` directly, for example `image.data["summary"] = "[picture]";`.
- Received segments carry their IDs as strings too. The `id` of a received `reply` segment is a decimal string; convert it to an integer for `GetMsg`.

## Receiving events

The plugin dispatches by `post_type`. Each of the four methods appends a callback. One type can have several callbacks, which start in the order they were registered:

| Method | `post_type` | Event type | Typed fields |
|---|---|---|---|
| `OnMessage(MessageHandler)` | `message` | `MessageEvent` | `message_type`, `sub_type`, `message_id`, `user_id`, `group_id` (group messages only), `message`, `raw_message`, `font`, `sender` |
| `OnNotice(NoticeHandler)` | `notice` | `NoticeEvent` | `notice_type`, `sub_type`, `detail` (see "Notice details") |
| `OnRequest(RequestHandler)` | `request` | `RequestEvent` | `request_type`, `sub_type`, `user_id`, `group_id` (group requests only), `comment`, `flag` |
| `OnMetaEvent(MetaEventHandler)` | `meta_event` | `MetaEvent` | `meta_event_type`, `sub_type`, `status` and `interval` (heartbeats only) |

- Every event has `time`, `self_id`, and the whole original event in `raw` (`Json::Value`). Read the fields that have no member of their own from `raw`, such as the `card_new` of NapCat's `group_card` notice.
- The callback type is `std::function<drogon::Task<>(Event)>`.
- Other `post_type` values, such as the `message_sent` that NapCat reports when `reportSelfMessage` is on, get 204 and are dropped.
- NapCat sometimes sends `user_id` and `group_id` as strings. They are parsed as numbers.
- A request event without `user_id` or `flag`, or a group request without `group_id`, gets 400. NapCat reports only friend requests that are unread, that the bot did not send, and that are not decided yet.
- For a group request, `sub_type` `add` is a request to join, where `user_id` is the requester, and `invite` invites the bot account, where `user_id` is the inviter. NapCat also reports as `add` a member's invitation of someone else that needs an admin's approval.
- A heartbeat has `status`, a `Status` like the one that `GetStatus` returns, and `interval`, the milliseconds until the next heartbeat; one without them gets 400. NapCat sends meta events only over WebSocket: over the reverse WebSocket, a lifecycle `connect` event on each connection and then its heartbeats. With HTTP POST reporting, `OnMetaEvent` never runs for NapCat.
- Over the reverse WebSocket, there is no response to carry a status. An event that the route would reject with 400 is logged at WARN level and dropped, and other `post_type` values are dropped too.

### Notice details

`NoticeEvent::detail` holds the type-specific fields of the notices below, as one struct per type in the `std::variant` `NoticeDetail`. Any other notice, such as NapCat's `group_card` or `essence`, has `std::monostate` there; read its fields from `raw`.

| `notice_type` | `detail` | Fields |
|---|---|---|
| `group_upload` | `GroupUploadNotice` | `group_id`, `user_id`, `file` (`id`, `name`, `size`, `busid`) |
| `group_admin` | `GroupAdminNotice` | `group_id`, `user_id`; `sub_type` is `set` or `unset` |
| `group_decrease` | `GroupDecreaseNotice` | `group_id`, `operator_id`, `user_id`; `sub_type` is `leave`, `kick`, `kick_me`, or NapCat's `disband` |
| `group_increase` | `GroupIncreaseNotice` | `group_id`, `operator_id`, `user_id`; `sub_type` is `approve` or `invite` |
| `group_ban` | `GroupBanNotice` | `group_id`, `operator_id`, `user_id`, `duration`; `sub_type` is `ban` or `lift_ban` |
| `friend_add` | `FriendAddNotice` | `user_id` |
| `group_recall` | `GroupRecallNotice` | `group_id`, `user_id`, `operator_id`, `message_id` |
| `friend_recall` | `FriendRecallNotice` | `user_id`, `message_id` |
| `notify`, with `sub_type` `poke` | `PokeNotice` | `user_id`, `target_id`; `group_id` in groups, NapCat's `sender_id` for friends |

```cpp
plugin->OnNotice([](drogon_onebot::NoticeEvent event) -> drogon::Task<> {
    if (auto const* upload{ std::get_if<drogon_onebot::GroupUploadNotice>(&event.detail) }) {
        LOG_INFO << upload->user_id << " uploaded " << upload->file.name;
    }
    co_return;
});
```

- The fields in the table are required, except a file's `size` and `busid`, which read as 0, and a poke's `group_id` and `sender_id`, which are `std::optional`. A notice of a listed type that misses a required field, or has a field of the wrong type, gets 400, and no callback runs.

### Route responses

| Case | Status | Callbacks |
|---|---|---|
| A secret is set, but `X-Signature` is missing | 401 | Not run |
| `X-Signature` does not match the request body | 403 | Not run |
| The body is not a JSON object; `post_type` or a required typed field is missing; an integer field is not an integer; `message` is not an array of message segments | 400, with the reason in the response body | Not run |
| A valid event | 204 | Started in the background after the response |

The route answers first and runs the callbacks afterwards. NapCat keeps its report request open until it gets a response, and a callback may wait for seconds (for example, on an LLM call). So quick operations (returning `reply` or other fields in the response body) are not supported. Always reply through actions such as `SendPrivateMsg` and `SendGroupMsg`.

### Callback rules

- **When to register**: only after the plugin is initialized and before the listeners start. That means in a beginning advice, or in `initAndStart` of another plugin that lists this one under `dependencies`. The routes read the callback lists on IO threads without a lock, so once the listeners have started, `On*` logs an ERROR line and drops the callback.
- **Lifetime**: each event starts one `drogon::async_run` task per callback, and each task frame holds its own copy of the callback and the event. So the captures of a callback lambda (such as `bot`) live until that task ends.
- **Exceptions**: the library catches any exception that a callback throws and logs it at ERROR level. Other callbacks and later events are not affected. An exception left uncaught in an `AsyncTask` calls `std::terminate` directly (`coroutine.h:397-401`).
- **Threads**: a callback starts running on the IO thread that received the request, or the reverse WebSocket connection. When the callback calls a `OneBot` method, it resumes on the same thread. Over HTTP, the request is sent on that thread's event loop too.
- Calling `On*` when neither `event_path` nor `ws_path` is configured logs a WARN line, and the callback never runs.

## Reverse WebSocket

With `ws_path` set, the OneBot implementation connects to the plugin as a WebSocket client (`ws-reverse.md` in the OneBot v11 standard), keeps the connection open, and reconnects when it drops. Events and action calls share the one connection.

The route checks the upgrade request before drogon accepts the connection:

| Case | Status |
|---|---|
| `X-Client-Role` is set but is not `Universal` | 400 |
| An access token is set, but `Authorization` is missing | 401 |
| `Authorization` is not `Bearer <access token>` | 403 |
| Otherwise | 101, the connection opens |

- The standard's separate `API` and `Event` clients are rejected; NapCat always connects as `Universal`. The plugin logs the `X-Self-ID` of each connection.
- One connection is open at a time. A new connection closes the open one, with close code 1000 and the reason `replaced by a new connection`, so a stale connection never blocks NapCat's reconnect. The plugin therefore serves one bot account: two NapCat instances connected to the same `ws_path` replace each other at every reconnect.
- Events arrive in the same JSON as HTTP POST reports, unsigned, because the standard has no signature over WebSocket, and run the same callbacks.
- Without `base_url`, the `OneBot` client sends each call as `{"action": ..., "params": ..., "echo": ...}` over the open connection and matches the response by its `echo`:
  - With no connection open, the call fails at once with `Connection`.
  - When the connection closes or is replaced before the response arrives, the call fails with `Connection`.
  - Without a response within `timeout`, the call fails with `Timeout`, and a late response is dropped. With `timeout` 0, the call waits until the connection closes.
- NapCat answers a failed action over WebSocket with the standard's WebSocket retcodes: 1400 when its parameter check fails (400 over HTTP), 1404 for an unknown action, and 1200 when the action throws (both 200 over HTTP). All of them are `Action` errors.
- drogon closes a WebSocket connection when a message from the client exceeds the app's `client_max_websocket_message_size`, 128 KiB by default, and logs `The size of the WebSocket message is too large!`. NapCat sends each response as one message, for example every member of a large group, or a voice as base64. Raise the limit in the `app` section of config.json, for example `"client_max_websocket_message_size": "64M"`; NapCat itself accepts messages of up to 50 MB.
- drogon sends a WebSocket ping every 30 seconds, which NapCat answers.
- NapCat's streaming actions, which answer one call with several messages, are not supported: the first message completes the call.

## Error handling

The OneBot v11 HTTP API answers with a non-200 status only when the request itself is faulty (400, 401, 403, 404, 406). A failed action still gets 200, and the `status` in the response body tells the two cases apart.

| `ErrorKind` | Meaning | `status_code` | `retcode` | `body` |
|---|---|---|---|---|
| `Connection` | No response arrived: DNS, TLS, connection refused, and so on; or no reverse WebSocket connection was open, or it closed first | 0 | 0 | Empty |
| `Timeout` | No response within `timeout` seconds | 0 | 0 | Empty |
| `Status` | Non-2xx, over HTTP only: no token (401), wrong token (403 from NapCat), unknown action (404), and so on | HTTP status code | 0 | Raw response body |
| `ResponseValidation` | A 2xx response that is not JSON, or a response that has no `status` string, or has an unknown `status`; or a typed action's `data` lacks a required field or has a field of the wrong type | 0 | 0 | Response content, or `data` |
| `Action` | The response's `status` is `failed` | 0 | The response's `retcode` | Response content |

- A `status` of `ok` or `async` counts as success. With `async`, `data` is null, and the result of the action cannot be known.
- `message` includes the response's `message`, or its `wording` when `message` is empty. Both fields are extensions from NapCat and other implementations; the v11 standard only has `status`, `retcode`, and `data`.
- NapCat answers an unknown action over HTTP with HTTP 200, `status` `failed`, and `retcode` 200, so it is an `Action` error, not a `Status` 404. Over the reverse WebSocket, its retcode is 1404; the "Reverse WebSocket" section lists the others.
- On Windows, a refused connection takes about 2 seconds to report, because the system retries the connect first. With a `timeout` shorter than that, the request fails with `Timeout` instead of `Connection`. On Linux, a refused connection fails immediately.

## Threads and event loops

- Only coroutine APIs are provided. drogon's documentation recommends asynchronous APIs or coroutines. A synchronous API blocks the IO thread, and waiting synchronously on the loop that the client runs on also deadlocks.
- `HttpTransport::Call` creates a new `HttpClient` for each request on the calling thread's event loop (or on the main loop when the calling thread runs no loop), and keeps it in the coroutine frame until the response arrives. drogon's `HttpClient` defaults to pipelining depth 0, so sharing one client would serialize concurrent requests.
- `ReverseWebSocket::Call` may run on any thread. It sends the request on the open connection, runs its timeout on the calling thread's event loop (or on the main loop), and resumes the caller there too, so a coroutine resumes on its own thread over either transport.
- Coroutines take their parameters by value. Each action method builds the request body and returns the task of `details::call_and_read` or `details::call_and_discard`, which takes the transport as a coroutine parameter, so a request completes even if the `OneBot` is destroyed before the task starts or while the request is in flight.

## Known limitations

- There is no forward WebSocket. The reverse WebSocket accepts only `Universal` clients and one connection at a time, so it serves one bot account, and it does not support NapCat's streaming actions.
- Quick operations in the report response are not supported.
- The typed actions are listed in the table under "Calling actions". Use `CallAction` for the other actions for now.
- `set_group_anonymous` and `set_group_anonymous_ban` have no typed method, because NapCat does not register them.
- `shake`, `anonymous`, `share`, `poke`, `xml`, `location`, and custom `music` have no `MessageSegment` function, because NapCat cannot send them. It rejects `shake`, `anonymous`, `share`, and custom `music`, drops `poke` and `xml`, and sends `location` without its coordinates. For other implementations, build them through `type` and `data`. NapCat pokes through its own actions, such as `group_poke`, which `CallAction` reaches.
- Only the notices under "Notice details" have typed fields of their own. For the other notices, only the common fields and the subtypes are typed. Read the other fields from `raw`.
- `MessageEvent::anonymous`, the `Sender` fields `sex`, `age`, `area`, `level`, and `title`, and the `notify` notices `lucky_king` and `honor` have no typed member, because NapCat never sends them. For other implementations, read them from `raw`.

## Tests

The tests use drogon's own test framework, `drogon/drogon_test.h`, which is installed with drogon, so no extra dependency is needed. Each `tests/test_*.cpp` builds into one program, which is registered with CTest.

- Run all tests: `just test`, which is equivalent to `ctest --preset linux-debug`.
- Run one program: `just run test_events`.
- Run one test case: `build/linux-debug/tests/test_events -r RejectsWrongSignature`. Add `-s` to print passing checks, or `-l` to list the test cases.

All tests run offline. The tests that need a server use the same app as a stand-in for NapCat.

| Program | What it covers |
|---|---|
| `test_config` | Calls `initAndStart` directly without starting the app: the required keys (`base_url` or `ws_path`), reading both `_env` keys and their errors, `event_path` and `ws_path` validation, the WARN lines for an event route without `secret_env` and a WebSocket route without `access_token_env`, and that the config.json in the repository passes validation. |
| `test_actions` | A fake NapCat HTTP API. `CallAction`'s request body, `Authorization`, and `base_url` path prefix; `async`; all five `ErrorKind`s (`failed`, `wording` only, 404, 403, non-JSON, no `status`, unknown `status`, timeout, connection failure); tasks awaited after their `OneBot` is gone; and for the typed actions, success with `data` shaped like NapCat's, missing or mistyped `data` fields, `Action` errors, and `no_cache` and the `send_msg` targets being sent only when set. |
| `test_events` | The signed event route. RFC 2202 vectors check the test's own HMAC; a NapCat signature computed independently with Python; uppercase signatures; a group message with string IDs; the 401, 403, and 400 rejections, with no callback run; dispatch of all four event types; typed notice details, and a notice missing a typed field; `message_sent` dropped; several callbacks all run; the service continuing after a callback throws; the response not waiting for a slow callback; a callback registered after the listeners started being dropped. |
| `test_events_unsigned` | The event route without `secret_env`: unsigned events are accepted, and any `X-Signature` is ignored. |
| `test_reverse_ws` | A fake NapCat that connects to `ws_path` the way NapCat's WebSocket client does, with config.json loaded without `base_url`. The 400, 401, and 403 handshake rejections; events of each kind over the connection; bad messages dropped while the connection stays open; typed and raw actions over the connection, with responses matched by `echo` out of order; `Action` errors with a WebSocket retcode, and malformed responses; timeouts; no connection; a call pending while the connection closes or is replaced; a response above drogon's default limit of 128 KiB; and a message callback that replies over the connection and resumes on its own loop. |
| `test_segments` | Without starting the app: the segments that each `MessageSegment` function builds, including IDs sent as strings, and a message that mixes built and hand-written segments. |

## HTTP request observation

Applications can set `OneBotConfig::http_trace or OneBotClient::NewOneBotClient(callback)` to receive an owned `HttpTrace` after every outgoing HTTP attempt, including non-2xx responses and transport failures. The callback runs on the request loop and must not block; concurrent requests may call it concurrently. Observer exceptions are ignored and never change request results or retries.

Records contain the method, URL, HTTP status (zero for a connection failure), elapsed milliseconds, error text, and bounded request/response text. Bodies retain at most 64 KiB with truncation flags and UTF-8 prefix boundaries. Authorization headers are excluded, and the configured bearer credential is redacted from retained text. Applications must additionally redact their own payload secrets and images before exposing diagnostics. The observer is programmatic and is never accepted from config.json.

Reverse WebSocket actions do not emit HTTP records. The original no-argument factory remains available. Offline regression coverage is in `test_actions`.

### NapCat favourite stickers

`FetchCustomFaceDetail({.count = 48})` returns typed `CustomFace` entries, retaining the complete entry in `raw`. NapCat currently calls the numeric identifier `emoId`; `emojiId` is accepted too. `res_id` identifies a favourite for deletion. `SetCustomFaceDesc` sends `emoji_id`, `res_id`, `md5`, and `desc`; `DeleteCustomFace` sends resource IDs as `ids`. `AddCustomFace` takes an existing file path **on the NapCat host**; NapCat stats that path even if metadata is supplied. These four methods are NapCat extensions and an implementation without them returns an action error. Mutation results remain raw JSON because NapCat delegates them directly to its native service.

The payloads were checked against NapCat's [CustomFace actions](https://github.com/NapNeko/NapCatQQ/blob/main/packages/napcat-onebot/action/extends/CustomFace.ts) and [native message service](https://github.com/NapNeko/NapCatQQ/blob/main/packages/napcat-core/services/NodeIKernelMsgService.ts).
