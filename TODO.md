# TODO

尚未支持的 OneBot v11 功能，全部实现后删除本文件。

未类型化的动作现在可以用 `CallAction` 调用，未类型化的事件字段可以从 `raw` 读取。实现前先对照 NapCat 源码确认实际行为。

## 通信方式

> 先不实现，先确保 HTTP 格式下所有的功能已经实现

- [ ] 正向 WebSocket
- [ ] 反向 WebSocket
- [ ] 消息的字符串格式（CQ 码）和 `auto_escape`
- [ ] 快速操作（在上报的响应体里返回 `reply` 等）。现在的事件路由先回 204、再运行回调，这个设计排除了快速操作。

## 动作

消息：

- [x] `send_msg`：按 `message_type` 发送私聊或群消息
- [x] `get_forward_msg`：获取合并转发的内容
- [x] `send_like`：点赞

群管理：

- [x] `set_group_kick`：踢出成员
- [x] `set_group_ban`：禁言成员
- [x] `set_group_whole_ban`：全员禁言
- [x] `set_group_admin`：设置管理员
- [x] `set_group_card`：设置群名片
- [x] `set_group_name`：设置群名
- [x] `set_group_leave`：退群或解散群
- [x] `set_group_special_title`：设置专属头衔

请求处理：

- [x] `set_friend_add_request`：处理好友申请
- [x] `set_group_add_request`：处理加群申请和邀请

信息查询：

- [x] `get_friend_list`：好友列表
- [x] `get_group_list`：群列表
- [x] `get_group_member_list`：群成员列表
- [x] `get_group_honor_info`：群荣誉信息

凭证：

- [x] `get_cookies`
- [x] `get_csrf_token`
- [x] `get_credentials`

文件和能力：

- [x] `get_record`：获取语音文件
- [x] `get_image`：获取图片文件
- [x] `can_send_image`
- [x] `can_send_record`

实现状态：

- [x] `get_status`：运行状态
- [x] `get_version_info`：版本信息
- [x] `set_restart`：重启实现
- [x] `clean_cache`：清理缓存

## 消息段构造函数

已有 `Text`、`At`、`AtAll`、`Reply`、`Image`、`Face`、`Record`、`Video`、`ContactFriend`、`ContactGroup`、`Json`、`Music`、`Rps`、`Dice`、`Node`、`NodeCustom`、`Forward`。

- [x] `record`：语音
- [x] `video`：短视频
- [x] `rps`：猜拳
- [x] `dice`：掷骰子
- [x] `contact`：推荐好友或群
- [x] `music`：音乐分享
- [x] `forward`：按 NapCat 的 `res_id` 再次发送合并转发；收到的 `forward` 用 `get_forward_msg` 获取内容
- [x] `node`：按 ID 引用的合并转发节点
- [x] `node`：自定义节点
- [x] `json`：JSON 消息

## 事件字段

通知事件，类型化字段在 `NoticeEvent::detail` 里：

- [x] `group_upload`：群文件上传，`group_id`、`user_id`、`file`
- [x] `group_admin`：管理员变动，`group_id`、`user_id`
- [x] `group_decrease`：成员减少，`group_id`、`operator_id`、`user_id`
- [x] `group_increase`：成员增加，`group_id`、`operator_id`、`user_id`
- [x] `group_ban`：禁言，`group_id`、`operator_id`、`user_id`、`duration`
- [x] `friend_add`：新好友，`user_id`
- [x] `group_recall`：群消息撤回，`group_id`、`user_id`、`operator_id`、`message_id`
- [x] `friend_recall`：好友消息撤回，`user_id`、`message_id`
- [x] `notify` 的 `poke`：戳一戳，`group_id`、`user_id`、`target_id`

请求事件：

- [x] `friend`：`user_id`、`comment`、`flag`
- [x] `group`：`group_id`、`user_id`、`comment`、`flag`

元事件：

- [x] `heartbeat`：`status`、`interval`
