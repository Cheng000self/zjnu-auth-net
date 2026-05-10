# zjnu_auth_net 详细说明

本文档说明 `zjnu_auth_net` 的运行方式、Portal 请求方法、接口参数、三种实现的差异、退出码和校园网使用注意事项。

## 1. 项目目标

本项目面向浙江师范大学校园网认证场景，提供三个入口：

```text
zjnu_auth_lit.py   Python 版
zjnu_auth_lit.sh   Bash 版
zjnu_auth_lit.cpp  C++ 版
```

三种实现的核心目标一致：

- 账号密码登录。
- 手机验证码登录。
- 查询当前设备是否在线。
- 注销当前设备。
- 登录后测试外网连通性。
- 登录后输出当前账号在线设备。
- 不保存账号、密码、手机号、验证码。

Python 版交互体验最好；Bash 版适合没有 Python 但有 `curl` 的 Linux 环境；C++ 版适合编译为单个二进制后部署。

## 2. 认证网关

默认 Portal API：

```text
http://10.1.116.8:801/eportal/portal
```

网关主机：

```text
10.1.116.8
```

相关门户页面：

```text
https://portal.zjnu.edu.cn
https://10.1.116.8
```

实际请求使用 HTTP 801 端口。工具必须运行在能访问该认证网关的校园网环境中，公网环境或普通家庭网络通常无法访问。

## 3. 运行环境

### 3.1 Python 版

依赖：

```text
Python 3
requests
```

安装：

```bash
pip install -r requirements.txt
```

运行：

```bash
python3 zjnu_auth_lit.py
```

### 3.2 Bash 版

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

也可以用 Alpine/musl 容器构建：

```bash
make static-alpine
./dist/zjnu_auth_lit_static
```

`make static-alpine` 会调用 `scripts/build_static_alpine.sh`，在容器中安装基础编译工具并输出 musl 完全静态二进制。

## 4. 交互模式

不带参数直接运行会进入交互菜单：

```text
[1] 账号密码登录
[2] 手机验证码登录
[3] 注销当前设备
[0] 退出工具
```

如果当前设备已经登录，工具会阻止重复执行登录。若当前设备未登录，工具会阻止注销当前设备。

## 5. 参数模式

账号密码登录：

```bash
python3 zjnu_auth_lit.py login <账号> <密码>
./zjnu_auth_lit.sh login <账号> <密码>
./zjnu_auth_lit login <账号> <密码>
```

显式参数：

```bash
python3 zjnu_auth_lit.py login -u <账号> -p <密码> -o 1
./zjnu_auth_lit.sh login -u <账号> -p <密码> -o 1
./zjnu_auth_lit login -u <账号> -p <密码> -o 1
```

注销当前设备：

```bash
python3 zjnu_auth_lit.py logout
./zjnu_auth_lit.sh logout
./zjnu_auth_lit logout
```

输出当前登录账号：

```bash
python3 zjnu_auth_lit.py status
./zjnu_auth_lit.sh status
./zjnu_auth_lit status
```

Debug：

```bash
python3 zjnu_auth_lit.py --debug status
./zjnu_auth_lit.sh --debug status
./zjnu_auth_lit --debug status
```

参数模式下 `login` 和 `logout` 默认不输出业务日志，只通过退出码表示结果；`status` 只在已登录时输出账号。

## 6. 运营商与账号格式

运营商编号：

```text
1 校园用户
2 校园电信
3 校园联通
4 校园移动
```

Portal 的 `user_account` 字段格式：

```text
,<运营商编号>,<账号>
```

示例：

```text
,1,202320701256
```

后缀规则：

- 选择校园电信时，账号自动追加 `@dx`，除非账号本身已经带有 `@dx`。
- 选择校园联通时，账号自动追加 `@lt`，除非账号本身已经带有 `@lt`。
- 校园用户和校园移动不追加后缀。

## 7. 公共请求参数

Portal 返回 JSONP，因此每个请求都会带公共参数：

```text
callback=dr<时间随机数>
jsVersion=4.2.1
v=<时间随机数>
lang=zh-cn
```

成功判断：

```text
result 为 1、ok 或 true
```

消息字段会按以下顺序读取：

```text
msg
message
ret_msg
error
error_msg
content
```

如果响应中有错误码，会附加显示：

```text
ret_code
code
err_code
error_code
```

## 8. 账号密码登录请求

接口：

```text
GET /eportal/portal/login
```

核心参数：

```text
login_method=1
user_account=,<运营商编号>,<账号>
user_password=<密码>
wlan_user_ip=<本机认证IP>
wlan_user_ipv6=
wlan_user_mac=<本机MAC>
wlan_ac_ip=
wlan_ac_name=
terminal_type=1
```

如果 Portal 返回 `ret_code=2`，工具把它视为“当前 IP 已在线”，也算登录成功。

## 9. 手机验证码登录请求

发送验证码接口：

```text
GET /eportal/portal/sms
```

核心参数：

```text
telephone=<手机号或门户允许的号码>
mac=<本机MAC>
ip=<本机认证IP>
ipv6=
bind=0
page_index=
prefix=
sms_type=0
```

验证码登录仍使用 `/login`：

```text
user_account=,<运营商编号>,<手机号>
user_password=<短信验证码>
```

注意：

- 工具不做本地手机号格式校验。
- 工具不做本地 60 秒重复发送限制。
- 是否允许某个号码、是否重复发送过快、错误码是否明确，均由学校 Portal 后端决定。

## 10. 在线状态查询

接口：

```text
GET /eportal/portal/online_list
```

查询当前设备：

```text
online_list
```

查询某账号在线设备：

```text
online_list?user_account=<账号>
```

工具会把返回的 `list` 作为在线设备列表。常见字段：

```text
user_account
online_ip
online_mac
online_time
time_long
is_owner_ip
is_perceive
```

当前设备判断：

- `online_ip` 等于本机认证 IP。
- 或 `is_owner_ip=1`。
- 如果都没有命中，则取列表第一项作为当前设备。

## 11. 设备列表合并

登录成功后，工具会合并三个来源：

```text
/mac/find?user_account=<账号>
/online_list?user_account=<账号>
当前设备 online_list 快照
```

合并时按 IP 或 MAC 去重。

这样做的原因是账号级接口有时只返回部分设备。如果当前机器是第三台登录设备，账号级列表可能漏掉当前设备，因此工具会把当前会话补进去。

## 12. 校园网设备数限制

真实校园网使用中，一个账号通常最多同时登录 3 台设备。达到上限时，新设备可能无法登录，或者 Portal 返回泛化错误。

建议：

- 登录失败时先检查是否已有 3 台设备在线。
- 不用的电脑、手机、路由器或虚拟机请先注销。
- 如果账号级设备列表看起来不完整，以 Portal 页面和当前设备状态为准。
- 手机验证码账号也可能存在多台在线设备，不应假设验证码登录只允许一台。

## 13. 注销当前设备

浏览器页面中的注销按钮不是简单调用空参数 `/logout`。

工具先读取门户配置：

```text
GET /eportal/portal/page/loadConfig
```

核心参数：

```text
program_index=
wlan_vlan_id=1
wlan_user_ip=<本机IP的Base64>
wlan_user_ipv6=
wlan_user_ssid=
wlan_user_areaid=
wlan_ac_ip=
wlan_ap_mac=000000000000
gw_id=000000000000
```

当前已观察到的门户配置通常为：

```text
un_bind_mac=1
register_mode=1
```

在这种配置下，浏览器主注销方式是：

```text
GET /eportal/portal/mac/unbind
```

核心参数：

```text
user_account=<当前账号>
wlan_user_mac=<当前MAC大写>
wlan_user_ip=<当前IP转整数>
```

其中 IP 转整数规则：

```text
a.b.c.d -> a*256^3 + b*256^2 + c*256 + d
```

如果 `/mac/unbind` 失败，工具 fallback 到完整参数 `/logout`：

```text
GET /eportal/portal/logout
```

fallback 参数：

```text
login_method=1
user_account=drcom
user_password=123
ac_logout=0
register_mode=1
wlan_user_ip=<当前IP>
wlan_user_ipv6=
wlan_vlan_id=1
wlan_user_mac=<当前MAC大写>
wlan_ac_ip=
wlan_ac_name=
```

## 14. 为什么不先打空参数 logout

早期脚本曾先请求：

```text
/eportal/portal/logout
```

并且只带 JSONP 基础参数。真实测试中它可能返回：

```text
Portal协议注销失败！
```

随后再调用 `/mac/unbind`，远程 SSH 场景下更容易出现短时不可达或等待异常。

当前实现按浏览器真实顺序处理：

1. 读取门户配置。
2. 配置允许时优先 `/mac/unbind`。
3. 失败后再 fallback 到完整参数 `/logout`。
4. 注销后轮询当前在线状态确认是否离线。

## 15. 本机 IP 与 MAC 获取

本机认证 IP：

- Python/C++ 版优先用 UDP socket 连接 `10.1.116.8:801`，读取本地 socket IP。
- Bash 版优先用 `ip route get 10.1.116.8` 提取 `src`。
- 失败时 fallback 到系统网卡地址。

本机 MAC：

1. 从 `/proc/net/route` 找默认路由网卡。
2. 读取 `/sys/class/net/<iface>/address`。
3. 失败时扫描 `/sys/class/net/*/address`。
4. 跳过 `lo`、`docker0`、`virbr*`、`veth*`。

MAC 内部统一去除冒号和横杠，转为小写；展示或注销请求中会转为大写。

## 16. 登录后外网测试

登录成功后执行 ping 测试：

```text
114.114.114.114
223.5.5.5
www.baidu.com
```

任一目标可达即视为通过。

如果机器禁 ping、网络策略阻断 ICMP，可能出现“认证成功但外网测试未通过”的情况。此时可用浏览器或 `curl` 再确认 HTTP/HTTPS 是否可达。

## 17. CLI 登录状态确认

参数模式执行 `login/logout/status` 时会先确认当前登录状态：

1. 连续检测两次，中间间隔 0.5 秒。
2. 两次都在线：确认已登录。
3. 两次都离线：确认未登录。
4. 如果一次在线、一次离线，等待 1 秒后再检测两次。
5. 第二轮仍不一致，则认为状态不稳定。

状态不稳定时返回退出码 `3`。

## 18. 退出码

### 18.1 login

```text
0 登录成功，或当前已经登录
1 账号、密码、参数或认证失败
3 登录状态检测不稳定
```

### 18.2 logout

```text
0 注销成功，或当前本来未登录
2 注销请求提交失败
3 登录状态检测不稳定
4 注销请求提交后，多次检测仍然显示已登录
```

### 18.3 status

```text
0 当前已登录，已输出登录账号
1 当前未登录，或未获取到登录账号
3 登录状态检测不稳定
```

## 19. 三种实现的差异

Python 版：

- 使用 `requests`。
- JSONP 解析和终端展示最完整。
- 推荐作为默认版本。

Bash 版：

- 使用 `curl --get --data-urlencode` 发送请求。
- 通过 `sed/grep/awk` 做轻量 JSONP 解析。
- 交互式界面复刻 Python 版颜色、对齐、菜单状态和提示文案。
- 避免 `declare -A`、`${var,,}` 等 Bash 4 专用写法，并兼容 GNU sed 新旧参数形式。
- 足够覆盖常见 Portal 响应，但不适合处理复杂或非预期 JSON。

C++ 版：

- 普通构建使用 `libcurl` 发送请求。
- 静态构建使用内置 HTTP socket 客户端，不依赖 `libcurl`。
- 内置轻量 JSONP/JSON 字段解析，不依赖第三方 JSON 库。
- 交互式界面复刻 Python 版颜色、对齐、菜单状态和提示文案。
- 适合编译为二进制分发。

## 20. 安全说明

工具不会保存：

- 校园网账号。
- 校园网密码。
- 手机号。
- 短信验证码。
- SSH 密码。
- root 密码。

工具不会写登录日志。交互式密码输入在 Python/C++ 版中不回显；Bash 版使用 `stty -echo` 尽量隐藏输入。

请不要把真实账号、密码、验证码写进 README、Issue、截图或 GitHub Actions 日志。

## 21. SSH 与远程机器注意事项

如果你通过 SSH 连到一台校园网内机器，然后在这台机器上注销当前认证会话，SSH 连接可能短暂中断。

建议：

- 远程操作前确认是否有其他访问方式。
- 不要在关键任务运行中注销当前设备。
- 如果只是想让另一台设备上线，优先在 Portal 页面或本工具中确认当前在线设备。

## 22. 排障建议

认证网关不可达：

```bash
ping 10.1.116.8
curl -I http://10.1.116.8:801/eportal/portal
```

Python 依赖缺失：

```bash
pip install -r requirements.txt
```

Bash 依赖缺失：

```bash
command -v curl
command -v bash
command -v ping
```

C++ 编译失败：

```bash
sudo apt install g++ libcurl4-openssl-dev make pkg-config
make clean
make
```

C++ 静态编译失败：

```bash
make clean
make static
make verify-static
```

也可使用 Alpine/musl 容器构建：

```bash
make static-alpine
```

账号达到设备上限：

- 先运行 `status` 确认当前设备状态。
- 尝试进入交互模式查看在线设备。
- 注销不用的设备后重新登录。

## 23. 基础检查

```bash
python3 -m py_compile zjnu_auth_lit.py
bash -n zjnu_auth_lit.sh
make
```

不在校园网环境时，不建议直接测试真实登录请求；可以先运行帮助命令和语法检查。
