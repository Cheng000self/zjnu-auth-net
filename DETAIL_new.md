# zjnu_auth_net 详细技术文档

本文档提供 `zjnu_auth_net` 的完整技术说明，包括协议分析方法、Portal API 详解、脚本架构、运行环境、使用方法、退出码、常见问题和安全说明。首次使用建议先阅读 [README.md](README.md)，需要了解技术细节、进行二次开发或排障时再阅读本文档。

---

## 1. 项目概述

### 1.1 项目简介

本项目面向浙江师范大学校园网 Portal 认证场景，提供三种独立实现的命令行认证工具：

```text
zjnu_auth_lit.py   Python 版（推荐）
zjnu_auth_lit.sh   Bash 版（轻量级）
zjnu_auth_lit.cpp  C++ 版（可编译为静态二进制）
```

### 1.2 核心功能

三种实现的功能目标一致：

- **账号密码登录**：支持校园用户、校园电信、校园联通、校园移动四种运营商账号
- **手机验证码登录**：支持通过短信验证码进行认证
- **在线状态查询**：查询当前设备是否在线及登录账号
- **设备管理**：查看当前账号所有在线设备
- **设备注销**：注销当前设备的认证会话
- **连通性测试**：登录后自动测试外网连通性
- **安全设计**：不保存账号、密码、手机号、验证码等敏感信息

### 1.3 版本选择建议

- **Python 版**：交互体验最好，JSONP 解析最完整，推荐作为默认版本
- **Bash 版**：适合没有 Python 但有 `curl` 的 Linux 环境，依赖少，启动快
- **C++ 版**：适合编译为单个二进制后部署，支持完全静态编译，无运行时依赖

### 1.4 认证网关信息

**默认 Portal API 基础路径**：

```text
http://10.1.116.8:801/eportal/portal
```

**网关主机**：

```text
10.1.116.8
```

**相关门户页面**：

```text
https://portal.zjnu.edu.cn  （用户门户）
https://10.1.116.8          （认证网关）
```

**重要说明**：

- 实际认证请求使用 HTTP 协议，端口 801
- 工具必须运行在能访问该认证网关的校园网环境中
- 公网环境或普通家庭网络通常无法访问该网关
- 认证网关需要验证真实客户端 IP，因此不建议使用代理

---

## 2. 协议分析

本节介绍如何抓取和分析浙师大校园网 Portal 认证协议，适合需要理解认证流程、进行二次开发或排障的用户。

### 2.1 使用浏览器开发者工具抓包

浏览器开发者工具是最简单直观的抓包方式，适合快速分析 HTTP 请求。

#### 2.1.1 Chrome/Edge 开发者工具

1. **打开开发者工具**：
   - 按 `F12` 或 `Ctrl+Shift+I`（Windows/Linux）
   - 按 `Cmd+Option+I`（macOS）

2. **切换到 Network 标签**：
   - 点击顶部的 "Network"（网络）标签
   - 确保红色录制按钮处于激活状态
   - 勾选 "Preserve log"（保留日志）以防页面跳转时清空记录

3. **访问认证页面**：
   ```text
   https://portal.zjnu.edu.cn
   或
   https://10.1.116.8
   ```

4. **执行登录操作**：
   - 输入账号密码
   - 选择运营商
   - 点击登录按钮

5. **分析关键请求**：
   - 在 Network 面板中找到 `login` 请求
   - 点击该请求，查看右侧详情面板
   - **Headers 标签**：查看请求 URL、请求方法（GET/POST）、请求头
   - **Payload/Query String Parameters 标签**：查看请求参数
   - **Response 标签**：查看服务器返回的 JSONP 响应

#### 2.1.2 关键请求示例

**登录请求**：

```text
Request URL: http://10.1.116.8:801/eportal/portal/login?callback=dr1234567890&jsVersion=4.2.1&v=1234567890&lang=zh-cn&user_account=,1,202320701256&user_password=yourpassword&wlan_user_ip=10.1.2.3&wlan_user_ipv6=&wlan_user_mac=aabbccddeeff&wlan_ac_ip=&wlan_ac_name=&terminal_type=1&login_method=1

Request Method: GET
Status Code: 200 OK
```

**响应示例**：

```javascript
dr1234567890({"result":"1","msg":"认证成功","ret_code":"0"})
```

#### 2.1.3 Firefox 开发者工具

Firefox 的操作类似，按 `F12` 打开开发者工具，切换到"网络"标签，执行登录操作后查看请求详情。

### 2.2 使用 Wireshark 抓包

Wireshark 是专业的网络协议分析工具，适合深入分析网络层面的通信细节。

#### 2.2.1 安装 Wireshark

**Debian/Ubuntu**：

```bash
sudo apt update
sudo apt install wireshark
```

**macOS**：

```bash
brew install --cask wireshark
```

**Windows**：

从 [Wireshark 官网](https://www.wireshark.org/download.html) 下载安装包。

#### 2.2.2 抓包步骤

1. **启动 Wireshark**：
   ```bash
   sudo wireshark
   ```

2. **选择网络接口**：
   - 选择连接到校园网的网卡（如 `eth0`、`wlan0`、`en0`）
   - 双击接口名称开始抓包

3. **设置过滤器**：
   - 在顶部过滤器栏输入：
     ```text
     http and ip.addr == 10.1.116.8
     ```
   - 这将只显示与认证网关 `10.1.116.8` 的 HTTP 通信

4. **执行登录操作**：
   - 在浏览器或使用本工具执行登录
   - Wireshark 会捕获所有相关的 HTTP 请求和响应

5. **分析捕获的数据包**：
   - 找到 `GET /eportal/portal/login` 请求
   - 右键点击数据包，选择 "Follow" → "HTTP Stream"
   - 查看完整的 HTTP 请求和响应内容

#### 2.2.3 导出 HTTP 对象

Wireshark 可以导出捕获的 HTTP 对象：

1. 菜单栏：`File` → `Export Objects` → `HTTP`
2. 在列表中找到 `/eportal/portal/login` 等请求
3. 点击 "Save" 保存响应内容

### 2.3 使用 tcpdump 抓包

`tcpdump` 是命令行抓包工具，适合在无图形界面的服务器环境中使用。

#### 2.3.1 基本抓包命令

**抓取与认证网关的所有通信**：

```bash
sudo tcpdump -i eth0 -w zjnu_auth.pcap host 10.1.116.8
```

参数说明：
- `-i eth0`：指定网卡接口（根据实际情况修改）
- `-w zjnu_auth.pcap`：保存到文件
- `host 10.1.116.8`：只抓取与该主机的通信

**只抓取 HTTP 流量**：

```bash
sudo tcpdump -i eth0 -w zjnu_auth_http.pcap 'host 10.1.116.8 and tcp port 801'
```

**实时查看抓包内容**：

```bash
sudo tcpdump -i eth0 -A 'host 10.1.116.8 and tcp port 801'
```

参数说明：
- `-A`：以 ASCII 格式显示数据包内容

#### 2.3.2 分析 pcap 文件

使用 Wireshark 打开 tcpdump 生成的 `.pcap` 文件：

```bash
wireshark zjnu_auth.pcap
```

或使用 `tcpdump` 读取：

```bash
tcpdump -r zjnu_auth.pcap -A
```

### 2.4 关键请求和响应分析

#### 2.4.1 登录请求参数解析

**请求 URL 结构**：

```text
http://10.1.116.8:801/eportal/portal/login?<参数>
```

**核心参数**：

| 参数名 | 示例值 | 说明 |
|--------|--------|------|
| `callback` | `dr1234567890` | JSONP 回调函数名，格式为 `dr` + 时间戳 |
| `jsVersion` | `4.2.1` | JavaScript 版本号 |
| `v` | `1234567890` | 时间戳，用于防止缓存 |
| `lang` | `zh-cn` | 语言设置 |
| `user_account` | `,1,202320701256` | 用户账号，格式：`,<运营商编号>,<账号>` |
| `user_password` | `yourpassword` | 密码明文（HTTP 传输，不安全） |
| `wlan_user_ip` | `10.1.2.3` | 客户端 IP 地址 |
| `wlan_user_ipv6` | `` | IPv6 地址（通常为空） |
| `wlan_user_mac` | `aabbccddeeff` | 客户端 MAC 地址（小写，无分隔符） |
| `wlan_ac_ip` | `` | AC（接入控制器）IP（通常为空） |
| `wlan_ac_name` | `` | AC 名称（通常为空） |
| `terminal_type` | `1` | 终端类型（1 表示 PC） |
| `login_method` | `1` | 登录方式（1 表示账号密码） |

**运营商编号**：

```text
1 - 校园用户
2 - 校园电信（账号需追加 @dx 后缀）
3 - 校园联通（账号需追加 @lt 后缀）
4 - 校园移动
```

#### 2.4.2 响应格式解析

**成功响应示例**：

```javascript
dr1234567890({"result":"1","msg":"认证成功","ret_code":"0"})
```

**失败响应示例**：

```javascript
dr1234567890({"result":"0","msg":"密码错误","ret_code":"1"})
```

**响应字段说明**：

| 字段名 | 说明 |
|--------|------|
| `result` | 结果标志，`1`/`ok`/`true` 表示成功，`0`/`fail`/`false` 表示失败 |
| `msg` | 消息内容，成功或失败的描述 |
| `ret_code` | 返回码，`0` 表示成功，`2` 表示已在线，其他值表示各种错误 |

**特殊情况**：

- `ret_code=2`：当前 IP 已在线，工具将其视为登录成功
- 响应可能包含其他字段如 `message`、`error`、`error_msg` 等，工具会按优先级读取

#### 2.4.3 短信验证码请求分析

**发送验证码请求**：

```text
GET http://10.1.116.8:801/eportal/portal/sms?callback=dr1234567890&jsVersion=4.2.1&v=1234567890&lang=zh-cn&telephone=13800138000&mac=aabbccddeeff&ip=10.1.2.3&ipv6=&bind=0&page_index=&prefix=&sms_type=0
```

**验证码登录请求**：

验证码登录仍使用 `/login` 接口，但 `user_account` 使用手机号，`user_password` 使用验证码：

```text
user_account=,1,13800138000
user_password=123456
```

#### 2.4.4 在线状态查询分析

**查询当前设备**：
```text
GET http://10.1.116.8:801/eportal/portal/online_list?callback=dr1234567890&jsVersion=4.2.1&v=1234567890&lang=zh-cn
```

**查询账号所有设备**：

```text
GET http://10.1.116.8:801/eportal/portal/online_list?callback=dr1234567890&jsVersion=4.2.1&v=1234567890&lang=zh-cn&user_account=202320701256
```

**响应示例**：

```javascript
dr1234567890({
  "result":"1",
  "list":[
    {
      "user_account":"202320701256",
      "online_ip":"10.1.2.3",
      "online_mac":"AA:BB:CC:DD:EE:FF",
      "online_time":"2026-05-10 10:30:00",
      "time_long":"3600",
      "is_owner_ip":"1",
      "is_perceive":"0"
    }
  ]
})
```

#### 2.4.5 注销请求分析

**主注销方式（MAC 解绑）**：

```text
GET http://10.1.116.8:801/eportal/portal/mac/unbind?callback=dr1234567890&jsVersion=4.2.1&v=1234567890&lang=zh-cn&user_account=202320701256&wlan_user_mac=AABBCCDDEEFF&wlan_user_ip=167837187
```

注意：
- `wlan_user_mac` 使用大写，无分隔符
- `wlan_user_ip` 是 IP 地址转换为整数：`a.b.c.d → a×256³ + b×256² + c×256 + d`
- 例如 `10.1.2.3` → `10×16777216 + 1×65536 + 2×256 + 3 = 167837187`

**备用注销方式（完整参数 logout）**：

```text
GET http://10.1.116.8:801/eportal/portal/logout?callback=dr1234567890&jsVersion=4.2.1&v=1234567890&lang=zh-cn&login_method=1&user_account=drcom&user_password=123&ac_logout=0&register_mode=1&wlan_user_ip=10.1.2.3&wlan_user_ipv6=&wlan_vlan_id=1&wlan_user_mac=AABBCCDDEEFF&wlan_ac_ip=&wlan_ac_name=
```

### 2.5 使用 curl 模拟请求

在理解协议后，可以使用 `curl` 手动模拟请求进行测试：

**模拟登录请求**：

```bash
curl -G "http://10.1.116.8:801/eportal/portal/login" \
  --data-urlencode "callback=dr$(date +%s)" \
  --data-urlencode "jsVersion=4.2.1" \
  --data-urlencode "v=$(date +%s)" \
  --data-urlencode "lang=zh-cn" \
  --data-urlencode "user_account=,1,202320701256" \
  --data-urlencode "user_password=yourpassword" \
  --data-urlencode "wlan_user_ip=10.1.2.3" \
  --data-urlencode "wlan_user_ipv6=" \
  --data-urlencode "wlan_user_mac=aabbccddeeff" \
  --data-urlencode "wlan_ac_ip=" \
  --data-urlencode "wlan_ac_name=" \
  --data-urlencode "terminal_type=1" \
  --data-urlencode "login_method=1"
```

**查询在线状态**：

```bash
curl -G "http://10.1.116.8:801/eportal/portal/online_list" \
  --data-urlencode "callback=dr$(date +%s)" \
  --data-urlencode "jsVersion=4.2.1" \
  --data-urlencode "v=$(date +%s)" \
  --data-urlencode "lang=zh-cn"
```

---

## 3. Portal 接口详解

本节详细说明浙师大校园网 Portal 认证系统的各个 API 接口，包括请求参数、响应格式和特殊处理逻辑。

### 3.1 公共请求参数

所有 Portal 接口都使用 JSONP 格式返回数据，因此每个请求都需要携带以下公共参数：

| 参数名 | 示例值 | 说明 |
|--------|--------|----|
| `callback` | `dr1715328000` | JSONP 回调函数名，格式为 `dr` + 时间戳 |
| `jsVersion` | `4.2.1` | JavaScript 版本号，固定值 |
| `v` | `1715328000` | 时间戳，用于防止缓存 |
| `lang` | `zh-cn` | 语言设置，固定为中文 |

**响应格式**：

所有接口返回 JSONP 格式：

```javascript
dr1715328000({"result":"1","msg":"操作成功","ret_code":"0"})
```

**成功判断规则**：

工具按以下规则判断请求是否成功：

1. `result` 字段为 `1`、`ok`、`true`（字符串或布尔值）
2. 如果没有 `result` 字段，则检查是否存在 `list` 字段（在线查询接口）

**消息字段优先级**：

工具按以下顺序读取响应消息：

```text
msg → message → ret_msg → error → error_msg → content
```

**错误码字段优先级**：

```text
ret_code → code → err_code → error_code
```

### 3.2 登录接口 (/login)

#### 3.2.1 账号密码登录

**接口路径**：

```text
GET /eportal/portal/login
```

**请求参数**：

| 参数名 | 必填 | 示例值 | 说明 |
|--------|------|--------|------|
| `login_method` | 是 | `1` | 登录方式，1 表示账号密码 |
| `user_account` | 是 | `,1,202320701256` | 用户账号，格式：`,<运营商编号>,<账号>` |
| `user_password` | 是 | `yourpassword` | 密码明文 |
| `wlan_user_ip` | 是 | `10.1.2.3` | 客户端 IP 地址 |
| `wlan_user_ipv6` | 否 | `` | IPv6 地址，通常为空 |
| `wlan_user_mac` | 是 | `aabbccddeeff` | 客户端 MAC 地址（小写，无分隔符） |
| `wlan_ac_ip` | 否 | `` | AC（接入控制器）IP，通常为空 |
| `wlan_ac_name` | 否 | `` | AC 名称，通常为空 |
| `terminal_type` | 是 | `1` | 终端类型，1 表示 PC |

**运营商编号与账号后缀**：

| 运营商编号 | 运营商名称 | 账号后缀 | 示例 |
|-----------|-----|---------|------|
| `1` | 校园用户 | 无 | `202320701256` |
| `2` | 校园电信 | `@dx` | `202320701256@dx` |
| `3` | 校园联通 | `@lt` | `202320701256@lt` |
| `4` | 校园移动 | 无 | `202320701256` |

工具会自动处理后缀：
- 选择校园电信时，如果账号不含 `@dx`，自动追加
- 选择校园联通时，如果账号不含 `@lt`，自动追加
- 校园用户和校园移动不追加后缀

**成功响应示例**：

```javascript
dr1715328000({"result":"1","msg":"认证成功","ret_code":"0"})
```

**失败响应示例**：

```javascript
dr1715328000({"result":"0","msg":"密码错误","ret_code":"1"})
```

**特殊情况处理**：

- `ret_code=2`：表示当前 IP 已在线，工具将其视为登录成功
- 密码错误、账号不存在、设备数超限等错误会在 `msg` 字段中说明

#### 3.2.2 手机验证码登录

手机验证码登录仍使用 `/login` 接口，但参数有所不同：

**请求参数差异**：

| 参数名 | 值 | 说明 |
|--------|-----|------|
| `user_account` | `,1,13800138000` | 使用手机号代替学号 |
| `user_password` | `123456` | 使用短信验证码代替密码 |

其他参数与账号密码登录相同。

### 3.3 短信接口 (/sms)

**接口路径**：

```text
GET /eportal/portal/sms
```
**请求参数**：

| 参数名 | 必填 | 示例值 | 说明 |
|--------|------|----|------|
| `telephone` | 是 | `13800138000` | 手机号码 |
| `mac` | 是 | `aabbccddeeff` | 客户端 MAC 地址（小写，无分隔符） |
| `ip` | 是 | `10.1.2.3` | 客户端 IP 地址 |
| `ipv6` | 否 | `` | IPv6 地址，通常为空 |
| `bind` | 是 | `0` | 绑定标志，固定为 0 |
| `page_index` | 否 | `` | 页面索引，通常为空 |
| `prefix` | 否 | `` | 前缀，通常为空 |
| `sms_type` | 是 | `0` | 短信类型，固定为 0 |

**成功响应示例**：

```javascript
dr1715328000({"result":"1","msg":"验证码已发送","ret_code":"0"})
```

**注意事项**：

- 工具不做本地手机号格式校验
- 工具不做本地 60 秒重复发送限制
- 是否允许某个号码、是否重复发送过快，均由 Portal 后端决定
- 验证码有效期通常为 5-10 分钟

### 3.4 在线查询接口 (/online_list)

#### 3.4.1 查询当前设备

**接口路径**：

```text
GET /eportal/portal/online_list
```

**请求参数**：

只需要公共参数（callback、jsVersion、v、lang）。

**响应示例**：

```javascript
dr1715328000({
  "result":"1",
  "list":[
    {
   "user_account":"202320701256",
      "online_ip":"10.1.2.3",
      "online_mac":"AA:BB:CC:DD:EE:FF",
      "online_time":"2026-05-10 10:30:00",
      "time_long":"3600",
      "is_owner_ip":"1",
      "is_perceive":"0"
    }
  ]
})
```

**响应字段说明**：

| 字段名 | 说明 |
|--------|------|
| `user_account` | 登录账号 |
| `online_ip` | 在线设备 IP |
| `online_mac` | 在线设备 MAC 地址 |
| `online_time` | 登录时间 |
| `time_long` | 在线时长（秒） |
| `is_owner_ip` | 是否为当前 IP，1 表示是 |
| `is_perceive` | 感知标志 |

#### 3.4.2 查询账号所有设备

**接口路径**：

```text
GET /eportal/portal/online_list?user_account=<账号>
```

**请求参数**：

| 参数名 | 必填 | 示例值 | 说明 |
|--------|------|--------|------|
| `user_account` | 是 | `202320701256` | 要查询的账号（不含运营商前缀） |

**响应格式**：

与查询当前设备相同，但 `list` 数组可能包含多个设备。

#### 3.4.3 设备列表合并策略

登录成功后，工具会合并三个来源的设备信息：

1. `/mac/find?user_account=<账号>` - MAC 查询接口
2. `/online_list?user_account=<账号>` - 账号级在线列表
3. 当前设备 `online_list` 快照 - 当前会话信息

**合并规则**：

- 按 IP 或 MAC 地址去重
- 如果账号级列表漏掉当前设备，工具会补充进去
- 这样做的原因是账号级接口有时只返回部分设备

**当前设备判断逻辑**：

1. `online_ip` 等于本机认证 IP
2. 或 `is_owner_ip=1`
3. 如果都没有命中，则取列表第一项作为当前设备

### 3.5 注销接口 (/logout, /mac/unbind)

#### 3.5.1 主注销方式：MAC 解绑

**接口路径**：

```text
GET /eportal/portal/mac/unbind
```

**请求参数**：

| 参数名 | 必填 | 示例值 | 说明 |
|--------|------|--------|----|
| `user_account` | 是 | `202320701256` | 当前登录账号（不含运营商前缀） |
| `wlan_user_mac` | 是 | `AABBCCDDEEFF` | 客户端 MAC 地址（大写，无分隔符） |
| `wlan_user_ip` | 是 | `167837187` | 客户端 IP 转换为整数 |

**IP 地址转整数算法**：

```text
a.b.c.d → a×256³ + b×256² + c×256 + d
```

**示例**：

```text
10.1.2.3 → 10×16777216 + 1×65536 + 2×256 + 3 = 167837187
```

**成功响应示例**：

```javascript
dr1715328000({"result":"1","msg":"注销成功","ret_code":"0"})
```

#### 3.5.2 备用注销方式：完整参数 logout

如果 `/mac/unbind` 失败，工具会 fallback 到 `/logout` 接口。

**接口路径**：

```text
GET /eportal/portal/logout
```

**请求参数**：

| 参数名 | 必填 | 示例值 | 说明 |
|--------|--------|------|
| `login_method` | 是 | `1` | 登录方式 |
| `user_account` | 是 | `drcom` | 固定值 drcom |
| `user_password` | 是 | `123` | 固定值 123 |
| `ac_logout` | 是 | `0` | AC 注销标志 |
| `register_mode` | 是 | `1` | 注册模式 |
| `wlan_user_ip` | 是 | `10.1.2.3` | 客户端 IP（点分十进制） |
| `wlan_user_ipv6` | 否 | `` | IPv6 地址 |
| `wlan_vlan_id` | 是 | `1` | VLAN ID |
| `wlan_user_mac` | 是 | `AABBCCDDEEFF` | 客户端 MAC（大写，无分隔符） |
| `wlan_ac_ip` | 否 | `` | AC IP |
| `wlan_ac_name` | 否 | `` | AC 名称 |

#### 3.5.3 注销流程

工具的完整注销流程：

1. **读取门户配置**：`GET /eportal/portal/page/loadConfig`
2. **检查配置**：如果 `un_bind_mac=1`，优先使用 `/mac/unbind`
3. **执行主注销**：调用 `/mac/unbind` 接口
4. **失败回退**：如果主注销失败，调用 `/logout` 接口
5. **状态确认**：轮询 `/online_list` 确认是否已离线

**为什么不先调用空参数 logout**：

早期脚本曾先请求只带 JSONP 基础参数的 `/logout`，但实测中它可能返回"Portal协议注销失败！"，且在远程 SSH 场景下容易出现短时不可达。当前实现按浏览器真实顺序处理，更加稳定可靠。

### 3.6 门户配置接口 (/page/loadConfig)

**接口路径**：

```text
GET /eportal/portal/page/loadConfig
```

**请求参数**：

| 参数名 | 必填 | 示例值 | 说明 |
|--------|------|--------|------|
| `program_index` | 否 | `` | 程序索引 |
| `wlan_vlan_id` | 是 | `1` | VLAN ID |
| `wlan_user_ip` | 是 | `MTAuMS4yLjM=` | 客户端 IP 的 Base64 编码 |
| `wlan_user_ipv6` | 否 | `` | IPv6 地址 |
| `wlan_user_ssid` | 否 | `` | WiFi SSID |
| `wlan_user_areaid` | 否 | `` | 区域 ID |
| `wlan_ac_ip` | 否 | `` | AC IP |
| `wlan_ap_mac` | 是 | `000000000000` | AP MAC 地址 |
| `gw_id` | 是 | `000000000000` | 网关 ID |

**响应示例**：

```javascript
dr1715328000({
  "result":"1",
  "un_bind_mac":"1",
  "register_mode":"1",
  ...
})
```

**关键配置字段**：

| 字段名 | 说明 |
|--------|------|
| `un_bind_mac` | 是否支持 MAC 解绑，1 表示支持 |
| `register_mode` | 注册模式 |

工具根据 `un_bind_mac` 字段决定使用哪种注销方式。

### 3.7 MAC 查询接口 (/mac/find)

**接口路径**：

```text
GET /eportal/portal/mac/find?user_account=<账号>
```

**用途**：

查询指定账号的在线设备，用于设备列表合并。

**响应格式**：

与 `/online_list` 类似，返回设备列表。

## 4. 脚本架构

本节介绍三种实现的技术架构、设计模式和实现差异。

### 4.1 Python 版本架构

**文件**：`zjnu_auth_lit.py`

**核心依赖**：

- `requests`：HTTP 客户端库
- `getpass`：安全密码输入
- `socket`：网络接口查询
- `json`：JSON 解析（JSONP 需手动提取）

**架构特点**：

1. **面向对象设计**：
   - 主类封装所有认证逻辑
   - 方法分离：登录、注销、查询、网络检测等

2. **JSONP 解析**：
   ```python
   # 提取 JSONP 回调函数中的 JSON
   match = re.search(r'^\w+\((.*)\)$', response_text)
   data = json.loads(match.group(1))
   ```

3. **网络接口获取**：
   ```python
   # 使用 UDP socket 获取本机 IP
   s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
   s.connect(('10.1.116.8', 801))
   local_ip = s.getsockname()[0]
   ```

4. **代理控制**：
   ```python
   # 默认禁用系统代理
   session.trust_env = False
   # 或使用 --use-proxy 启用
   session.trust_env = True
   ```

5. **交互式界面**：
   - ANSI 颜色码美化输出
   - 表格对齐显示设备列表
   - 菜单状态动态更新

**推荐理由**：

- 代码可读性最好
- JSONP 解析最完整
- 错误处理最健壮
- 交互体验最佳

### 4.2 Bash 版本架构

**文件**：`zjnu_auth_lit.sh`

**核心依赖**：

- `curl`：HTTP 客户端
- `sed`/`grep`/`awk`：文本处理
- `ip` 或 `hostname`：网络接口查询
- `ping`：连通性测试
- `base64`：Base64 编码（可选）

**架构特点**：

1. **函数式设计**：
   ```bash
   zjnu_login() { ... }
   zjnu_logout() { ... }
   zjnu_status() { ... }
   ```

2. **轻量 JSONP 解析**：
   ```bash
   # 提取 result 字段
   result=$(echo "$response" | sed 's/.*"result":"\([^"]*\)".*/\1/')
   # 提取 msg 字段
   msg=$(echo "$response" | sed 's/.*"msg":"\([^"]*\)".*/\1/')
   ```

3. **网络接口获取**：
   ```bash
   # 使用 ip route 获取本机 IP
   local_ip=$(ip route get 10.1.116.8 | grep -oP 'src \K\S+')
   # 从 /sys/class/net 读取 MAC
   mac=$(cat /sys/class/net/$iface/address)
   ```

4. **兼容性处理**：
   - 避免 Bash 4 专用语法（如 `declare -A`、`${var,,}`）
   - 兼容 GNU sed 新旧参数形式（`-r` vs `-E`）
   - 跳过虚拟网卡（`lo`、`docker0`、`virbr*`、`veth*`）

5. **环境变量配置**：
   ```bash
   ZJNU_AUTH_API_BASE=http://10.1.116.8:801/eportal/portal ./zjnu_auth_lit.sh
   ```

**适用场景**：

- 没有 Python 的 Linux 环境
- 需要快速启动的场景
- 嵌入式系统或路由器

**局限性**：

- JSONP 解析能力有限，不适合处理复杂或非预期 JSON
- 错误处理不如 Python 版完善
- 交互式界面依赖 ANSI 转义码，部分终端可能显示异常

### 4.3 C++ 版本架构

**文件**：`zjnu_auth_lit.cpp`

**核心依赖**：

- **普通构建**：`libcurl`（HTTP 客户端）
- **静态构建**：无外部依赖（使用内置 HTTP 客户端）

**架构特点**：

1. **条件编译**：
   ```cpp
   #ifdef ZJNU_AUTH_NO_CURL
   // 使用内置 HTTP socket 客户端
   #else
   // 使用 libcurl
   #endif
   ```

2. **内置 HTTP 客户端**：
   ```cpp
   // 静态构建时使用原始 socket
   int sock = socket(AF_INET, SOCK_STREAM, 0);
   connect(sock, ...);
   send(sock, "GET /path HTTP/1.1\r\n...", ...);
   ```

3. **轻量 JSON 解析**：
   ```cpp
   // 不依赖第三方 JSON 库
   std::string extract_field(const std::string& json, const std::string& key) {
       size_t pos = json.find("\"" + key + "\"");
       // 手动提取字段值
   }
   ```

4. **网络接口获取**：
   ```cpp
   // 使用 UDP socket 获取本机 IP
   int sock = socket(AF_INET, SOCK_DGRAM, 0);
   connect(sock, ...);
   getsockname(sock, ...);
   ```

5. **交互式界面**：
   - 复刻 Python 版的颜色和对齐
   - 使用 ANSI 转义码
   - 密码输入使用 `termios` 隐藏回显

**编译选项**：

**普通构建**（动态链接）：

```bash
make
# 生成 zjnu_auth_lit（依赖 libcurl.so）
```

**静态构建**（无运行时依赖）：

```bash
make static
# 生成 zjnu_auth_lit_static（完全静态，无外部依赖）
```

**验证静态链接**：

```bash
make verify-static
# 输出：zjnu_auth_lit_static: statically linked
```

**适用场景**：

- 需要编译为单个二进制分发
- 嵌入式系统或无 Python/Bash 环境
- 需要完全静态链接的场景

**局限性**：

- 静态版只支持 HTTP，不支持 HTTPS Portal API
- JSON 解析能力有限，不适合处理复杂响应
- 代码维护成本高于 Python 版

### 4.4 共同设计模式

三种实现遵循相同的业务逻辑和设计模式：

#### 4.4.1 状态检测模式

**双重确认机制**：

```text
1. 第一次检测 → 等待 0.5 秒 → 第二次检测
2. 如果两次结果一致，确认状态
3. 如果两次结果不一致，等待 1 秒后再检测两次
4. 第二轮仍不一致，返回"状态不稳定"错误
```

**目的**：避免网络抖动导致的误判。

#### 4.4.2 注销流程模式

```text
1. 读取门户配置 (/page/loadConfig)
2. 检查 un_bind_mac 配置
3. 优先使用 /mac/unbind（如果支持）
4. 失败时 fallback 到 /logout
5. 轮询 /online_list 确认离线
```

**目的**：提高注销成功率，避免远程 SSH 中断。

#### 4.4.3 设备列表合并模式

```text
1. 查询 /mac/find?user_account=<账号>
2. 查询 /online_list?user_account=<账号>
3. 查询 /online_list（当前设备）
4. 按 IP 或 MAC 去重合并
```

**目的**：确保设备列表完整，避免账号级接口漏掉当前设备。

#### 4.4.4 外网测试模式

```text
登录成功后依次 ping：
1. 114.114.114.114（国内 DNS）
2. 223.5.5.5（阿里 DNS）
3. www.baidu.com（域名解析测试）

任一目标可达即视为通过
```

**目的**：验证认证后的外网连通性。

#### 4.4.5 代理控制模式

```text
- 默认：禁用系统代理（HTTP_PROXY、HTTPS_PROXY）
- 交互式模式：始终禁用代理
- 命令行模式：可通过 --use-proxy 启用代理
```

**目的**：避免代理干扰校园网认证（认证网关需要验证真实客户端 IP）。

## 5. 运行环境

### 5.1 Python 版

**依赖**：

```text
Python 3.6+
requests
```

**安装依赖**：

```bash
pip install -r requirements.txt
```

**运行**：

```bash
python3 zjnu_auth_lit.py
```

**系统要求**：

- 支持 Linux、macOS、Windows（WSL）
- 需要能访问校园网认证网关 `10.1.116.8`

### 5.2 Bash 版

依赖：

```text
bash
curl
sed
grep
awk
ip 或 hostname
ping
base64（注销配置请求中使用；缺少时仍会尽量继续）
```

运行：

```bash
chmod +x zjnu_auth_lit.sh
./zjnu_auth_lit.sh
```

Bash 版可通过环境变量临时改 Portal API：

```bash
ZJNU_AUTH_API_BASE=http://10.1.116.8:801/eportal/portal ./zjnu_auth_lit.sh
```

### 3.3 C++ 版

依赖：

```text
g++
make
```

普通动态构建额外需要 `libcurl`；完全静态构建不需要 `libcurl`，会使用内置 HTTP 客户端。

Debian/Ubuntu：

```bash
sudo apt install g++ libcurl4-openssl-dev make pkg-config
```

编译：

```bash
make
```

运行：

```bash
./zjnu_auth_lit
```

完全静态编译：

```bash
make static
make verify-static
./zjnu_auth_lit_static
```

`make static` 使用 `-static -DZJNU_AUTH_NO_CURL`，不会链接 `libcurl`，因此不需要 `libcurl.a`、`libgssapi_krb5.a` 等依赖链静态库。静态版只支持默认的 HTTP Portal API，不支持 HTTPS API。

## 6. 使用方法

本节介绍工具的两种使用模式：交互式模式和命令行参数模式。

### 6.1 交互式模式

不带参数直接运行会进入交互菜单：

**Python 版**：

```bash
python3 zjnu_auth_lit.py
```

**Bash 版**：

```bash
./zjnu_auth_lit.sh
```

**C++ 版**：

```bash
./zjnu_auth_lit
```

**菜单界面**：

```text
==================================
    浙师大校园网认证工具 v1.0
=========

当前状态: 未登录

[1] 账号密码登录
[2] 手机验证码登录
[3] 注销当前设备
[0] 退出工具

请选择操作:
```

**状态感知**：

- 如果当前设备已经登录，工具会显示"当前状态: 已登录 (账号: 202320701256)"
- 已登录时，选项 [1] 和 [2] 会被禁用，提示"当前已登录，请先注销"
- 未登录时，选项 [3] 会被禁用，提示"当前未登录，无需注销"

**账号密码登录流程**：

1. 选择运营商（1-校园用户，2-校园电信，3-校园联通，4-校园移动）
2. 输入账号
3. 输入密码（不回显）
4. 工具自动获取本机 IP 和 MAC
5. 发送登录请求
6. 显示登录结果和在线设备列表
7. 执行外网连通性测试

**手机验证码登录流程**：

1. 选择运营商
2. 输入手机号
3. 发送验证码
4. 输入收到的验证码
5. 发送登录请求
6. 显示登录结果

**注销流程**：

1. 确认当前登录状态
2. 读取门户配置
3. 执行注销请求
4. 轮询确认是否离线
5. 显示注销结果

**交互式模式特点**：

- 彩色输出，界面友好
- 自动状态检测，防止重复登录
- 详细的操作提示和错误信息
- 始终禁用系统代理

### 6.2 命令行参数模式

命令行模式适合脚本自动化和远程调用。

#### 6.2.1 账号密码登录

**基本语法**：

```bash
<工具> login <账号> <密码>
```

