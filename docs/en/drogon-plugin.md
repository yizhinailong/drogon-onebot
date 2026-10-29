# drogon plugin: `drogon_onebot::OneBotClient`

English · [简体中文](../zh-CN/drogon-plugin.md)

This page describes how to add drogon-onebot to a drogon app as a plugin, its configuration, lifecycle, and usage, and the design constraints that follow from drogon's threading model. The protocol is OneBot v11, and the library implements two parts:

- Calling actions: the HTTP API, through the `OneBot` client.
- Receiving events: HTTP POST reporting, through the event route that the plugin registers.

The reference OneBot implementation is NapCat.

## Overview

`drogon_onebot::OneBotClient` derives from `drogon::Plugin<OneBotClient>` and does three things:

1. When drogon starts, it reads and validates its configuration (`OneBotConfig`) from config.json.
2. When `event_path` is set, it registers a `POST` route at that path. The route receives the events that the OneBot implementation reports and dispatches them to the callbacks registered with `OnMessage` and the other `On*` methods.
3. `NewOneBotClient()` returns a `OneBot` client bound to that configuration. The client calls actions, for example to reply to a message.

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

| Key | Required | Description |
|---|---|---|
| `base_url` | Yes | The HTTP API address of the OneBot implementation, optionally with a path prefix. Actions go to `<base_url>/<action>`. |
| `access_token_env` | No | **Name of the environment variable** that holds the access token. Without it, no `Authorization` header is sent. |
| `event_path` | No | Route path that receives events. It must start with `/`. Without it, no route is registered, and the plugin can send but not receive. |
| `secret_env` | No | **Name of the environment variable** that holds the secret that signs reported events. Without it, signatures are not checked and unsigned events are accepted. |

- Neither secret goes into config.json. Both are read only from environment variables.
- The access token is sent as `Authorization: Bearer <token>` and must match the token of the OneBot implementation's HTTP API.
- The secret checks the `X-Signature: sha1=<hex of HMAC-SHA1(secret, body)>` header of each event.
- `initAndStart` throws `std::invalid_argument` in any of these cases:
  - `base_url` is missing or empty;
  - `event_path` does not start with `/`;
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

## Lifecycle

```
app().loadConfigFile("config.json")       reads the plugins section
app().run()
 ├─ OneBotClient::initAndStart(config)    parses and validates the config, registers the event_path route, throws on failure
 ├─ routers initialized                   no more routes can be registered after this
 ├─ beginning advices                     getPlugin<OneBotClient>() works from here on; register callbacks here
 ├─ startListening()                      each IO loop starts listening asynchronously, and events start to arrive
 └─ event loops run and handle requests
app().quit()                              drogon calls OneBotClient::shutdown()
```

- `getPlugin` returns a `OneBotClient*` that drogon owns. Don't delete it.
- The event route is registered in `initAndStart`. drogon initializes plugins before routers (`HttpAppFrameworkImpl.cc:663-671`), and drogon's own `PromExporter` plugin registers its route the same way (`PromExporter.cc:18`).
- With static linking, the app's code must reference `OneBotClient` (one call to `getPlugin<drogon_onebot::OneBotClient>()` is enough). Otherwise the linker drops `onebot_client.o`, and drogon prints `Plugin drogon_onebot::OneBotClient undefined!` and keeps running.

## Calling actions

- `OneBot` is a value handle whose only shared state is one `std::shared_ptr<details::Transport>`. A copy only increments a reference count, so pass it into coroutines by value.
- Every method returns `drogon::Task<Result<T>>`, where `Result<T>` is `std::expected<T, Error>`. A failed action **does not throw**.
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
- `SetFriendAddRequest` takes the `flag` of the friend request event, read from `RequestEvent::raw` (`event.raw["flag"].asString()`). It sends `remark` only when it is not empty. NapCat fails with `Action` when no pending request has that `flag`; otherwise it does not wait for QQ's answer, so the action succeeds even if QQ refuses.
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
| `OnNotice(NoticeHandler)` | `notice` | `NoticeEvent` | `notice_type`, `sub_type` |
| `OnRequest(RequestHandler)` | `request` | `RequestEvent` | `request_type`, `sub_type` |
| `OnMetaEvent(MetaEventHandler)` | `meta_event` | `MetaEvent` | `meta_event_type`, `sub_type` |

- Every event has `time`, `self_id`, and the whole original event in `raw` (`Json::Value`). Read the fields that have no member of their own from `raw`, such as the `flag` of a request event or the `interval` of a heartbeat.
- The callback type is `std::function<drogon::Task<>(Event)>`.
- Other `post_type` values, such as the `message_sent` that NapCat reports when `reportSelfMessage` is on, get 204 and are dropped.
- NapCat sometimes sends `user_id` and `group_id` as strings. They are parsed as numbers.

### Route responses

| Case | Status | Callbacks |
|---|---|---|
| A secret is set, but `X-Signature` is missing | 401 | Not run |
| `X-Signature` does not match the request body | 403 | Not run |
| The body is not a JSON object; `post_type` or a required typed field is missing; an integer field is not an integer; `message` is not an array of message segments | 400, with the reason in the response body | Not run |
| A valid event | 204 | Started in the background after the response |

The route answers first and runs the callbacks afterwards. NapCat keeps its report request open until it gets a response, and a callback may wait for seconds (for example, on an LLM call). So quick operations (returning `reply` or other fields in the response body) are not supported. Always reply through actions such as `SendPrivateMsg` and `SendGroupMsg`.

### Callback rules

- **When to register**: only after the plugin is initialized and before the listeners start. That means in a beginning advice, or in `initAndStart` of another plugin that lists this one under `dependencies`. The route reads the callback lists on IO threads without a lock.
- **Lifetime**: each event starts one `drogon::async_run` task per callback, and each task frame holds its own copy of the callback and the event. So the captures of a callback lambda (such as `bot`) live until that task ends.
- **Exceptions**: the library catches any exception that a callback throws and logs it at ERROR level. Other callbacks and later events are not affected. An exception left uncaught in an `AsyncTask` calls `std::terminate` directly (`coroutine.h:397-401`).
- **Threads**: a callback starts running on the IO thread that received the request. When the callback calls a `OneBot` method, the request is sent on that thread's event loop too, and the callback resumes on the same thread.
- Calling `On*` when `event_path` is not configured logs a WARN line, and the callback never runs.

## Error handling

The OneBot v11 HTTP API answers with a non-200 status only when the request itself is faulty (400, 401, 403, 404, 406). A failed action still gets 200, and the `status` in the response body tells the two cases apart.

| `ErrorKind` | Meaning | `status_code` | `retcode` | `body` |
|---|---|---|---|---|
| `Connection` | No response arrived: DNS, TLS, connection refused, and so on | 0 | 0 | Empty |
| `Timeout` | No response within `timeout` seconds | 0 | 0 | Empty |
| `Status` | Non-2xx: no token (401), wrong token (403 from NapCat), unknown action (404), and so on | HTTP status code | 0 | Raw response body |
| `ResponseValidation` | A 2xx response that is not JSON, has no `status` string, or has an unknown `status`; or a typed action's `data` lacks a required field or has a field of the wrong type | 0 | 0 | Response content, or `data` |
| `Action` | The response's `status` is `failed` | 0 | The response's `retcode` | Response content |

- A `status` of `ok` or `async` counts as success. With `async`, `data` is null, and the result of the action cannot be known.
- `message` includes the response's `message`, or its `wording` when `message` is empty. Both fields are extensions from NapCat and other implementations; the v11 standard only has `status`, `retcode`, and `data`.
- NapCat answers an unknown action with HTTP 200, `status` `failed`, and `retcode` 200, so it is an `Action` error, not a `Status` 404.
- On Windows, a refused connection takes about 2 seconds to report, because the system retries the connect first. With a `timeout` shorter than that, the request fails with `Timeout` instead of `Connection`. On Linux, a refused connection fails immediately.

## Threads and event loops

- Only coroutine APIs are provided. drogon's documentation recommends asynchronous APIs or coroutines. A synchronous API blocks the IO thread, and waiting synchronously on the loop that the client runs on also deadlocks.
- `Transport::Call` creates a new `HttpClient` for each request on the calling thread's event loop (or on the main loop when the calling thread runs no loop), and keeps it in the coroutine frame until the response arrives. drogon's `HttpClient` defaults to pipelining depth 0, so sharing one client would serialize concurrent requests.
- Coroutines take their parameters by value. Every action method copies `m_transport` into the coroutine frame before its first `co_await`, so a request completes even if the `OneBot` is destroyed while the request is in flight.

## Known limitations

- Only the HTTP API and HTTP POST reporting are implemented. There is no forward or reverse WebSocket.
- Quick operations in the report response are not supported.
- The typed actions are listed in the table under "Calling actions". Use `CallAction` for the other actions for now.
- `set_group_anonymous` and `set_group_anonymous_ban` have no typed method, because NapCat does not register them.
- `shake`, `anonymous`, `share`, `poke`, `xml`, `location`, and custom `music` have no `MessageSegment` function, because NapCat cannot send them. It rejects `shake`, `anonymous`, `share`, and custom `music`, drops `poke` and `xml`, and sends `location` without its coordinates. For other implementations, build them through `type` and `data`. NapCat pokes through its own actions, such as `group_poke`, which `CallAction` reaches.
- For notice, request, and meta events, only the common fields and the subtypes are typed. Read the other fields from `raw`.

## Tests

The tests use drogon's own test framework, `drogon/drogon_test.h`, which is installed with drogon, so no extra dependency is needed. Each `tests/test_*.cpp` builds into one program, which is registered with CTest.

- Run all tests: `just test`, which is equivalent to `ctest --preset linux-debug`.
- Run one program: `just run test_events`.
- Run one test case: `build/linux-debug/tests/test_events -r RejectsWrongSignature`. Add `-s` to print passing checks, or `-l` to list the test cases.

All tests run offline. The tests that need a server use the same app as a stand-in for NapCat.

| Program | What it covers |
|---|---|
| `test_config` | Calls `initAndStart` directly without starting the app: the required key, reading both `_env` keys and their errors, `event_path` validation, and that the config.json in the repository passes validation. |
| `test_actions` | A fake NapCat HTTP API. `CallAction`'s request body, `Authorization`, and `base_url` path prefix; `async`; all five `ErrorKind`s (`failed`, `wording` only, 404, 403, non-JSON, no `status`, unknown `status`, timeout, connection failure); and for the typed actions, success with `data` shaped like NapCat's, missing or mistyped `data` fields, `Action` errors, and `no_cache` and the `send_msg` targets being sent only when set. |
| `test_events` | The signed event route. RFC 2202 vectors check the test's own HMAC; a NapCat signature computed independently with Python; uppercase signatures; a group message with string IDs; the 401, 403, and 400 rejections, with no callback run; dispatch of all four event types; `message_sent` dropped; several callbacks all run; the service continuing after a callback throws; the response not waiting for a slow callback. |
| `test_events_unsigned` | The event route without `secret_env`: unsigned events are accepted, and any `X-Signature` is ignored. |
| `test_segments` | Without starting the app: the segments that each `MessageSegment` function builds, including IDs sent as strings, and a message that mixes built and hand-written segments. |
