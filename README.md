# zjnu-auth-net

浙江师范大学校园网 Portal 认证工具，支持 Python、Bash、C++ 三种实现。

提供账号密码登录、短信验证码登录、查询在线状态、注销设备等功能，登录后自动检测连通性。
注：本项目使用 gpt-5.5和opus-4.7 辅助完成。

<img src="demo.gif" width="600" alt="Demo">

演示视频使用[vhs](https://github.com/charmbracelet/vhs)项目构建。

## 🚀 快速开始

### Python 版本（推荐）

```bash
# 安装依赖
pip install -r requirements.txt

# 运行交互式界面
python3 zjnu_auth_lit.py
# 命令行登录
python3 zjnu_auth_lit.py login <账号> <密码>
```

## 📖 使用说明

### 交互式模式

直接运行程序进入交互式菜单：

```bash
python3 zjnu_auth_lit.py
./zjnu_auth_lit.sh
./zjnu_auth_lit
```

交互式界面提供：
- 实时显示认证状态和设备信息
- 彩色终端界面，清晰易读
- 密码输入不回显，保护隐私
- 自动检测当前设备是否已登录

### 命令行模式

适合脚本化调用和自动化场景：

```bash
# 账号密码登录
python3 zjnu_auth_lit.py login <账号> <密码>

# 指定运营商（1=校园用户 2=电信 3=联通 4=移动）
python3 zjnu_auth_lit.py login -u <账号> -p <密码> -o 1

# 注销当前设备
python3 zjnu_auth_lit.py logout

# 查询当前登录账号
python3 zjnu_auth_lit.py status

# 调试模式
python3 zjnu_auth_lit.py --debug status
```

**退出码说明**：
- `0`：成功
- `1`：失败（参数错误、认证失败、未登录等）
- `2`：注销请求提交失败
- `3`：登录状态不稳定
- `4`：注销后仍显示在线

### 运营商选择

| 编号 | 运营商 | 账号后缀 |
|------|--------|--------|
| 1 | 校园用户 | 无 |
| 2 | 校园电信 | @dx |
| 3 | 校园联通 | @lt |
| 4 | 校园移动 | 无 |

## ✨ 特性

- 🔐 **多种登录方式**：支持账号密码登录和手机验证码登录
- 🖥️ **交互式界面**：彩色终端界面，实时显示认证状态和在线设备
- ⚡ **命令行模式**：支持脚本化调用，适合自动化场景
- 🔒 **安全设计**：不保存任何敏感信息（账号、密码、验证码）
- 📱 **设备管理**：查看同账号在线设备，支持注销指定设备
- 🚫 **代理控制**：默认禁用系统代理，确保认证可靠性
- 📦 **多种实现**：Python、Bash、C++ 三种版本，适应不同环境

## 📋 版本对比

| 版本 | 优势 | 适用场景 |
|------|------|----------|
| **Python** | 功能完整，交互体验最佳 | 推荐优先使用 |
| **Bash** | 依赖少，兼容性好 | 只有 bash 和 curl 的环境 |
| **C++** | 可编译为单个二进制文件 | 需要分发部署的场景 |

## 🔧 高级功能

### 静态编译（C++ 版本）

编译为完全静态的二进制文件，无需任何运行时依赖：

```bash
make static
make verify-static
./zjnu_auth_lit_static
```

**注意**：静态版本使用内置 HTTP 客户端，仅支持 HTTP Portal API（不支持 HTTPS）。

### 代理设置

默认情况下，工具会**禁用系统代理**以确保认证可靠性。如需使用代理：

```bash
python3 zjnu_auth_lit.py --use-proxy login <账号> <密码>
./zjnu_auth_lit.sh --use-proxy login <账号> <密码>
./zjnu_auth_lit --use-proxy login <账号> <密码>
```

⚠️ **警告**：使用代理可能导致认证失败，因为认证网关需要验证真实客户端 IP。

### 环境变量

Bash 版本支持通过环境变量自定义配置：

```bash
# 自定义 Portal API 地址
ZJNU_AUTH_API_BASE=http://10.1.116.8:801/eportal/portal ./zjnu_auth_lit.sh

# 自定义请求超时时间（秒）
ZJNU_AUTH_TIMEOUT=15 ./zjnu_auth_lit.sh
```

## 📁 项目结构
```
zjnu_auth_net/
├── zjnu_auth_lit.py      # Python 版本（推荐）
├── zjnu_auth_lit.sh      # Bash 版本
├── zjnu_auth_lit.cpp     # C++ 版本
├── requirements.txt      # Python 依赖
├── Makefile             # C++ 构建脚本
├── README.md            # 项目说明
├── DETAIL.md            # 详细文档（协议分析、抓包方法等）
└── demo.gif          # 演示动画
```

## ⚠️ 注意事项

1. **网络环境**：必须在能访问校园网认证网关的环境中运行（校园网内或 VPN）
2. **认证网关**：默认 Portal API 为 `http://10.1.116.8:801/eportal/portal`
3. **代理设置**：默认禁用系统代理，避免干扰认证（交互式模式始终禁用）
4. **设备限制**：同一账号通常最多同时登录 3 台设备
5. **SSH 注意**：通过 SSH 操作远程机器时，注销当前设备可能导致连接短暂中断
6. **隐私保护**：不要将真实账号、密码、验证码提交到 GitHub、Issue 或日志中

## 🔍 故障排查

### 认证网关不可达

```bash
# 测试网关连通性
ping 10.1.116.8
curl -I http://10.1.116.8:801/eportal/portal
```

### Python 依赖问题

```bash
pip install -r requirements.txt
```

### Bash 依赖检查

```bash
command -v curl bash ping
```

### C++ 编译失败

```bash
# 安装依赖
sudo apt install g++ libcurl4-openssl-dev make pkg-config

# 清理重新编译
make clean
make
```

### 账号达到设备上限
1. 运行 `status` 查看当前状态
2. 进入交互模式查看在线设备列表
3. 注销不用的设备后重新登录
## 📚 详细文档

更多技术细节请参考 [DETAIL.md](DETAIL.md)，包括：

- Portal 协议详细说明
- 抓包分析方法
- 接口参数说明
- 注销流程分析
- 三种实现的差异
- 退出码详细说明
- 排障建议

## 📄 License

MIT License

---

**免责声明**：本工具仅供学习交流使用，请遵守学校网络使用规定。
