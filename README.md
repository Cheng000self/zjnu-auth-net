# zjnu_auth_net

浙江师范大学校园网 Portal 认证工具，提供 Python、Bash 和 C++ 三种实现。三份程序使用同一套请求流程，覆盖账号密码登录、手机验证码登录、当前状态查询、当前设备注销、登录后外网连通测试和在线设备展示。

本项目以 MIT License 完全开源。代码不会保存账号、密码、手机号、验证码，也不会写入登录日志。

## 文件说明

| 文件 | 说明 |
| --- | --- |
| `zjnu_auth_lit.py` | Python 版，功能最完整，终端界面更友好。 |
| `zjnu_auth_lit.sh` | Bash 版，适合没有 Python 环境但有 `bash` 和 `curl` 的 Linux 机器；避免 Bash 4 专用语法，兼容较旧 Linux。 |
| `zjnu_auth_lit.cpp` | C++ 版，普通构建使用 `libcurl`；静态构建使用内置 HTTP 客户端，不依赖 `libcurl`。 |
| `DETAIL.md` | 请求方法、接口参数、退出码、校园网注意事项的详细说明。 |
| `Makefile` | C++ 构建和基础检查入口。 |
| `scripts/build_static_alpine.sh` | 使用 Alpine/musl 容器构建完全静态 C++ 二进制。 |
| `requirements.txt` | Python 版依赖。 |

## 功能

- 账号密码登录。
- 手机验证码登录。
- 查询当前设备是否已认证。
- 注销当前设备，优先使用浏览器一致的 `/mac/unbind` 流程。
- 登录成功后测试外网连通性。
- 登录成功后展示同账号在线设备列表。
- 支持交互模式和 `login/logout/status` 参数模式。

## 快速使用

Python 版：

```bash
pip install -r requirements.txt
python3 zjnu_auth_lit.py
```

Bash 版：

```bash
chmod +x zjnu_auth_lit.sh
./zjnu_auth_lit.sh
```

C++ 版：

```bash
make
./zjnu_auth_lit
```

完全静态二进制：

```bash
make static
make verify-static
./zjnu_auth_lit_static
```

`make static` 不链接 `libcurl`，因此不需要安装 `libcurl` 依赖链的静态库。它只支持默认的 HTTP Portal API，不支持 HTTPS API。也可以用 Alpine/musl 容器构建：

```bash
make static-alpine
./dist/zjnu_auth_lit_static
```

如果系统缺少 C++ 依赖，可在 Debian/Ubuntu 上安装：

```bash
sudo apt install g++ libcurl4-openssl-dev make pkg-config
```

## 参数模式

账号密码登录：

```bash
python3 zjnu_auth_lit.py login <账号> <密码>
./zjnu_auth_lit.sh login <账号> <密码>
./zjnu_auth_lit login <账号> <密码>
```

也可以使用显式参数：

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

运营商编号：

```text
1 校园用户
2 校园电信
3 校园联通
4 校园移动
```

## 注意事项

- 工具需要在能访问浙江师范大学校园网认证网关的网络环境中运行。
- 默认 Portal API 为 `http://10.1.116.8:801/eportal/portal`。
- 同一账号通常最多同时登录 3 台设备，达到上限时请先注销不用的设备。
- 注销当前认证会话可能导致 SSH 或远程连接短暂中断。
- 手机验证码能否发送、验证码错误原因、重复发送限制等由学校 Portal 后端决定。

更完整的协议细节、请求参数和排障说明见 [DETAIL.md](DETAIL.md)。
