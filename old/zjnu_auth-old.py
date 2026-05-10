#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
浙江师范大学校园网认证工具 v1.5 (旧网络)
认证地址: http://10.66.254.253
Dr.COM 哆点 Portal 系统
"""

import requests
import time
import re
import sys
from typing import Optional, Dict, Tuple

# ============================================================================
# 配置
# ============================================================================

BASE_URL = "http://10.66.254.253"
EPORTAL_PORT = 801
VERSION = "1.5"

# ============================================================================
# 终端颜色
# ============================================================================

class C:
    R = '\033[0m'       # reset
    B = '\033[1m'       # bold
    G = '\033[92m'      # green
    E = '\033[91m'      # red/error
    W = '\033[93m'      # yellow/warning
    I = '\033[96m'      # cyan/info
    D = '\033[2m'       # dim
    M = '\033[95m'      # magenta


def hdr(text: str):
    """居中标题"""
    w = 56
    print(f"\n{C.B}{C.I}{'═' * w}{C.R}")
    print(f"{C.B}{C.I}{text:^{w}}{C.R}")
    print(f"{C.B}{C.I}{'═' * w}{C.R}")

def sep():
    print(f"{C.D}{'─' * 56}{C.R}")

def ok(text: str):
    print(f"  {C.G}✓{C.R} {text}")

def err(text: str):
    print(f"  {C.E}✗{C.R} {text}")

def info(text: str):
    print(f"  {C.I}›{C.R} {text}")

def warn(text: str):
    print(f"  {C.W}⚠{C.R} {text}")

def stat(label: str, value: str, color: str = C.I):
    print(f"  {C.D}{label:12s}{C.R} {color}{value}{C.R}")

def spin(sec: float, msg: str = ""):
    if msg:
        print(f"  {C.D}{msg}...{C.R}", end='', flush=True)
    time.sleep(sec)
    if msg:
        print(f" {C.G}完成{C.R}")


# ============================================================================
# 认证类
# ============================================================================

class ZJNUAuth:
    """浙江师范大学旧网络认证 (Dr.COM)"""

    def __init__(self):
        self.base = BASE_URL
        self.eport = f"{BASE_URL}:{EPORTAL_PORT}"

    # ── 状态检测 ──────────────────────────────────────────────

    def check_status(self) -> Optional[str]:
        """检查当前在线账号，离线返回 None"""
        try:
            resp = requests.get(self.base, timeout=8)
            m = re.search(r"uid='([^']+)'", resp.text)
            if m and m.group(1):
                return m.group(1)
            return None
        except Exception:
            return None

    def get_details(self) -> Optional[Dict[str, str]]:
        """获取详细登录信息"""
        try:
            resp = requests.get(self.base, timeout=8)
            vars_ = dict(re.findall(r"(\w+)='([^']*)'", resp.text))
            if not vars_.get('uid'):
                return None

            # 追加 v4ip, v6ip 等
            for k in ('v4ip', 'v6ip', 'olmac', 'oltime', 'time', 'flow', 'fee'):
                m = re.search(rf"{k}='([^']*)'", resp.text)
                if m and m.group(1):
                    vars_[k] = m.group(1)

            return vars_
        except Exception:
            return None

    def get_account_type(self) -> Tuple[str, str]:
        """判断账号类型 (基于 fee 字段)"""
        try:
            resp = requests.get(self.base, timeout=5)
            m = re.search(r"fee='([^']*)'", resp.text)
            if m and m.group(1).strip() != '0':
                return 'phone', '手机登录'
            return 'account', '账号登录'
        except Exception:
            return 'unknown', '未知'

    # ── 显示状态 ──────────────────────────────────────────────

    def show_status(self):
        """打印当前状态面板"""
        print(f"\n{C.B}▎网络状态{C.R}")
        sep()
        uid = self.check_status()
        if uid:
            typ, tn = self.get_account_type()
            det = self.get_details()
            stat("状态", "已连接", C.G)
            stat("账号", uid)
            stat("类型", tn)
            if det:
                if det.get('v4ip'):
                    stat("IPv4", det['v4ip'])
                if det.get('v6ip'):
                    stat("IPv6", det['v6ip'])
                if det.get('olmac'):
                    mac = det['olmac']
                    if len(mac) == 12:
                        mac = ':'.join(mac[i:i+2] for i in range(0, 12, 2))
                    stat("MAC", mac)
                if det.get('time'):
                    stat("时长", f"{det['time']} 分钟")
                if det.get('flow'):
                    try:
                        mb = int(det['flow']) / 1024
                        stat("流量", f"{mb:.1f} MB")
                    except Exception:
                        pass
        else:
            stat("状态", "未连接", C.E)

    # ── 登录 ──────────────────────────────────────────────────

    def login_account(self, username: str, password: str) -> bool:
        """账号密码登录"""
        hdr("账号登录")

        stat("账号", username)

        cur = self.check_status()
        if cur:
            info(f"当前在线: {cur}")
            if cur == username:
                ok("已登录，无需重复操作")
                return True

            typ, _ = self.get_account_type()
            if typ == 'phone':
                info("检测到手机登录在线，使用双重登录策略覆盖")
                return self._double_login(username, password)
            else:
                info(f"当前 {cur} 在线，将替换登录")
        print()

        info("正在认证...")
        return self._do_login(username, password)

    def login_phone(self, phone: str) -> bool:
        """手机验证码登录

        注意: 旧网络 getChallengeCode 返回 base64 编码的验证码，
        可直接解码使用，无需手动输入
        """
        hdr("手机验证码登录")

        stat("手机号", phone)

        cur = self.check_status()
        if cur:
            info(f"当前在线: {cur}")
            if cur == phone:
                ok("已登录，无需重复操作")
                return True
            typ, _ = self.get_account_type()
            if typ == 'account':
                err("当前有账号登录在线，手机无法覆盖")
                info("请先注销账号登录")
                return False

        # 步骤1: 获取验证码
        print(f"\n  {C.D}[1/3] 获取验证码{C.R}")
        code = self._get_challenge(phone)
        if not code:
            return False
        ok(f"验证码已获取: {code}")
        print()

        # 步骤2: 等待 (原代码逻辑)
        print(f"  {C.D}[2/3] 等待 3 秒{C.R}")
        spin(3)

        # 步骤3: 登录
        print(f"  {C.D}[3/3] 执行登录{C.R}")
        return self._do_login(phone, code)

    def _get_challenge(self, phone: str) -> Optional[str]:
        """获取短信验证码 (旧接口直接返回)

        注意: 此接口可能在旧网络已失效，返回 EPortal 管理页而非验证码
        """
        try:
            resp = requests.get(
                f"{self.eport}/eportal/?c=ACSetting&a=getChallengeCode"
                f"&callback=cb&phone={phone}",
                timeout=15
            )
            m = re.search(r'"code":"([^"]+)"', resp.text)
            if m:
                import base64
                return base64.b64decode(m.group(1)).decode('utf-8')

            # 尝试备用解析
            m2 = re.search(r'code["\']?\s*:\s*["\']([^"\']+)["\']', resp.text)
            if m2:
                import base64
                return base64.b64decode(m2.group(1)).decode('utf-8')

            err("获取验证码失败: 接口返回异常")
            warn("getChallengeCode 接口可能已变更，建议使用新网络认证")
            return None
        except Exception as e:
            err(f"获取验证码失败: {e}")
            return None

    def _do_login(self, user: str, pwd: str) -> bool:
        """执行登录 (POST 到 eportal)"""
        data = {
            'DDDDD': user,
            'upass': pwd,
            'R1': '0', 'R2': '0', 'R3': '0',
            'R6': '0', 'para': '00', 'terminal_type': '1'
        }
        try:
            resp = requests.post(
                f"{self.eport}/eportal/?c=ACSetting&a=Login",
                data=data, timeout=15
            )

            m = re.search(r'Msg=(\d+)', resp.text)
            if m and m.group(1) != '00':
                code = m.group(1)
                msga = re.search(r"msga='([^']+)'", resp.text)
                msg = msga.group(1) if msga else f"Msg={code}"
                err(f"认证失败: {msg}")

                # 错误解读
                if '超时' in msg or 'timeout' in msg.lower():
                    warn("认证服务器超时，请稍后重试")
                elif '密码' in msg or 'password' in msg.lower():
                    warn("密码错误")
                elif '不存在' in msg:
                    warn("账号不存在")
                return False

            spin(2, "验证状态")

            new_uid = self.check_status()
            if new_uid and new_uid == user:
                ok(f"登录成功: {new_uid}")
                return True
            elif new_uid:
                warn(f"登录已提交，当前在线: {new_uid}")
                return True
            else:
                err("登录失败: 当前离线")
                return False

        except Exception as e:
            err(f"请求失败: {e}")
            return False

    def _double_login(self, user: str, pwd: str) -> bool:
        """双重登录策略 (覆盖手机登录)"""
        # 第一次登录
        print(f"  {C.D}  [a] 第一次登录{C.R}")
        self._do_login(user, pwd)

        # 注销
        print(f"  {C.D}  [b] 调用注销接口{C.R}")
        requests.get(f"{self.eport}/eportal/?c=ACSetting&a=Logout", timeout=8)
        time.sleep(2)

        # 第二次登录
        print(f"  {C.D}  [c] 第二次登录{C.R}")
        return self._do_login(user, pwd)

    # ── 注销 ──────────────────────────────────────────────────

    def logout(self) -> bool:
        """注销登录 (多策略)"""
        hdr("注销登录")

        cur = self.check_status()
        if not cur:
            info("当前未登录")
            return True

        typ, tn = self.get_account_type()
        stat("账号", cur)
        stat("类型", tn)
        print()

        # 策略选择
        if typ == 'phone':
            return self._logout_phone(cur)
        else:
            return self._logout_account(cur)

    def _logout_account(self, uid: str) -> bool:
        """注销账号登录

        策略:
        1. POST 注销 (ver=1.0) — JS 实际使用的方法
        2. GET 注销 — 简单兼容方法
        3. 如均失败提示自助服务
        """
        methods = [
            ("POST", self._logout_post),
            ("GET", self._logout_get),
        ]

        for name, fn in methods:
            info(f"尝试 {name} 注销...")
            fn()
            spin(3, "等待生效")
            if not self.check_status():
                ok("注销成功！")
                return True

        # 所有方法都失败
        warn("AC 层注销失败 (ACLogOut=2)")
        info("Dr.COM 接入控制器未响应断开请求")
        info("请尝试以下方式:")
        print(f"  {C.I}  1.{C.R} 访问自助服务: http://rzself.zjnu.edu.cn/Self")
        print(f"  {C.I}  2.{C.R} 断开网络连接等待 DHCP 过期")
        print(f"  {C.I}  3.{C.R} 联系信息技术中心处理")
        return False

    def _logout_phone(self, uid: str) -> bool:
        """注销手机登录 (需要学号覆盖)"""
        info("手机登录需要使用学号覆盖注销")
        print()

        account = input(f"  {C.I}学号: {C.R}").strip()
        if not account:
            err("已取消")
            return False

        password = input(f"  {C.I}密码: {C.R}").strip()
        if not password:
            err("已取消")
            return False
        print()

        for attempt in range(2):
            if attempt > 0:
                info("会话恢复，再次尝试...")
                print()

            # 学号覆盖 + 注销
            print(f"  {C.D}[第 {attempt+1} 轮] 学号覆盖手机{C.R}")
            self._do_login(account, password)

            print(f"  {C.D}注销学号会话{C.R}")
            self._logout_post()
            spin(3, "等待生效")

            if not self.check_status():
                ok("完全注销成功！")
                return True

        warn("注销未完全成功，部分会话可能仍在线")
        return False

    def _logout_post(self):
        """POST 注销 (JS 实际使用的 ver=1.0)"""
        try:
            requests.post(
                f"{self.eport}/eportal/?c=ACSetting&a=Logout&ver=1.0",
                timeout=10
            )
        except Exception:
            pass

    def _logout_get(self):
        """GET 注销 (兼容旧版本)"""
        try:
            requests.get(
                f"{self.eport}/eportal/?c=ACSetting&a=Logout",
                timeout=10
            )
        except Exception:
            pass


# ============================================================================
# 交互式菜单
# ============================================================================

def interactive():
    """交互式主菜单"""
    hdr("浙江师范大学校园网认证 v1.5")

    auth = ZJNUAuth()

    while True:
        auth.show_status()
        print()

        print(f"{C.B}操作{C.R}")
        sep()
        print(f"  {C.I}[1]{C.R} 账号密码登录")
        print(f"  {C.I}[2]{C.R} 手机验证码登录")
        print(f"  {C.I}[3]{C.R} 注销")
        print(f"  {C.I}[4]{C.R} 刷新状态")
        print(f"  {C.I}[0]{C.R} 退出")
        print()

        try:
            ch = input(f"{C.W}选择 › {C.R}").strip()
        except (EOFError, KeyboardInterrupt):
            print(f"\n{C.I}再见！{C.R}\n")
            sys.exit(0)

        if ch == '1':
            print()
            account = input(f"  {C.I}学号: {C.R}").strip()
            if not account: continue
            password = input(f"  {C.I}密码: {C.R}").strip()
            if not password: continue
            print()
            auth.login_account(account, password)
            input(f"\n  {C.D}按回车继续...{C.R}")
            hdr("浙江师范大学校园网认证 v1.5")

        elif ch == '2':
            print()
            phone = input(f"  {C.I}手机号: {C.R}").strip()
            if not phone: continue
            print()
            auth.login_phone(phone)
            input(f"\n  {C.D}按回车继续...{C.R}")
            hdr("浙江师范大学校园网认证 v1.5")

        elif ch == '3':
            print()
            auth.logout()
            input(f"\n  {C.D}按回车继续...{C.R}")
            hdr("浙江师范大学校园网认证 v1.5")

        elif ch == '4':
            hdr("浙江师范大学校园网认证 v1.5")

        elif ch == '0':
            print(f"\n{C.I}再见！{C.R}\n")
            sys.exit(0)

        else:
            err("无效选项")
            time.sleep(0.5)
            hdr("浙江师范大学校园网认证 v1.5")


# ============================================================================
# 命令行入口
# ============================================================================

def main():
    import argparse

    p = argparse.ArgumentParser(
        description='浙江师范大学校园网认证工具 v1.5 (旧网络)',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument('--status', action='store_true', help='查看认证状态')
    p.add_argument('--account', help='学号')
    p.add_argument('--password', help='密码')
    p.add_argument('--phone', help='手机号')
    p.add_argument('--logout', action='store_true', help='注销登录')

    args = p.parse_args()
    auth = ZJNUAuth()

    try:
        if args.status:
            auth.show_status()
        elif args.logout:
            auth.logout()
        elif args.account:
            if not args.password:
                err("需要 --password")
                sys.exit(1)
            ok_ = auth.login_account(args.account, args.password)
            sys.exit(0 if ok_ else 1)
        elif args.phone:
            ok_ = auth.login_phone(args.phone)
            sys.exit(0 if ok_ else 1)
        else:
            interactive()

    except KeyboardInterrupt:
        print(f"\n\n{C.I}再见！{C.R}\n")
        sys.exit(0)
    except Exception as e:
        err(f"异常: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()
