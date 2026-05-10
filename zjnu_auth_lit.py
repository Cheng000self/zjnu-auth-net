#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
浙江师范大学校园网认证工具

功能范围：
1. 账号密码登录
2. 手机验证码登录
3. 安全注销当前设备
0. 退出工具

"""

from __future__ import annotations

import argparse
import getpass
import base64
import glob
import ipaddress
import json
import os
import re
import socket
import subprocess
import sys
import threading
import time
import unicodedata
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any, Dict, Iterable, List, Optional, Tuple
from urllib.parse import urlparse

try:
    import requests
    from requests.packages.urllib3.exceptions import InsecureRequestWarning
except ImportError:
    print("错误: 缺少 requests 库，请运行: pip install requests")
    sys.exit(1)


requests.packages.urllib3.disable_warnings(InsecureRequestWarning)


# ==========================================
# 终端颜色
# ==========================================
if os.name == "nt":
    os.system("")


class C:
    RST = "\033[0m"
    DG = "\033[90m"
    W = "\033[37m"
    BW = "\033[97m"
    BC = "\033[96m"
    BB = "\033[94m"
    BM = "\033[95m"
    BG = "\033[92m"
    BY = "\033[93m"
    R = "\033[31m"


# ==========================================
# Portal 协议常量
# ==========================================
JS_VERSION = "4.2.1"
TERMINAL_TYPE = "1"
DEFAULT_API_BASE = "http://10.1.116.8:801/eportal/portal"
PING_HOSTS = ("114.114.114.114", "223.5.5.5", "www.baidu.com")
UI_WIDTH = 74
CLI_STATE_PAIR_INTERVAL = 0.5
CLI_STATE_RETRY_DELAY = 1.0
CLI_LOGOUT_CHECKS = 4
CLI_LOGOUT_INTERVAL = 1.0

OPERATORS = {
    "1": {"name": "校园用户", "suffix": ""},
    "2": {"name": "校园电信", "suffix": "@dx"},
    "3": {"name": "校园联通", "suffix": "@lt"},
    "4": {"name": "校园移动", "suffix": ""},
}
DEFAULT_OPERATOR = "1"
CLI_DEBUG = False


@dataclass
class RequestResult:
    ok: bool
    message: str
    data: Dict[str, Any]


@dataclass
class AuthSnapshot:
    online: bool
    message: str
    account: str = ""
    current_device: Dict[str, Any] = field(default_factory=dict)
    devices: List[Dict[str, Any]] = field(default_factory=list)


@dataclass
class DeviceQueryResult:
    ok: bool
    message: str
    devices: List[Dict[str, Any]]
    added_current: bool = False
    mac_count: int = 0
    account_online_count: int = 0


def callback_name() -> str:
    return "dr" + str(int(time.time() * 1000) % 100000)


def random_v() -> str:
    return str(int(time.time() * 1000) % 100000)


def parse_jsonp(text: str) -> Optional[Dict[str, Any]]:
    """解析 Dr.COM Portal 返回的 JSONP 数据。"""
    text = text.strip()
    if not text:
        return None
    try:
        if text.startswith("{"):
            return json.loads(text)
        match = re.search(r"^[\w$.]+\((.*)\);?$", text, flags=re.S)
        if match:
            return json.loads(match.group(1))
    except (TypeError, ValueError):
        return None
    return None


def response_message(data: Dict[str, Any], fallback: str = "") -> str:
    """从门户响应中尽量提取用户可读消息，并附带错误码。"""
    for key in ("msg", "message", "ret_msg", "error", "error_msg", "content"):
        value = data.get(key)
        if value not in (None, ""):
            message = str(value)
            break
    else:
        message = fallback

    code_parts = []
    for key in ("ret_code", "code", "err_code", "error_code"):
        value = data.get(key)
        if value not in (None, ""):
            code_parts.append(f"{key}={value}")
    if code_parts:
        codes = "，".join(code_parts)
        return f"{message}（{codes}）" if message else codes
    return message


def visible_width(value: Any) -> int:
    """计算中文终端中的可视宽度，用于对齐看板字段。"""
    width = 0
    for char in str(value):
        width += 2 if unicodedata.east_asian_width(char) in "WF" else 1
    return width


def center_text(value: str, width: int = UI_WIDTH) -> str:
    """按中英文混排宽度居中文本。"""
    padding = max(0, width - visible_width(value))
    left = padding // 2
    right = padding - left
    return " " * left + value + " " * right


def separator(char: str = "=") -> str:
    return char * UI_WIDTH


def format_duration(seconds: Any) -> str:
    try:
        value = int(seconds)
    except (TypeError, ValueError):
        return str(seconds or "0秒")

    days, remainder = divmod(value, 86400)
    hours, remainder = divmod(remainder, 3600)
    minutes, secs = divmod(remainder, 60)
    if days:
        return f"{days}天{hours}时{minutes:02d}分{secs:02d}秒"
    if hours:
        return f"{hours}时{minutes:02d}分{secs:02d}秒"
    if minutes:
        return f"{minutes}分{secs:02d}秒"
    return f"{secs}秒"


def format_bytes(value: Any) -> str:
    try:
        size = float(value)
    except (TypeError, ValueError):
        return str(value or "0 B")

    units = ("B", "KB", "MB", "GB", "TB")
    index = 0
    while size >= 1024 and index < len(units) - 1:
        size /= 1024
        index += 1
    if index == 0:
        return f"{int(size)} {units[index]}"
    return f"{size:.2f} {units[index]}"


def normalize_operator(operator: Optional[str]) -> str:
    return operator if operator in OPERATORS else DEFAULT_OPERATOR


def ip_to_uint(ip: str) -> int:
    return int(ipaddress.IPv4Address(ip))


def command_exists(candidates: Iterable[str]) -> Optional[str]:
    for candidate in candidates:
        if os.path.exists(candidate) and os.access(candidate, os.X_OK):
            return candidate
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        for candidate in candidates:
            path = os.path.join(directory, os.path.basename(candidate))
            if os.path.exists(path) and os.access(path, os.X_OK):
                return path
    return None


def get_default_interface() -> Optional[str]:
    try:
        with open("/proc/net/route", "r", encoding="utf-8") as fh:
            for line in fh.readlines()[1:]:
                parts = line.split()
                if len(parts) > 2 and parts[1] == "00000000":
                    return parts[0]
    except OSError:
        pass
    return None


def get_local_ip(server_host: str = "10.1.116.8") -> str:
    """优先使用到认证网关的 UDP 路由判断本机认证 IP。"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.connect((server_host, 801))
        ip = sock.getsockname()[0]
        sock.close()
        return ip
    except OSError:
        pass

    ip_cmd = command_exists(("/usr/sbin/ip", "/sbin/ip", "ip"))
    if ip_cmd:
        try:
            output = subprocess.check_output(
                [ip_cmd, "-4", "-o", "addr", "show", "scope", "global"],
                stderr=subprocess.DEVNULL,
                text=True,
                timeout=3,
            )
            for line in output.splitlines():
                match = re.search(r"\binet\s+(\d+\.\d+\.\d+\.\d+)/", line)
                if match and not match.group(1).startswith(("127.", "172.17.")):
                    return match.group(1)
        except (OSError, subprocess.SubprocessError):
            pass
    return "0.0.0.0"


def get_mac_address() -> str:
    """读取默认路由网卡的 MAC 地址，失败时再扫描其他物理网卡。"""

    def normalize(mac: str) -> str:
        return mac.strip().replace(":", "").replace("-", "").lower()

    iface = get_default_interface()
    if iface:
        path = f"/sys/class/net/{iface}/address"
        try:
            with open(path, "r", encoding="utf-8") as fh:
                mac = normalize(fh.read())
                if mac and mac != "000000000000":
                    return mac
        except OSError:
            pass

    for path in sorted(glob.glob("/sys/class/net/*/address")):
        iface_name = path.split("/")[-2]
        if iface_name in {"lo", "docker0"} or iface_name.startswith(("virbr", "veth")):
            continue
        try:
            with open(path, "r", encoding="utf-8") as fh:
                mac = normalize(fh.read())
                if mac and mac != "000000000000":
                    return mac
        except OSError:
            continue
    return "000000000000"


def test_external_network(timeout: int = 2) -> Tuple[bool, str]:
    """登录后用 ping 测试外网是否可达。"""
    ping = command_exists(("/bin/ping", "/usr/bin/ping", "ping"))
    if not ping:
        return False, "未找到 ping 命令"

    for host in PING_HOSTS:
        try:
            result = subprocess.run(
                [ping, "-c", "1", "-W", str(timeout), host],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=timeout + 1,
            )
            if result.returncode == 0:
                return True, host
        except (OSError, subprocess.SubprocessError):
            continue
    return False, "外网测试未通过"


def is_same_device(left: Dict[str, Any], right: Dict[str, Any]) -> bool:
    left_ip = str(left.get("online_ip") or "")
    right_ip = str(right.get("online_ip") or "")
    left_mac = str(left.get("online_mac") or "").lower()
    right_mac = str(right.get("online_mac") or "").lower()
    return bool((left_ip and left_ip == right_ip) or (left_mac and left_mac == right_mac))


def auth_method_label(device: Dict[str, Any]) -> str:
    account = str(device.get("user_account") or "")
    if account.isdigit() and len(account) == 11 and account.startswith("1"):
        method = "手机验证码登录"
    else:
        method = "账号密码登录"
    if str(device.get("is_perceive")) == "1":
        method += " / 感知上线"
    return method


class ZJNUSimpleAuth:
    def __init__(self, api_base: str = DEFAULT_API_BASE, timeout: int = 10) -> None:
        self.api_base = api_base.rstrip("/")
        self.timeout = timeout
        self.session = requests.Session()
        self.session.headers.update(
            {
                "User-Agent": (
                    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                    "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124 Safari/537.36"
                ),
                "Accept": "*/*",
                "Referer": "http://10.1.116.8/",
            }
        )
        host = urlparse(self.api_base).hostname or "10.1.116.8"
        self.local_ip = get_local_ip(host)
        self.local_mac = get_mac_address()

    @property
    def portal_host(self) -> str:
        return urlparse(self.api_base).hostname or "10.1.116.8"

    def _request(self, endpoint: str, params: Optional[Dict[str, Any]] = None) -> RequestResult:
        payload = dict(params or {})
        payload.setdefault("callback", callback_name())
        payload.setdefault("jsVersion", JS_VERSION)
        payload.setdefault("v", random_v())
        payload.setdefault("lang", "zh-cn")

        try:
            response = self.session.get(
                self.api_base + endpoint,
                params=payload,
                timeout=self.timeout,
                verify=False,
            )
            response.raise_for_status()
        except requests.RequestException as exc:
            return RequestResult(False, f"请求失败: {exc}", {})

        data = parse_jsonp(response.text)
        if data is None:
            return RequestResult(False, f"响应解析失败: {response.text[:120]}", {})
        ok = str(data.get("result")).lower() in {"1", "ok", "true"}
        return RequestResult(ok, response_message(data), data)

    def user_account(self, account: str, operator: str = DEFAULT_OPERATOR) -> str:
        op = normalize_operator(operator)
        suffix = OPERATORS[op]["suffix"]
        if suffix and not account.endswith(suffix):
            account = account + suffix
        return f",{op},{account}"

    def login_with_password(self, username: str, password: str, operator: str) -> Tuple[bool, str]:
        result = self._request(
            "/login",
            {
                "login_method": "1",
                "user_account": self.user_account(username, operator),
                "user_password": password,
                "wlan_user_ip": self.local_ip,
                "wlan_user_ipv6": "",
                "wlan_user_mac": self.local_mac,
                "wlan_ac_ip": "",
                "wlan_ac_name": "",
                "terminal_type": TERMINAL_TYPE,
            },
        )
        if result.ok:
            return True, result.message or "Portal协议认证成功"
        if str(result.data.get("ret_code")) == "2":
            return True, result.message or "当前 IP 已在线"
        return False, result.message or "登录失败"

    def send_sms(self, phone: str) -> Tuple[bool, str]:
        result = self._request(
            "/sms",
            {
                "telephone": phone,
                "mac": self.local_mac,
                "ip": self.local_ip,
                "ipv6": "",
                "bind": "0",
                "page_index": "",
                "prefix": "",
                "sms_type": "0",
            },
        )
        if result.ok:
            return True, result.message or "验证码发送成功"
        return False, response_message(result.data, result.message)

    def login_with_phone(self, phone: str, code: str, operator: str) -> Tuple[bool, str]:
        result = self._request(
            "/login",
            {
                "login_method": "1",
                "user_account": self.user_account(phone, operator),
                "user_password": code,
                "wlan_user_ip": self.local_ip,
                "wlan_user_ipv6": "",
                "wlan_user_mac": self.local_mac,
                "wlan_ac_ip": "",
                "wlan_ac_name": "",
                "terminal_type": TERMINAL_TYPE,
            },
        )
        if result.ok:
            return True, result.message or "Portal协议认证成功"
        if str(result.data.get("ret_code")) == "2":
            return True, result.message or "当前 IP 已在线"
        return False, result.message or "登录失败"

    def query_current_online(self) -> Tuple[bool, str, List[Dict[str, Any]]]:
        result = self._request("/online_list", {})
        devices = result.data.get("list") if isinstance(result.data.get("list"), list) else []
        if result.ok:
            return True, result.message or "获取当前在线信息成功", devices
        return False, result.message or "当前设备未登录", []

    def get_auth_snapshot(self) -> AuthSnapshot:
        ok, msg, devices = self.query_current_online()
        if not ok or not devices:
            if msg.startswith("请求失败"):
                msg = "当前设备未登录，或当前环境无法访问认证网关"
            return AuthSnapshot(False, msg or "当前设备未登录")

        current = next(
            (
                device
                for device in devices
                if device.get("online_ip") == self.local_ip or str(device.get("is_owner_ip")) == "1"
            ),
            devices[0],
        )
        account = str(current.get("user_account") or "")
        return AuthSnapshot(True, "当前设备已认证在线", account, current, devices)

    def query_account_devices_with_current(
        self,
        account: str,
        snapshot: Optional[AuthSnapshot],
    ) -> DeviceQueryResult:
        """合并账号级列表和当前会话，解决第三台当前设备被账号列表漏掉的问题。"""
        mac_result = self._request("/mac/find", {"user_account": account})
        online_result = self._request("/online_list", {"user_account": account})

        mac_devices = mac_result.data.get("list") if isinstance(mac_result.data.get("list"), list) else []
        online_devices = (
            online_result.data.get("list") if isinstance(online_result.data.get("list"), list) else []
        )

        devices: List[Dict[str, Any]] = []
        for device in list(mac_devices) + list(online_devices):
            device_account = str(device.get("user_account") or "")
            if account and device_account and device_account != account:
                continue
            if not any(is_same_device(device, item) for item in devices):
                devices.append(device)

        added_current = False
        if snapshot and snapshot.online and snapshot.current_device:
            current_account = str(snapshot.current_device.get("user_account") or snapshot.account or "")
            if not account or not current_account or account == current_account:
                if not any(is_same_device(snapshot.current_device, item) for item in devices):
                    current = dict(snapshot.current_device)
                    current.setdefault("user_account", current_account or account)
                    devices.append(current)
                    added_current = True

        ok = mac_result.ok or online_result.ok or bool(devices)
        message = (
            f"账号列表 {len(mac_devices)} 台，账号在线 {len(online_devices)} 台，"
            f"当前会话补充 {'1' if added_current else '0'} 台，合并 {len(devices)} 台"
        )
        return DeviceQueryResult(ok, message, devices, added_current, len(mac_devices), len(online_devices))

    def load_portal_config(self) -> Dict[str, Any]:
        """读取门户前端配置，用来判断浏览器注销按钮实际采用的注销方式。"""
        result = self._request(
            "/page/loadConfig",
            {
                "program_index": "",
                "wlan_vlan_id": "1",
                "wlan_user_ip": base64.b64encode(self.local_ip.encode("utf-8")).decode("ascii"),
                "wlan_user_ipv6": "",
                "wlan_user_ssid": "",
                "wlan_user_areaid": "",
                "wlan_ac_ip": "",
                "wlan_ap_mac": "000000000000",
                "gw_id": "000000000000",
            },
        )
        data = result.data.get("data") if isinstance(result.data.get("data"), dict) else {}
        return data

    def browser_style_portal_logout(self, snapshot: AuthSnapshot) -> Tuple[bool, str]:
        """浏览器 fallback 注销：携带前端 JS 中 logout_portal 使用的完整参数。"""
        device = snapshot.current_device
        result = self._request(
            "/logout",
            {
                "login_method": "1",
                "user_account": "drcom",
                "user_password": "123",
                "ac_logout": "0",
                "register_mode": "1",
                "wlan_user_ip": str(device.get("online_ip") or self.local_ip),
                "wlan_user_ipv6": "",
                "wlan_vlan_id": "1",
                "wlan_user_mac": str(device.get("online_mac") or self.local_mac).upper(),
                "wlan_ac_ip": "",
                "wlan_ac_name": "",
            },
        )
        return result.ok, result.message or ("Portal注销成功" if result.ok else "Portal注销失败")

    def browser_style_unbind_logout(self, snapshot: AuthSnapshot) -> Tuple[bool, str]:
        """浏览器主注销方式：直接解绑当前 MAC，会释放当前认证会话。"""
        device = snapshot.current_device
        ip = str(device.get("online_ip") or self.local_ip)
        mac = str(device.get("online_mac") or self.local_mac).upper()
        result = self._request(
            "/mac/unbind",
            {
                "user_account": snapshot.account,
                "wlan_user_mac": mac,
                "wlan_user_ip": str(ip_to_uint(ip)),
            },
        )
        return result.ok, result.message or ("解绑终端MAC成功！" if result.ok else "注销并解绑MAC失败")

    def submit_browser_style_logout(self, snapshot: AuthSnapshot) -> Tuple[bool, str]:
        """只提交浏览器同款注销请求，不做状态轮询。"""
        portal_config = self.load_portal_config()
        un_bind_mac = str(portal_config.get("un_bind_mac", "1"))
        register_mode = str(portal_config.get("register_mode", "1"))

        # ZJNU 当前门户前端 wc() 的主路径：
        # un_bind_mac=1 且 register_mode 属于 1/3/4 时，直接 mac/unbind。
        # 旧脚本先请求 /logout 再 unbind，和浏览器顺序不一致，容易让 SSH 连接进入异常等待。
        if un_bind_mac == "1" and register_mode in {"1", "3", "4"}:
            ok, msg = self.browser_style_unbind_logout(snapshot)
            if not ok:
                fallback_ok, fallback_msg = self.browser_style_portal_logout(snapshot)
                if not fallback_ok:
                    return False, f"{msg}；fallback: {fallback_msg}"
                msg = fallback_msg
        else:
            ok, msg = self.browser_style_portal_logout(snapshot)
            if not ok:
                return False, msg
        return True, msg

    def logout_current_safe(self) -> Tuple[bool, str]:
        """按浏览器按钮的真实顺序注销当前设备，避免先打错误 logout 造成异常状态。"""
        snapshot = self.get_auth_snapshot()
        if not snapshot.online:
            return False, "当前设备未登录，无法注销"

        ok, msg = self.submit_browser_style_logout(snapshot)
        if not ok:
            return False, msg

        # 注销状态可能有短延迟，最多等待 3 秒确认当前设备已经离线。
        deadline = time.time() + 3
        while time.time() < deadline:
            after = self.get_auth_snapshot()
            if not after.online:
                return True, msg
            time.sleep(0.5)
        return False, f"{msg}\n ! 3秒内当前设备仍显示在线，可能注销失败，请稍后刷新认证页确认。"


def clear_screen() -> None:
    os.system("cls" if os.name == "nt" else "clear")


def select_operator() -> str:
    print(f"\n {C.BM}*{C.RST} 请选择运营商:")
    for op_id in ("1", "2", "3", "4"):
        marker = f" {C.DG}(默认){C.RST}" if op_id == DEFAULT_OPERATOR else ""
        print(f"   {C.BG}[{op_id}]{C.RST} {C.W}{OPERATORS[op_id]['name']}{C.RST}{marker}")
    while True:
        value = input(f" {C.BM}➤{C.RST} 请输入运营商编号[{C.DG}{DEFAULT_OPERATOR}{C.RST}]: ").strip()
        value = value or DEFAULT_OPERATOR
        if value in OPERATORS:
            return value
        print(f" {C.R}✗ 无效的运营商编号{C.RST}")


def format_dashboard_row(label1: str, value1: str, color1: str, label2: str, value2: str, color2: str) -> str:
    padding = " " * max(1, 24 - visible_width(value1))
    return (
        f" {C.BM}[*]{C.RST} {C.W}{label1}{C.RST} {C.DG}::{C.RST} "
        f"{color1}{value1}{C.RST}{padding}"
        f"{C.BM}[*]{C.RST} {C.W}{label2}{C.RST} {C.DG}::{C.RST} "
        f"{color2}{value2}{C.RST}"
    )


def print_dashboard(auth: ZJNUSimpleAuth, snapshot: AuthSnapshot) -> None:
    clear_screen()
    now_str = datetime.now().strftime("%Y-%m-%d %H:%M")
    print(f"{C.DG}{separator('=')}{C.RST}")
    header = f"///  ZJNU Campus Network Auth  ///    [ {now_str} ]"
    print(f"{C.BM}{center_text(header)}{C.RST}")
    print(f"{C.BC}{center_text('浙江师范大学校园网认证工具')}{C.RST}")
    print(f"{C.DG}{separator('=')}{C.RST}")
    print("")
    print(f" {C.BB}[+]{C.RST} {C.W}认证网关{C.RST} {C.DG}::{C.RST} {C.BC}{auth.portal_host}{C.RST}")
    print(
        f" {C.BB}[+]{C.RST} {C.W}本机设备{C.RST} {C.DG}::{C.RST} "
        f"{C.BY}{auth.local_ip}{C.RST} {C.DG}/{C.RST} {C.BY}{auth.local_mac.upper()}{C.RST}"
    )
    print(f"{C.DG}{separator('-')}{C.RST}")

    if snapshot.online:
        device = snapshot.current_device
        print(f" {C.BG}✓ 当前设备已认证在线{C.RST}")
        print(format_dashboard_row("用户账号", snapshot.account or "N/A", C.BW, "认证方式", auth_method_label(device), C.BW))
        print(format_dashboard_row("IP地址  ", str(device.get("online_ip") or auth.local_ip), C.BY, "MAC地址 ", str(device.get("online_mac") or auth.local_mac).upper(), C.BY))
        print(format_dashboard_row("登录时间", str(device.get("online_time") or "N/A"), C.BY, "在线时长", format_duration(device.get("time_long", "0")), C.BY))
    else:
        print(f" {C.R}✗ 当前设备未登录认证{C.RST}")
        print(f" {C.DG}{snapshot.message or '请先使用账号密码或手机验证码登录。'}{C.RST}")

    print(f"{C.DG}{separator('-')}{C.RST}")
    if snapshot.online:
        print(f"  {C.DG}[1] 账号密码登录{C.RST}")
        print(f"  {C.DG}[2] 手机验证码登录{C.RST}")
        print(f"  {C.BG}[3]{C.RST} {C.W}注销当前设备{C.RST}")
    else:
        print(f"  {C.BG}[1]{C.RST} {C.W}账号密码登录{C.RST}")
        print(f"  {C.BG}[2]{C.RST} {C.W}手机验证码登录{C.RST}")
        print(f"  {C.DG}[3] 注销当前设备{C.RST}")
    print(f"  {C.BG}[0]{C.RST} {C.W}退出工具{C.RST}")
    print("")


def print_device_detail(device: Dict[str, Any], current_ip: str, current_mac: str) -> None:
    print(f"  {C.W}IP地址:  {C.RST}{C.BY}{device.get('online_ip') or current_ip}{C.RST}")
    print(f"  {C.W}MAC地址: {C.RST}{C.BY}{str(device.get('online_mac') or current_mac).upper()}{C.RST}")
    print(f"  {C.W}账号:    {C.RST}{C.BY}{device.get('user_account', 'N/A')}{C.RST}")
    print(f"  {C.W}登录时间:{C.RST}{C.BY}{device.get('online_time', 'N/A')}{C.RST}")
    print(f"  {C.W}在线时长:{C.RST}{C.BY}{format_duration(device.get('time_long', '0'))}{C.RST}")


def print_device_list(devices: List[Dict[str, Any]], current_ip: str, current_mac: str) -> None:
    if not devices:
        print(f"{C.DG}没有获取到在线设备列表{C.RST}")
        return
    print(f"\n{C.BC}=== 在线设备：{len(devices)} 台 ==={C.RST}")
    for index, device in enumerate(devices, 1):
        ip = str(device.get("online_ip") or "")
        mac = str(device.get("online_mac") or current_mac).upper()
        account = str(device.get("user_account") or "N/A")
        login_time = str(device.get("online_time") or "N/A")
        duration = format_duration(device.get("time_long", "0"))
        current = f" {C.BG}[当前]{C.RST}" if ip == current_ip else ""
        print(f"  {C.BB}[{index}]{C.RST}{current} {C.BY}{ip or current_ip}{C.RST} / {C.BY}{mac}{C.RST}")
        print(f"      账号: {C.BW}{account}{C.RST} | 登录: {C.BY}{login_time}{C.RST} | 在线: {C.BY}{duration}{C.RST}")


def wait_snapshot(auth: ZJNUSimpleAuth, attempts: int = 3, delay: float = 1.0) -> AuthSnapshot:
    snapshot = AuthSnapshot(False, "当前设备未登录")
    for _ in range(attempts):
        snapshot = auth.get_auth_snapshot()
        if snapshot.online:
            return snapshot
        time.sleep(delay)
    return snapshot


def run_with_spinner(message: str, operation):
    """运行阻塞操作时显示转圈动画，操作完成后返回函数结果。"""
    result: Dict[str, Any] = {}

    def worker() -> None:
        try:
            result["value"] = operation()
        except Exception as exc:  # noqa: BLE001 - 交互式工具需要把异常转成用户可读信息
            result["error"] = exc

    thread = threading.Thread(target=worker, daemon=True)
    thread.start()
    frames = "|/-\\"
    index = 0
    while thread.is_alive():
        print(f"\r {C.BY}{frames[index % len(frames)]}{C.RST} {message}...", end="", flush=True)
        index += 1
        time.sleep(0.12)
    thread.join()
    print("\r" + " " * (visible_width(message) + 12) + "\r", end="", flush=True)
    if "error" in result:
        return False, f"请求异常: {result['error']}"
    return result.get("value")


def interactive_password_login(auth: ZJNUSimpleAuth) -> None:
    print(f"\n{C.BC}=== 账号密码登录 ==={C.RST}")
    username = input(f" {C.BM}*{C.RST} 请输入账号: ").strip()
    if not username:
        print(f"{C.R}✗ 账号不能为空{C.RST}")
        return

    password = getpass.getpass(f" {C.BM}*{C.RST} 请输入密码: ").strip()
    if not password:
        print(f"{C.R}✗ 密码不能为空{C.RST}")
        return

    operator = select_operator()
    ok, msg = auth.login_with_password(username, password, operator)
    print(f"\n{(C.BG + '✓') if ok else (C.R + '✗')} {C.W}{msg}{C.RST}")
    if not ok:
        return

    net_ok, net_msg = run_with_spinner("正在测试外网连通", lambda: test_external_network())
    if net_ok:
        print(f"{C.BG}✓ 外网连通测试通过: {net_msg}{C.RST}")
    else:
        print(f"{C.BY}! 外网连通测试未通过: {net_msg}{C.RST}")

    snapshot = wait_snapshot(auth)
    account = snapshot.account or username
    result = auth.query_account_devices_with_current(account, snapshot)
    print_device_list(result.devices, auth.local_ip, auth.local_mac)


def interactive_phone_login(auth: ZJNUSimpleAuth) -> None:
    print(f"\n{C.BC}=== 手机验证码登录 ==={C.RST}")
    phone = input(f" {C.BM}*{C.RST} 请输入手机号: ").strip()
    if not phone:
        print(f"{C.R}✗ 手机号不能为空{C.RST}")
        return

    ok, msg = run_with_spinner("正在发送短信验证码", lambda: auth.send_sms(phone))
    my_msg = "验证码正在下发，稍有延迟，请用户注意查收！"
    my_wing = "请检查手机号码是否正确或者重复发送验证码，请退出重新登录..."
    print(f" {(C.BG + '✓') if ok else (C.R + '✗')} {C.W}{my_msg}{C.RST}")
    if not ok:
#        print(f" {C.R}✗ 发送失败原因: {msg or '门户未返回明确原因'}{C.RST}")
        print(f" ! {C.BY}{my_wing}{C.RST}")
        return

    code = input(f" {C.BM}*{C.RST} 请输入短信验证码: ").strip()
    if not code:
        print(f" {C.R}✗ 验证码不能为空，请重新输入一次{C.RST}")
        code = input(f" {C.BM}*{C.RST} 请输入短信验证码: ").strip()
        if not code:
            print(f" {C.R}✗ 验证码不能为空{C.RST}")
            return

    operator = select_operator()
    ok, msg = auth.login_with_phone(phone, code, operator)
    print(f"\n{(C.BG + '✓') if ok else (C.R + '✗')} {C.W}{msg}{C.RST}")
    if not ok:
        code = input(f" {C.BM}*{C.RST} 登录失败，请重新输入短信验证码（直接回车取消）: ").strip()
        if not code:
            return
        ok, msg = auth.login_with_phone(phone, code, operator)
        print(f"\n{(C.BG + '✓') if ok else (C.R + '✗')} {C.W}{msg}{C.RST}")
        if not ok:
            return

    net_ok, net_msg = run_with_spinner("正在测试外网连通", lambda: test_external_network())
    if net_ok:
        print(f"{C.BG}✓ 外网连通测试通过: {net_msg}{C.RST}")
    else:
        print(f"{C.BY}! 外网连通测试未通过: {net_msg}{C.RST}")

    snapshot = wait_snapshot(auth)
    if snapshot.online:
        account = snapshot.account or phone
        result = auth.query_account_devices_with_current(account, snapshot)
        print_device_list(result.devices, auth.local_ip, auth.local_mac)
    else:
        print(f"{C.BY}! 登录接口返回成功，但暂未查询到当前在线详情，请稍后刷新。{C.RST}")


def interactive_logout(auth: ZJNUSimpleAuth) -> None:
    print(f"\n{C.BC}=== 注销当前设备 ==={C.RST}")
    print(f" {C.BB}[+]{C.RST} 当前IP: {C.BY}{auth.local_ip}{C.RST}")
    print(f" {C.BB}[+]{C.RST} 当前MAC: {C.BY}{auth.local_mac.upper()}{C.RST}")
    if os.environ.get("SSH_CONNECTION"):
        print(f" {C.BY}！注销认证本身可能让远程连接短暂中断。{C.RST}")

    confirm = input(f"\n {C.BM}➤{C.RST} 确认注销当前设备吗? [{C.DG}y/N{C.RST}]: ").strip().lower()
    if confirm != "y":
        print(f"{C.DG}已取消{C.RST}")
        return

    ok, msg = run_with_spinner("正在注销当前设备", lambda: auth.logout_current_safe())
    color = C.BG if ok else C.BY
    symbol = "✓" if ok else "!"
    print(f"{color} {symbol} {msg}{C.RST}")


def interactive_mode() -> None:
    auth = ZJNUSimpleAuth()
    while True:
        snapshot = auth.get_auth_snapshot()
        print_dashboard(auth, snapshot)
        choice = input(f"{C.BG}➤{C.RST} {C.BW}请输入操作编号[0-3]:{C.RST} ").strip()

        if choice in {"1", "2"} and snapshot.online:
            print(f"\n{C.R}✗ 当前设备已登录，请先注销当前设备后再重新登录。{C.RST}")
        elif choice == "3" and not snapshot.online:
            print(f"\n{C.R}✗ 当前设备未登录，无法使用注销功能。{C.RST}")
        elif choice == "1":
            interactive_password_login(auth)
        elif choice == "2":
            interactive_phone_login(auth)
        elif choice == "3":
            interactive_logout(auth)
        elif choice == "0":
            print(f"\n{C.BC}再见！{C.RST}")
            return
        else:
            print(f"\n{C.R}✗ 无效选择{C.RST}")

        input(f"\n{C.DG}按回车继续...{C.RST}")


def debug_log(message: str) -> None:
    if CLI_DEBUG:
        print(f"[DEBUG] {message}", file=sys.stderr)


def confirm_login_state(auth: ZJNUSimpleAuth) -> Optional[bool]:
    """CLI 专用登录状态确认；返回 None 表示两轮检测仍不一致。"""

    def check_pair() -> Tuple[bool, bool]:
        first = auth.get_auth_snapshot().online
        debug_log(f"状态检测第1次: {'已登录' if first else '未登录'}")
        time.sleep(CLI_STATE_PAIR_INTERVAL)
        second = auth.get_auth_snapshot().online
        debug_log(f"状态检测第2次: {'已登录' if second else '未登录'}")
        return first, second

    debug_log("开始确认当前登录状态")
    first, second = check_pair()
    if first and second:
        debug_log("两次检测均为已登录，确认已登录")
        return True
    if not first and not second:
        debug_log("两次检测均为未登录，确认未登录")
        return False

    debug_log("两次检测不一致，等待后重试")
    time.sleep(CLI_STATE_RETRY_DELAY)
    first, second = check_pair()
    if first and second:
        debug_log("重试后两次检测均为已登录，确认已登录")
        return True
    if not first and not second:
        debug_log("重试后两次检测均为未登录，确认未登录")
        return False
    debug_log("重试后状态仍不一致，返回状态不稳定")
    return None


def cli_login(args: argparse.Namespace) -> int:
    username = args.username_opt or args.username
    password = args.password_opt or args.password
    if not username or not password:
        debug_log("login 参数不完整，缺少账号或密码，退出码 1")
        return 1

    debug_log(f"执行 login 命令，账号={username}，运营商={args.operator}")
    auth = ZJNUSimpleAuth()
    debug_log(f"认证网关={auth.api_base}，本机IP={auth.local_ip}，本机MAC={auth.local_mac}")
    state = confirm_login_state(auth)
    if state is None:
        debug_log("登录前状态不稳定，退出码 3")
        return 3
    if state:
        debug_log("当前已登录，不执行登录请求，退出码 0")
        return 0

    debug_log("当前未登录，开始提交账号密码登录请求")
    ok, msg = auth.login_with_password(username, password, normalize_operator(args.operator))
    debug_log(f"登录请求结果: ok={ok}，message={msg}")
    if not ok:
        debug_log("登录请求失败，退出码 1")
        return 1

    state = confirm_login_state(auth)
    if state is None:
        debug_log("登录后状态不稳定，退出码 3")
        return 3
    debug_log(f"登录后确认状态={'已登录' if state else '未登录'}，退出码 {0 if state else 1}")
    return 0 if state else 1


def cli_logout(args: argparse.Namespace) -> int:
    _ = args
    debug_log("执行 logout 命令")
    auth = ZJNUSimpleAuth()
    debug_log(f"认证网关={auth.api_base}，本机IP={auth.local_ip}，本机MAC={auth.local_mac}")
    state = confirm_login_state(auth)
    if state is None:
        debug_log("注销前状态不稳定，退出码 3")
        return 3
    if not state:
        debug_log("当前未登录，不执行注销请求，退出码 0")
        return 0

    snapshot = auth.get_auth_snapshot()
    if not snapshot.online:
        debug_log("状态确认后重新读取快照显示未登录，退出码 0")
        return 0

    debug_log(
        "提交注销请求: "
        f"账号={snapshot.account}，IP={snapshot.current_device.get('online_ip', auth.local_ip)}，"
        f"MAC={snapshot.current_device.get('online_mac', auth.local_mac)}"
    )
    ok, msg = auth.submit_browser_style_logout(snapshot)
    debug_log(f"注销请求结果: ok={ok}，message={msg}")
    if not ok:
        debug_log("注销请求提交失败，退出码 2")
        return 2

    for index in range(CLI_LOGOUT_CHECKS):
        online = auth.get_auth_snapshot().online
        debug_log(f"注销后第 {index + 1}/{CLI_LOGOUT_CHECKS} 次检测: {'已登录' if online else '未登录'}")
        if not online:
            debug_log("检测到未登录，退出码 0")
            return 0
        time.sleep(CLI_LOGOUT_INTERVAL)
    debug_log(f"{CLI_LOGOUT_CHECKS} 次检测仍为已登录，退出码 4")
    return 4


def cli_status(args: argparse.Namespace) -> int:
    _ = args
    debug_log("执行 status 命令")
    auth = ZJNUSimpleAuth()
    debug_log(f"认证网关={auth.api_base}，本机IP={auth.local_ip}，本机MAC={auth.local_mac}")
    state = confirm_login_state(auth)
    if state is None:
        debug_log("状态不稳定，退出码 3")
        return 3
    if not state:
        debug_log("当前未登录，退出码 1")
        return 1

    snapshot = auth.get_auth_snapshot()
    if not snapshot.online or not snapshot.account:
        debug_log("状态确认后未获取到登录账号，退出码 1")
        return 1
    print(snapshot.account)
    debug_log(f"输出登录账号: {snapshot.account}，退出码 0")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="浙江师范大学校园网认证工具")
    parser.add_argument("--debug", action="store_true", help="参数模式的调试")
    subparsers = parser.add_subparsers(dest="command")

    login = subparsers.add_parser("login", help="账号密码登录")
    login.add_argument("username", nargs="?", help="账号")
    login.add_argument("password", nargs="?", help="密码")
    login.add_argument("-u", "--username", dest="username_opt", help="账号")
    login.add_argument("-p", "--password", dest="password_opt", help="密码")
    login.add_argument("-o", "--operator", choices=("1", "2", "3", "4"), default=DEFAULT_OPERATOR, help="运营商编号")

    subparsers.add_parser("logout", help="注销当前设备")
    subparsers.add_parser("status", help="输出当前登录账号")
    return parser


def cli_mode(argv: List[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    global CLI_DEBUG
    CLI_DEBUG = bool(args.debug)
    debug_log(f"解析参数完成: command={args.command}")
    if args.command == "login":
        code = cli_login(args)
        debug_log(f"login 最终退出码 {code}")
        return code
    if args.command == "logout":
        code = cli_logout(args)
        debug_log(f"logout 最终退出码 {code}")
        return code
    if args.command == "status":
        code = cli_status(args)
        debug_log(f"status 最终退出码 {code}")
        return code
    parser.print_help()
    return 0


def main() -> int:
    try:
        if len(sys.argv) > 1:
            return cli_mode(sys.argv[1:])
        interactive_mode()
        return 0
    except KeyboardInterrupt:
        print(f"\n{C.R}程序被用户中断{C.RST}")
        return 130


if __name__ == "__main__":
    sys.exit(main())
