#!/usr/bin/env bash
# 浙江师范大学校园网认证工具 Bash 版
# 依赖：bash、curl、sed、grep、awk、ip/hostname、ping

set -u

JS_VERSION="4.2.1"
TERMINAL_TYPE="1"
API_BASE="${ZJNU_AUTH_API_BASE:-http://10.1.116.8:801/eportal/portal}"
PING_HOSTS=("114.114.114.114" "223.5.5.5" "www.baidu.com")
DEFAULT_OPERATOR="1"
CLI_STATE_PAIR_INTERVAL="0.5"
CLI_STATE_RETRY_DELAY="1"
CLI_LOGOUT_CHECKS=4
CLI_LOGOUT_INTERVAL=1
REQUEST_TIMEOUT="${ZJNU_AUTH_TIMEOUT:-10}"
CLI_DEBUG=0
UI_WIDTH=74

# 终端颜色，与 Python 版 C 类保持一致。
C_RST=$'\033[0m'
C_DG=$'\033[90m'
C_W=$'\033[37m'
C_BW=$'\033[97m'
C_BC=$'\033[96m'
C_BB=$'\033[94m'
C_BM=$'\033[95m'
C_BG=$'\033[92m'
C_BY=$'\033[93m'
C_R=$'\033[31m'

# GNU sed 旧版本常用 -r，新版本支持 -E；运行时探测一次。
SED_ERE="-E"
if ! printf '' | sed -E '' >/dev/null 2>&1; then
    SED_ERE="-r"
fi

LOCAL_IP=""
LOCAL_MAC=""
LAST_JSON=""
LAST_MSG=""
LAST_OK=1
SNAPSHOT_ONLINE=0
SNAPSHOT_ACCOUNT=""
SNAPSHOT_IP=""
SNAPSHOT_MAC=""
SNAPSHOT_ONLINE_TIME=""
SNAPSHOT_TIME_LONG="0"
SNAPSHOT_IS_PERCEIVE=""
SNAPSHOT_MESSAGE="当前设备未登录"
RUN_CODE=0
RUN_OUTPUT=""
RUN_MSG=""
OPERATOR_CHOICE="$DEFAULT_OPERATOR"

debug_log() {
    if [[ "$CLI_DEBUG" == "1" ]]; then
        printf '[DEBUG] %s\n' "$*" >&2
    fi
}

portal_host() {
    printf '%s\n' "$API_BASE" | sed "$SED_ERE" 's#^[a-zA-Z]+://([^/:]+).*#\1#'
}

random_v() {
    local value
    value="$(date +%s%N 2>/dev/null)"
    value="${value//[^0-9]/}"
    [[ -n "$value" ]] || value="$(date +%s)"
    printf '%s\n' "$((value % 100000))"
}

callback_name() {
    printf 'dr%s\n' "$(random_v)"
}

normalize_operator() {
    case "${1:-}" in
        1|2|3|4) printf '%s\n' "$1" ;;
        *) printf '%s\n' "$DEFAULT_OPERATOR" ;;
    esac
}

operator_name() {
    case "$1" in
        1) printf '校园用户' ;;
        2) printf '校园电信' ;;
        3) printf '校园联通' ;;
        4) printf '校园移动' ;;
        *) printf '校园用户' ;;
    esac
}

repeat_char() {
    local char="$1"
    local count="$2"
    local out=""
    while [ "$count" -gt 0 ]; do
        out="${out}${char}"
        count=$((count - 1))
    done
    printf '%s' "$out"
}

visible_width() {
    # 兼容老 Linux：不用 Python/Perl，按 UTF-8 字节估算中文宽度。
    # ASCII 按 1 列，大多数中文和全角符号按 2 列；这覆盖本工具所有固定 UI 文案。
    local value="$1"
    local bytes ascii_bytes non_ascii_bytes non_ascii_chars
    bytes="$(printf '%s' "$value" | wc -c | tr -d '[:space:]')"
    ascii_bytes="$(printf '%s' "$value" | LC_ALL=C tr -cd '\000-\177' | wc -c | tr -d '[:space:]')"
    non_ascii_bytes=$((bytes - ascii_bytes))
    non_ascii_chars=$(((non_ascii_bytes + 2) / 3))
    printf '%s\n' "$((ascii_bytes + non_ascii_chars * 2))"
}

center_text() {
    local value="$1"
    local width="${2:-$UI_WIDTH}"
    local text_width padding left right
    text_width="$(visible_width "$value")"
    padding=$((width - text_width))
    [ "$padding" -lt 0 ] && padding=0
    left=$((padding / 2))
    right=$((padding - left))
    printf '%s%s%s' "$(repeat_char ' ' "$left")" "$value" "$(repeat_char ' ' "$right")"
}

separator() {
    repeat_char "${1:-=}" "$UI_WIDTH"
}

lower_ascii() {
    printf '%s' "$1" | tr 'A-Z' 'a-z'
}

upper_ascii() {
    printf '%s' "$1" | tr 'a-f' 'A-F'
}

user_account() {
    local account="$1"
    local operator
    operator="$(normalize_operator "${2:-$DEFAULT_OPERATOR}")"
    case "$operator" in
        2)
            [[ "$account" == *"@dx" ]] || account="${account}@dx"
            ;;
        3)
            [[ "$account" == *"@lt" ]] || account="${account}@lt"
            ;;
    esac
    printf ',%s,%s\n' "$operator" "$account"
}

get_local_ip() {
    local host route ip
    host="$(portal_host)"
    if command -v ip >/dev/null 2>&1; then
        route="$(ip route get "$host" 2>/dev/null)"
        ip="$(printf '%s\n' "$route" | sed -n "$SED_ERE" 's/.* src ([0-9.]+).*/\1/p' | head -n 1)"
        if [[ -n "$ip" ]]; then
            printf '%s\n' "$ip"
            return
        fi
    fi
    if command -v hostname >/dev/null 2>&1; then
        ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
        if [[ -n "$ip" ]]; then
            printf '%s\n' "$ip"
            return
        fi
    fi
    if command -v ifconfig >/dev/null 2>&1; then
        ip="$(ifconfig 2>/dev/null | awk '/inet / && $2 !~ /^127\./ {print $2; exit}' | sed 's/^addr://')"
        if [[ -n "$ip" ]]; then
            printf '%s\n' "$ip"
            return
        fi
    fi
    printf '0.0.0.0\n'
}

get_default_interface() {
    awk '$2 == "00000000" {print $1; exit}' /proc/net/route 2>/dev/null
}

normalize_mac() {
    printf '%s' "$1" | tr -d ':-' | tr -d '[:space:]' | tr 'A-F' 'a-f'
}

get_mac_address() {
    local iface path mac
    iface="$(get_default_interface)"
    if [[ -n "$iface" ]]; then
        path="/sys/class/net/${iface}/address"
        if [[ -r "$path" ]]; then
            mac="$(normalize_mac "$(cat "$path")")"
            if [[ -n "$mac" && "$mac" != "000000000000" ]]; then
                printf '%s\n' "$mac"
                return
            fi
        fi
    fi

    for path in /sys/class/net/*/address; do
        [[ -r "$path" ]] || continue
        iface="${path%/address}"
        iface="${iface##*/}"
        case "$iface" in
            lo|docker0|virbr*|veth*) continue ;;
        esac
        mac="$(normalize_mac "$(cat "$path")")"
        if [[ -n "$mac" && "$mac" != "000000000000" ]]; then
            printf '%s\n' "$mac"
            return
        fi
    done
    printf '000000000000\n'
}

init_device() {
    LOCAL_IP="$(get_local_ip)"
    LOCAL_MAC="$(get_mac_address)"
}

strip_jsonp() {
    local text="$1"
    text="${text//$'\r'/}"
    text="${text//$'\n'/}"
    text="$(printf '%s' "$text" | sed "$SED_ERE" 's/^[[:space:]]+|[[:space:]]+$//g')"
    if [[ "$text" == \{* || "$text" == \[* ]]; then
        printf '%s\n' "$text"
        return
    fi
    if [[ "$text" == *"("*")"* ]]; then
        text="${text#*(}"
        text="${text%)*}"
        text="${text%;}"
        printf '%s\n' "$text"
    else
        printf '%s\n' "$text"
    fi
}

json_value() {
    local key="$1"
    local json="$2"
    # 不用 sed 的贪婪正则，避免多个同名字段时拿到最后一个字段。
    printf '%s\n' "$json" | awk -v key="$key" '
        function trim(v) {
            sub(/^[ \t\r\n]+/, "", v)
            sub(/[ \t\r\n]+$/, "", v)
            return v
        }
        function parse_string(s,    i,ch,esc,out) {
            esc = 0
            out = ""
            for (i = 2; i <= length(s); i++) {
                ch = substr(s, i, 1)
                if (esc) {
                    out = out ch
                    esc = 0
                } else if (ch == "\\") {
                    esc = 1
                } else if (ch == "\"") {
                    print out
                    exit
                } else {
                    out = out ch
                }
            }
            print out
            exit
        }
        { data = data $0 }
        END {
            needle = "\"" key "\""
            pos = index(data, needle)
            if (!pos) exit
            rest = substr(data, pos + length(needle))
            colon = index(rest, ":")
            if (!colon) exit
            rest = substr(rest, colon + 1)
            sub(/^[ \t\r\n]+/, "", rest)
            if (substr(rest, 1, 1) == "\"") {
                parse_string(rest)
            }
            if (match(rest, /^[^,}\]]+/)) {
                print trim(substr(rest, RSTART, RLENGTH))
            }
        }
    '
}

json_list_has_object() {
    local json="$1"
    printf '%s\n' "$json" | awk '
        { data = data $0 }
        END {
            pos = index(data, "\"list\"")
            if (!pos) exit 1
            rest = substr(data, pos)
            lb = index(rest, "[")
            if (!lb) exit 1
            rest = substr(rest, lb + 1)
            if (rest ~ /^[ \t\r\n]*\{/) exit 0
            exit 1
        }
    '
}

json_list_objects() {
    local json="$1"
    printf '%s\n' "$json" | awk '
        { data = data $0 }
        END {
            pos = index(data, "\"list\"")
            if (!pos) exit
            data = substr(data, pos)
            lb = index(data, "[")
            if (!lb) exit
            data = substr(data, lb + 1)
            depth = 0
            in_string = 0
            esc = 0
            obj = ""
            for (i = 1; i <= length(data); i++) {
                ch = substr(data, i, 1)
                if (depth > 0) obj = obj ch
                if (esc) {
                    esc = 0
                    continue
                }
                if (in_string && ch == "\\") {
                    esc = 1
                    continue
                }
                if (ch == "\"") {
                    in_string = !in_string
                    continue
                }
                if (in_string) continue
                if (ch == "{") {
                    if (depth == 0) obj = "{"
                    depth++
                } else if (ch == "}") {
                    depth--
                    if (depth == 0) {
                        print obj
                        obj = ""
                    }
                } else if (ch == "]" && depth == 0) {
                    exit
                }
            }
        }
    '
}

response_message() {
    local json="$1"
    local key value codes code_key code_value
    for key in msg message ret_msg error error_msg content; do
        value="$(json_value "$key" "$json")"
        if [[ -n "$value" ]]; then
            break
        fi
    done
    for code_key in ret_code code err_code error_code; do
        code_value="$(json_value "$code_key" "$json")"
        if [[ -n "$code_value" ]]; then
            if [[ -n "${codes:-}" ]]; then
                codes="${codes}，${code_key}=${code_value}"
            else
                codes="${code_key}=${code_value}"
            fi
        fi
    done
    if [[ -n "${codes:-}" ]]; then
        if [[ -n "${value:-}" ]]; then
            printf '%s（%s）\n' "$value" "$codes"
        else
            printf '%s\n' "$codes"
        fi
    else
        printf '%s\n' "${value:-}"
    fi
}

request_ok_from_json() {
    local result
    result="$(json_value result "$1" | tr 'A-Z' 'a-z')"
    [[ "$result" == "1" || "$result" == "ok" || "$result" == "true" ]]
}

portal_request() {
    local endpoint="$1"
    shift
    local args pair raw json curl_code
    args=(-k -sS --noproxy "*" --connect-timeout "$REQUEST_TIMEOUT" -m "$REQUEST_TIMEOUT" -G "${API_BASE}${endpoint}")
    args+=(-H "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124 Safari/537.36")
    args+=(-H "Accept: */*")
    args+=(-H "Referer: http://10.1.116.8/")
    for pair in "$@"; do
        args+=(--data-urlencode "$pair")
    done
    args+=(--data-urlencode "callback=$(callback_name)")
    args+=(--data-urlencode "jsVersion=${JS_VERSION}")
    args+=(--data-urlencode "v=$(random_v)")
    args+=(--data-urlencode "lang=zh-cn")

    raw="$(curl "${args[@]}" 2>/dev/null)"
    curl_code=$?
    if [[ $curl_code -ne 0 || -z "$raw" ]]; then
        LAST_JSON=""
        LAST_MSG="请求失败：无法连接认证网关（curl_exit=${curl_code}）"
        LAST_OK=1
        debug_log "请求失败 endpoint=${endpoint} curl_exit=${curl_code}"
        return 1
    fi

    json="$(strip_jsonp "$raw")"
    LAST_JSON="$json"
    LAST_MSG="$(response_message "$json")"
    debug_log "请求完成 endpoint=${endpoint} ok=$(request_ok_from_json "$json" && printf 1 || printf 0) msg=${LAST_MSG:-N/A} raw_prefix=$(printf '%s' "$raw" | cut -c 1-160)"
    if request_ok_from_json "$json"; then
        LAST_OK=0
        return 0
    fi
    LAST_OK=1
    return 1
}

login_with_password() {
    local username="$1"
    local password="$2"
    local operator="$3"
    local account ret_code
    account="$(user_account "$username" "$operator")"
    portal_request "/login" \
        "login_method=1" \
        "user_account=${account}" \
        "user_password=${password}" \
        "wlan_user_ip=${LOCAL_IP}" \
        "wlan_user_ipv6=" \
        "wlan_user_mac=${LOCAL_MAC}" \
        "wlan_ac_ip=" \
        "wlan_ac_name=" \
        "terminal_type=${TERMINAL_TYPE}"
    ret_code="$(json_value ret_code "$LAST_JSON")"
    if [[ "$LAST_OK" == "0" || "$ret_code" == "2" ]]; then
        [[ -n "$LAST_MSG" ]] || LAST_MSG="Portal协议认证成功"
        return 0
    fi
    [[ -n "$LAST_MSG" ]] || LAST_MSG="登录失败"
    return 1
}

send_sms() {
    local phone="$1"
    portal_request "/sms" \
        "telephone=${phone}" \
        "mac=${LOCAL_MAC}" \
        "ip=${LOCAL_IP}" \
        "ipv6=" \
        "bind=0" \
        "page_index=" \
        "prefix=" \
        "sms_type=0"
}

login_with_phone() {
    local phone="$1"
    local code="$2"
    local operator="$3"
    login_with_password "$phone" "$code" "$operator"
}

query_current_online_json() {
    portal_request "/online_list"
}

snapshot_refresh() {
    local obj selected obj_ip obj_owner
    SNAPSHOT_ONLINE=0
    SNAPSHOT_ACCOUNT=""
    SNAPSHOT_IP=""
    SNAPSHOT_MAC=""
    SNAPSHOT_ONLINE_TIME=""
    SNAPSHOT_TIME_LONG="0"
    SNAPSHOT_IS_PERCEIVE=""
    SNAPSHOT_MESSAGE="当前设备未登录"

    query_current_online_json
    if [[ "$LAST_OK" != "0" ]]; then
        case "$LAST_MSG" in
            请求失败*) SNAPSHOT_MESSAGE="当前设备未登录，或当前环境无法访问认证网关" ;;
            "") SNAPSHOT_MESSAGE="当前设备未登录" ;;
            *) SNAPSHOT_MESSAGE="$LAST_MSG" ;;
        esac
        return 1
    fi
    if ! json_list_has_object "$LAST_JSON"; then
        SNAPSHOT_MESSAGE="${LAST_MSG:-当前设备未登录}"
        debug_log "online_list 返回成功但 list 为空或无法识别: $(printf '%s' "$LAST_JSON" | cut -c 1-220)"
        return 1
    fi

    selected=""
    while IFS= read -r obj; do
        [[ -n "$obj" ]] || continue
        [[ -n "$selected" ]] || selected="$obj"
        obj_ip="$(json_value online_ip "$obj")"
        obj_owner="$(json_value is_owner_ip "$obj")"
        if [[ "$obj_ip" == "$LOCAL_IP" || "$obj_owner" == "1" ]]; then
            selected="$obj"
            break
        fi
    done < <(json_list_objects "$LAST_JSON")

    if [[ -z "$selected" ]]; then
        SNAPSHOT_MESSAGE="${LAST_MSG:-当前设备未登录}"
        debug_log "online_list 有 list 但未能解析设备对象: $(printf '%s' "$LAST_JSON" | cut -c 1-220)"
        return 1
    fi

    SNAPSHOT_ONLINE=1
    SNAPSHOT_MESSAGE="当前设备已认证在线"
    SNAPSHOT_ACCOUNT="$(json_value user_account "$selected")"
    SNAPSHOT_IP="$(json_value online_ip "$selected")"
    SNAPSHOT_MAC="$(json_value online_mac "$selected" | tr 'a-f' 'A-F')"
    SNAPSHOT_ONLINE_TIME="$(json_value online_time "$selected")"
    SNAPSHOT_TIME_LONG="$(json_value time_long "$selected")"
    SNAPSHOT_IS_PERCEIVE="$(json_value is_perceive "$selected")"
    [[ -n "$SNAPSHOT_IP" ]] || SNAPSHOT_IP="$LOCAL_IP"
    [[ -n "$SNAPSHOT_MAC" ]] || SNAPSHOT_MAC="$(printf '%s' "$LOCAL_MAC" | tr 'a-f' 'A-F')"
    [[ -n "$SNAPSHOT_TIME_LONG" ]] || SNAPSHOT_TIME_LONG="0"
    return 0
}

confirm_login_state() {
    local first second
    snapshot_refresh
    first="$SNAPSHOT_ONLINE"
    debug_log "状态检测第1次: $([[ "$first" == "1" ]] && printf 已登录 || printf 未登录)"
    sleep "$CLI_STATE_PAIR_INTERVAL"
    snapshot_refresh
    second="$SNAPSHOT_ONLINE"
    debug_log "状态检测第2次: $([[ "$second" == "1" ]] && printf 已登录 || printf 未登录)"

    if [[ "$first" == "1" && "$second" == "1" ]]; then
        printf 'online\n'
        return 0
    fi
    if [[ "$first" == "0" && "$second" == "0" ]]; then
        printf 'offline\n'
        return 0
    fi

    sleep "$CLI_STATE_RETRY_DELAY"
    snapshot_refresh
    first="$SNAPSHOT_ONLINE"
    debug_log "重试状态检测第1次: $([[ "$first" == "1" ]] && printf 已登录 || printf 未登录)"
    sleep "$CLI_STATE_PAIR_INTERVAL"
    snapshot_refresh
    second="$SNAPSHOT_ONLINE"
    debug_log "重试状态检测第2次: $([[ "$second" == "1" ]] && printf 已登录 || printf 未登录)"

    if [[ "$first" == "1" && "$second" == "1" ]]; then
        printf 'online\n'
        return 0
    fi
    if [[ "$first" == "0" && "$second" == "0" ]]; then
        printf 'offline\n'
        return 0
    fi
    printf 'unstable\n'
    return 0
}

ip_to_uint() {
    local ip="$1"
    local a b c d
    IFS=. read -r a b c d <<< "$ip"
    printf '%u\n' "$((a * 256 * 256 * 256 + b * 256 * 256 + c * 256 + d))"
}

load_portal_config() {
    local ip_b64
    if command -v base64 >/dev/null 2>&1; then
        ip_b64="$(printf '%s' "$LOCAL_IP" | base64 | tr -d '\n')"
    else
        ip_b64=""
    fi
    portal_request "/page/loadConfig" \
        "program_index=" \
        "wlan_vlan_id=1" \
        "wlan_user_ip=${ip_b64}" \
        "wlan_user_ipv6=" \
        "wlan_user_ssid=" \
        "wlan_user_areaid=" \
        "wlan_ac_ip=" \
        "wlan_ap_mac=000000000000" \
        "gw_id=000000000000"
}

browser_style_unbind_logout() {
    local ip_uint mac
    ip_uint="$(ip_to_uint "$SNAPSHOT_IP")"
    mac="$(printf '%s' "$SNAPSHOT_MAC" | tr 'a-f' 'A-F')"
    portal_request "/mac/unbind" \
        "user_account=${SNAPSHOT_ACCOUNT}" \
        "wlan_user_mac=${mac}" \
        "wlan_user_ip=${ip_uint}"
}

browser_style_portal_logout() {
    local mac
    mac="$(printf '%s' "$SNAPSHOT_MAC" | tr 'a-f' 'A-F')"
    portal_request "/logout" \
        "login_method=1" \
        "user_account=drcom" \
        "user_password=123" \
        "ac_logout=0" \
        "register_mode=1" \
        "wlan_user_ip=${SNAPSHOT_IP}" \
        "wlan_user_ipv6=" \
        "wlan_vlan_id=1" \
        "wlan_user_mac=${mac}" \
        "wlan_ac_ip=" \
        "wlan_ac_name="
}

submit_browser_style_logout() {
    local config_json un_bind_mac register_mode primary_msg
    load_portal_config
    config_json="$LAST_JSON"
    un_bind_mac="$(json_value un_bind_mac "$config_json")"
    register_mode="$(json_value register_mode "$config_json")"
    [[ -n "$un_bind_mac" ]] || un_bind_mac="1"
    [[ -n "$register_mode" ]] || register_mode="1"

    if [[ "$un_bind_mac" == "1" && ( "$register_mode" == "1" || "$register_mode" == "3" || "$register_mode" == "4" ) ]]; then
        browser_style_unbind_logout
        if [[ "$LAST_OK" == "0" ]]; then
            [[ -n "$LAST_MSG" ]] || LAST_MSG="解绑终端MAC成功！"
            return 0
        fi
        primary_msg="$LAST_MSG"
        browser_style_portal_logout
        if [[ "$LAST_OK" == "0" ]]; then
            [[ -n "$LAST_MSG" ]] || LAST_MSG="Portal注销成功"
            return 0
        fi
        LAST_MSG="${primary_msg}；fallback: ${LAST_MSG}"
        return 1
    fi

    browser_style_portal_logout
    return "$LAST_OK"
}

logout_current_safe() {
    snapshot_refresh
    if [[ "$SNAPSHOT_ONLINE" != "1" ]]; then
        LAST_MSG="当前设备未登录，无法注销"
        return 1
    fi

    submit_browser_style_logout
    if [[ "$LAST_OK" != "0" ]]; then
        return 1
    fi

    local saved_msg="$LAST_MSG"
    local i
    for i in 1 2 3 4 5 6; do
        sleep 0.5
        snapshot_refresh
        if [[ "$SNAPSHOT_ONLINE" != "1" ]]; then
            LAST_MSG="$saved_msg"
            return 0
        fi
    done
    LAST_MSG="${saved_msg}
 ! 3秒内当前设备仍显示在线，可能注销失败，请稍后刷新认证页确认。"
    return 1
}

format_duration() {
    local seconds="${1:-0}"
    [[ "$seconds" =~ ^[0-9]+$ ]] || { printf '%s\n' "$seconds"; return; }
    local days hours minutes secs remainder
    days=$((seconds / 86400))
    remainder=$((seconds % 86400))
    hours=$((remainder / 3600))
    remainder=$((remainder % 3600))
    minutes=$((remainder / 60))
    secs=$((remainder % 60))
    if ((days > 0)); then
        printf '%d天%d时%02d分%02d秒\n' "$days" "$hours" "$minutes" "$secs"
    elif ((hours > 0)); then
        printf '%d时%02d分%02d秒\n' "$hours" "$minutes" "$secs"
    elif ((minutes > 0)); then
        printf '%d分%02d秒\n' "$minutes" "$secs"
    else
        printf '%d秒\n' "$secs"
    fi
}

test_external_network() {
    local host
    if ! command -v ping >/dev/null 2>&1; then
        printf '未找到 ping 命令\n'
        return 1
    fi
    for host in "${PING_HOSTS[@]}"; do
        if ping -c 1 -W 2 "$host" >/dev/null 2>&1 || ping -c 1 -w 3 "$host" >/dev/null 2>&1; then
            printf '%s\n' "$host"
            return 0
        fi
    done
    printf '外网测试未通过\n'
    return 1
}

auth_method_label() {
    local account="$1"
    local is_perceive="$2"
    local method="账号密码登录"
    case "$account" in
        1??????????)
            case "$account" in
                *[!0-9]*) method="账号密码登录" ;;
                *) method="手机验证码登录" ;;
            esac
            ;;
    esac
    if [[ "$is_perceive" == "1" ]]; then
        method="${method} / 感知上线"
    fi
    printf '%s\n' "$method"
}

format_dashboard_row() {
    local label1="$1"
    local value1="$2"
    local color1="$3"
    local label2="$4"
    local value2="$5"
    local color2="$6"
    local width padding
    width="$(visible_width "$value1")"
    padding=$((24 - width))
    [ "$padding" -lt 1 ] && padding=1
    printf ' %s[*]%s %s%s%s %s::%s %s%s%s%s%s[*]%s %s%s%s %s::%s %s%s%s\n' \
        "$C_BM" "$C_RST" "$C_W" "$label1" "$C_RST" "$C_DG" "$C_RST" \
        "$color1" "$value1" "$C_RST" "$(repeat_char ' ' "$padding")" \
        "$C_BM" "$C_RST" "$C_W" "$label2" "$C_RST" "$C_DG" "$C_RST" \
        "$color2" "$value2" "$C_RST"
}

make_tmp_base() {
    local base
    base="$(mktemp "${TMPDIR:-/tmp}/zjnu_auth.XXXXXX" 2>/dev/null)"
    if [[ -z "$base" ]]; then
        base="${TMPDIR:-/tmp}/zjnu_auth.$$.$RANDOM"
        : > "$base"
    fi
    printf '%s\n' "$base"
}

sleep_short() {
    sleep "$1" 2>/dev/null || sleep 1
}

run_with_spinner() {
    local message="$1"
    shift
    local base out msg code err pid frame index clear_len
    base="$(make_tmp_base)"
    out="${base}.out"
    msg="${base}.msg"
    code="${base}.code"
    err="${base}.err"
    rm -f "$base"

    (
        "$@" > "$out" 2> "$err"
        printf '%s\n' "$?" > "$code"
        printf '%s\n' "$LAST_MSG" > "$msg"
    ) &
    pid=$!

    index=0
    while kill -0 "$pid" 2>/dev/null; do
        case $((index % 4)) in
            0) frame='|' ;;
            1) frame='/' ;;
            2) frame='-' ;;
            *) frame="\\" ;;
        esac
        printf '\r %s%s%s %s...' "$C_BY" "$frame" "$C_RST" "$message"
        index=$((index + 1))
        sleep_short 0.12
    done
    wait "$pid" 2>/dev/null

    clear_len=$(( $(visible_width "$message") + 12 ))
    printf '\r%s\r' "$(repeat_char ' ' "$clear_len")"

    RUN_CODE="$(cat "$code" 2>/dev/null)"
    RUN_OUTPUT="$(cat "$out" 2>/dev/null)"
    RUN_MSG="$(cat "$msg" 2>/dev/null)"
    [ -n "$RUN_CODE" ] || RUN_CODE=1
    LAST_MSG="$RUN_MSG"
    rm -f "$out" "$msg" "$code" "$err"
}

objects_from_json() {
    json_list_objects "$1" | grep -E 'online_ip|online_mac|user_account' || true
}

print_merged_devices() {
    local account="$1"
    local json1 json2 obj ip mac dev_account login_time time_long key count current seen_keys duration
    local snapshot_key
    local base device_file
    seen_keys=""
    count=0
    base="$(make_tmp_base)"
    device_file="${base}.devices"
    rm -f "$base"
    : > "$device_file"

    portal_request "/mac/find" "user_account=${account}"
    json1="$LAST_JSON"
    portal_request "/online_list" "user_account=${account}"
    json2="$LAST_JSON"

    while IFS= read -r obj; do
        ip="$(json_value online_ip "$obj")"
        mac="$(json_value online_mac "$obj" | tr 'a-f' 'A-F')"
        dev_account="$(json_value user_account "$obj")"
        login_time="$(json_value online_time "$obj")"
        time_long="$(json_value time_long "$obj")"
        [[ -n "$ip" || -n "$mac" ]] || continue
        if [[ -n "$account" && -n "$dev_account" && "$dev_account" != "$account" ]]; then
            continue
        fi
        key="${ip}_${mac}"
        if printf '%s\n' "$seen_keys" | grep -Fxq "$key"; then
            continue
        fi
        seen_keys="${seen_keys}
${key}"
        count=$((count + 1))
        current=""
        if [[ "$ip" == "$LOCAL_IP" ]]; then
            current=" ${C_BG}[当前]${C_RST}"
        fi
        duration="$(format_duration "${time_long:-0}")"
        {
            printf '  %s[%d]%s%s %s%s%s / %s%s%s\n' \
                "$C_BB" "$count" "$C_RST" "$current" "$C_BY" "${ip:-$LOCAL_IP}" "$C_RST" "$C_BY" "$mac" "$C_RST"
            printf '      账号: %s%s%s | 登录: %s%s%s | 在线: %s%s%s\n' \
                "$C_BW" "${dev_account:-N/A}" "$C_RST" "$C_BY" "${login_time:-N/A}" "$C_RST" "$C_BY" "$duration" "$C_RST"
        } >> "$device_file"
    done < <(printf '%s\n%s\n' "$(objects_from_json "$json1")" "$(objects_from_json "$json2")")

    if [[ "$SNAPSHOT_ONLINE" == "1" ]]; then
        snapshot_key="${SNAPSHOT_IP}_${SNAPSHOT_MAC}"
        if ! printf '%s\n' "$seen_keys" | grep -Fxq "$snapshot_key"; then
            count=$((count + 1))
            duration="$(format_duration "${SNAPSHOT_TIME_LONG:-0}")"
            {
            printf '  %s[%d]%s %s[当前]%s %s%s%s / %s%s%s\n' \
                "$C_BB" "$count" "$C_RST" "$C_BG" "$C_RST" "$C_BY" "${SNAPSHOT_IP:-$LOCAL_IP}" "$C_RST" "$C_BY" "$SNAPSHOT_MAC" "$C_RST"
            printf '      账号: %s%s%s | 登录: %s%s%s | 在线: %s%s%s\n' \
                "$C_BW" "${SNAPSHOT_ACCOUNT:-$account}" "$C_RST" "$C_BY" "${SNAPSHOT_ONLINE_TIME:-N/A}" "$C_RST" "$C_BY" "$duration" "$C_RST"
            } >> "$device_file"
        fi
    fi

    if ((count == 0)); then
        printf '%s没有获取到在线设备列表%s\n' "$C_DG" "$C_RST"
        rm -f "$device_file"
        return
    fi
    printf '\n%s=== 在线设备：%d 台 ===%s\n' "$C_BC" "$count" "$C_RST"
    cat "$device_file"
    rm -f "$device_file"
}

select_operator() {
    local value
    printf '\n %s*%s 请选择运营商:\n' "$C_BM" "$C_RST"
    printf '   %s[1]%s %s校园用户%s %s(默认)%s\n' "$C_BG" "$C_RST" "$C_W" "$C_RST" "$C_DG" "$C_RST"
    printf '   %s[2]%s %s校园电信%s\n' "$C_BG" "$C_RST" "$C_W" "$C_RST"
    printf '   %s[3]%s %s校园联通%s\n' "$C_BG" "$C_RST" "$C_W" "$C_RST"
    printf '   %s[4]%s %s校园移动%s\n' "$C_BG" "$C_RST" "$C_W" "$C_RST"
    while true; do
        printf ' %s➤%s 请输入运营商编号[%s%s%s]: ' "$C_BM" "$C_RST" "$C_DG" "$DEFAULT_OPERATOR" "$C_RST"
        IFS= read -r value
        value="${value:-$DEFAULT_OPERATOR}"
        case "$value" in
            1|2|3|4) OPERATOR_CHOICE="$value"; return ;;
            *) printf ' %s✗ 无效的运营商编号%s\n' "$C_R" "$C_RST" ;;
        esac
    done
}

password_read() {
    local prompt="$1"
    local value
    printf '%s' "$prompt" >&2
    stty -echo 2>/dev/null || true
    IFS= read -r value
    stty echo 2>/dev/null || true
    printf '\n' >&2
    printf '%s\n' "$value"
}

interactive_password_login() {
    local username password operator net_msg account
    printf '\n%s=== 账号密码登录 ===%s\n' "$C_BC" "$C_RST"
    printf ' %s*%s 请输入账号: ' "$C_BM" "$C_RST"
    IFS= read -r username
    username="$(printf '%s' "$username" | sed "$SED_ERE" 's/^[[:space:]]+|[[:space:]]+$//g')"
    [[ -n "$username" ]] || { printf '%s✗ 账号不能为空%s\n' "$C_R" "$C_RST"; return; }
    password="$(password_read " ${C_BM}*${C_RST} 请输入密码: ")"
    password="$(printf '%s' "$password" | sed "$SED_ERE" 's/^[[:space:]]+|[[:space:]]+$//g')"
    [[ -n "$password" ]] || { printf '%s✗ 密码不能为空%s\n' "$C_R" "$C_RST"; return; }
    select_operator
    operator="$OPERATOR_CHOICE"

    if login_with_password "$username" "$password" "$operator"; then
        printf '\n%s✓%s %s%s%s\n' "$C_BG" "$C_RST" "$C_W" "$LAST_MSG" "$C_RST"
    else
        printf '\n%s✗%s %s%s%s\n' "$C_R" "$C_RST" "$C_W" "$LAST_MSG" "$C_RST"
        return
    fi

    run_with_spinner "正在测试外网连通" test_external_network
    net_msg="$RUN_OUTPUT"
    if [[ "$RUN_CODE" == "0" ]]; then
        printf '%s✓ 外网连通测试通过: %s%s\n' "$C_BG" "$net_msg" "$C_RST"
    else
        printf '%s! 外网连通测试未通过: %s%s\n' "$C_BY" "$net_msg" "$C_RST"
    fi

    sleep 1
    snapshot_refresh
    account="${SNAPSHOT_ACCOUNT:-$username}"
    print_merged_devices "$account"
}

interactive_phone_login() {
    local phone code operator net_msg account symbol
    printf '\n%s=== 手机验证码登录 ===%s\n' "$C_BC" "$C_RST"
    printf ' %s*%s 请输入手机号: ' "$C_BM" "$C_RST"
    IFS= read -r phone
    phone="$(printf '%s' "$phone" | sed "$SED_ERE" 's/^[[:space:]]+|[[:space:]]+$//g')"
    [[ -n "$phone" ]] || { printf '%s✗ 手机号不能为空%s\n' "$C_R" "$C_RST"; return; }
    run_with_spinner "正在发送短信验证码" send_sms "$phone"
    if [[ "$RUN_CODE" == "0" ]]; then
        symbol="${C_BG}✓"
    else
        symbol="${C_R}✗"
    fi
    printf ' %s %s验证码正在下发，稍有延迟，请用户注意查收！%s\n' "$symbol" "$C_W" "$C_RST"
    if [[ "$RUN_CODE" == "0" ]]; then
        :
    else
        printf ' ! %s请检查手机号码是否正确或者重复发送验证码，请退出重新登录...%s\n' "$C_BY" "$C_RST"
        return
    fi
    printf ' %s*%s 请输入短信验证码: ' "$C_BM" "$C_RST"
    IFS= read -r code
    code="$(printf '%s' "$code" | sed "$SED_ERE" 's/^[[:space:]]+|[[:space:]]+$//g')"
    if [[ -z "$code" ]]; then
        printf ' %s✗ 验证码不能为空，请重新输入一次%s\n' "$C_R" "$C_RST"
        printf ' %s*%s 请输入短信验证码: ' "$C_BM" "$C_RST"
        IFS= read -r code
        code="$(printf '%s' "$code" | sed "$SED_ERE" 's/^[[:space:]]+|[[:space:]]+$//g')"
        [[ -n "$code" ]] || { printf ' %s✗ 验证码不能为空%s\n' "$C_R" "$C_RST"; return; }
    fi
    select_operator
    operator="$OPERATOR_CHOICE"
    if login_with_phone "$phone" "$code" "$operator"; then
        printf '\n%s✓%s %s%s%s\n' "$C_BG" "$C_RST" "$C_W" "$LAST_MSG" "$C_RST"
    else
        printf '\n%s✗%s %s%s%s\n' "$C_R" "$C_RST" "$C_W" "$LAST_MSG" "$C_RST"
        printf ' %s*%s 登录失败，请重新输入短信验证码（直接回车取消）: ' "$C_BM" "$C_RST"
        IFS= read -r code
        code="$(printf '%s' "$code" | sed "$SED_ERE" 's/^[[:space:]]+|[[:space:]]+$//g')"
        [[ -n "$code" ]] || return
        if login_with_phone "$phone" "$code" "$operator"; then
            printf '\n%s✓%s %s%s%s\n' "$C_BG" "$C_RST" "$C_W" "$LAST_MSG" "$C_RST"
        else
            printf '\n%s✗%s %s%s%s\n' "$C_R" "$C_RST" "$C_W" "$LAST_MSG" "$C_RST"
            return
        fi
    fi

    run_with_spinner "正在测试外网连通" test_external_network
    net_msg="$RUN_OUTPUT"
    if [[ "$RUN_CODE" == "0" ]]; then
        printf '%s✓ 外网连通测试通过: %s%s\n' "$C_BG" "$net_msg" "$C_RST"
    else
        printf '%s! 外网连通测试未通过: %s%s\n' "$C_BY" "$net_msg" "$C_RST"
    fi

    sleep 1
    snapshot_refresh
    account="${SNAPSHOT_ACCOUNT:-$phone}"
    if [[ "$SNAPSHOT_ONLINE" == "1" ]]; then
        print_merged_devices "$account"
    else
        printf '%s! 登录接口返回成功，但暂未查询到当前在线详情，请稍后刷新。%s\n' "$C_BY" "$C_RST"
    fi
}

interactive_logout() {
    local confirm
    printf '\n%s=== 注销当前设备 ===%s\n' "$C_BC" "$C_RST"
    printf ' %s[+]%s 当前IP: %s%s%s\n' "$C_BB" "$C_RST" "$C_BY" "$LOCAL_IP" "$C_RST"
    printf ' %s[+]%s 当前MAC: %s%s%s\n' "$C_BB" "$C_RST" "$C_BY" "$(upper_ascii "$LOCAL_MAC")" "$C_RST"
    if [[ -n "${SSH_CONNECTION:-}" ]]; then
        printf ' %s！注销认证本身可能让远程连接短暂中断。%s\n' "$C_BY" "$C_RST"
    fi
    printf '\n %s➤%s 确认注销当前设备吗? [%sy/N%s]: ' "$C_BM" "$C_RST" "$C_DG" "$C_RST"
    IFS= read -r confirm
    confirm="$(lower_ascii "$(printf '%s' "$confirm" | sed "$SED_ERE" 's/^[[:space:]]+|[[:space:]]+$//g')")"
    [[ "$confirm" == "y" ]] || { printf '%s已取消%s\n' "$C_DG" "$C_RST"; return; }
    run_with_spinner "正在注销当前设备" logout_current_safe
    if [[ "$RUN_CODE" == "0" ]]; then
        printf '%s ✓ %s%s\n' "$C_BG" "$LAST_MSG" "$C_RST"
    else
        printf '%s ! %s%s\n' "$C_BY" "$LAST_MSG" "$C_RST"
    fi
}

print_dashboard() {
    snapshot_refresh
    clear 2>/dev/null || true
    local now_str header method
    now_str="$(date '+%Y-%m-%d %H:%M')"
    header="///  ZJNU Campus Network Auth  ///    [ ${now_str} ]"
    printf '%s%s%s\n' "$C_DG" "$(separator '=')" "$C_RST"
    printf '%s%s%s\n' "$C_BM" "$(center_text "$header")" "$C_RST"
    printf '%s%s%s\n' "$C_BC" "$(center_text '浙江师范大学校园网认证工具')" "$C_RST"
    printf '%s%s%s\n' "$C_DG" "$(separator '=')" "$C_RST"
    printf '\n'
    printf ' %s[+]%s %s认证网关%s %s::%s %s%s%s\n' "$C_BB" "$C_RST" "$C_W" "$C_RST" "$C_DG" "$C_RST" "$C_BC" "$(portal_host)" "$C_RST"
    printf ' %s[+]%s %s本机设备%s %s::%s %s%s%s %s/%s %s%s%s\n' \
        "$C_BB" "$C_RST" "$C_W" "$C_RST" "$C_DG" "$C_RST" "$C_BY" "$LOCAL_IP" "$C_RST" \
        "$C_DG" "$C_RST" "$C_BY" "$(upper_ascii "$LOCAL_MAC")" "$C_RST"
    printf '%s%s%s\n' "$C_DG" "$(separator '-')" "$C_RST"
    if [[ "$SNAPSHOT_ONLINE" == "1" ]]; then
        printf ' %s✓ 当前设备已认证在线%s\n' "$C_BG" "$C_RST"
        method="$(auth_method_label "${SNAPSHOT_ACCOUNT:-}" "$SNAPSHOT_IS_PERCEIVE")"
        format_dashboard_row "用户账号" "${SNAPSHOT_ACCOUNT:-N/A}" "$C_BW" "认证方式" "$method" "$C_BW"
        format_dashboard_row "IP地址  " "${SNAPSHOT_IP:-$LOCAL_IP}" "$C_BY" "MAC地址 " "${SNAPSHOT_MAC:-$(upper_ascii "$LOCAL_MAC")}" "$C_BY"
        format_dashboard_row "登录时间" "${SNAPSHOT_ONLINE_TIME:-N/A}" "$C_BY" "在线时长" "$(format_duration "${SNAPSHOT_TIME_LONG:-0}")" "$C_BY"
    else
        printf ' %s✗ 当前设备未登录认证%s\n' "$C_R" "$C_RST"
        printf ' %s%s%s\n' "$C_DG" "${SNAPSHOT_MESSAGE:-请先使用账号密码或手机验证码登录。}" "$C_RST"
    fi
    printf '%s%s%s\n' "$C_DG" "$(separator '-')" "$C_RST"
    if [[ "$SNAPSHOT_ONLINE" == "1" ]]; then
        printf '  %s[1] 账号密码登录%s\n' "$C_DG" "$C_RST"
        printf '  %s[2] 手机验证码登录%s\n' "$C_DG" "$C_RST"
        printf '  %s[3]%s %s注销当前设备%s\n' "$C_BG" "$C_RST" "$C_W" "$C_RST"
    else
        printf '  %s[1]%s %s账号密码登录%s\n' "$C_BG" "$C_RST" "$C_W" "$C_RST"
        printf '  %s[2]%s %s手机验证码登录%s\n' "$C_BG" "$C_RST" "$C_W" "$C_RST"
        printf '  %s[3] 注销当前设备%s\n' "$C_DG" "$C_RST"
    fi
    printf '  %s[0]%s %s退出工具%s\n' "$C_BG" "$C_RST" "$C_W" "$C_RST"
    printf '\n'
}

interactive_mode() {
    local choice
    while true; do
        print_dashboard
        printf '%s➤%s %s请输入操作编号[0-3]:%s ' "$C_BG" "$C_RST" "$C_BW" "$C_RST"
        IFS= read -r choice
        case "$choice" in
            1)
                if [[ "$SNAPSHOT_ONLINE" == "1" ]]; then
                    printf '\n%s✗ 当前设备已登录，请先注销当前设备后再重新登录。%s\n' "$C_R" "$C_RST"
                else
                    interactive_password_login
                fi
                ;;
            2)
                if [[ "$SNAPSHOT_ONLINE" == "1" ]]; then
                    printf '\n%s✗ 当前设备已登录，请先注销当前设备后再重新登录。%s\n' "$C_R" "$C_RST"
                else
                    interactive_phone_login
                fi
                ;;
            3)
                if [[ "$SNAPSHOT_ONLINE" != "1" ]]; then
                    printf '\n%s✗ 当前设备未登录，无法使用注销功能。%s\n' "$C_R" "$C_RST"
                else
                    interactive_logout
                fi
                ;;
            0)
                printf '\n%s再见！%s\n' "$C_BC" "$C_RST"
                return 0
                ;;
            *)
                printf '\n%s✗ 无效选择%s\n' "$C_R" "$C_RST"
                ;;
        esac
        printf '\n%s按回车继续...%s' "$C_DG" "$C_RST"
        IFS= read -r _
    done
}

show_help() {
    cat <<'EOF'
浙江师范大学校园网认证工具 Bash 版

用法:
  ./zjnu_auth_lit.sh
  ./zjnu_auth_lit.sh [--debug] login <账号> <密码> [-o 运营商]
  ./zjnu_auth_lit.sh [--debug] login -u <账号> -p <密码> [-o 运营商]
  ./zjnu_auth_lit.sh [--debug] logout
  ./zjnu_auth_lit.sh [--debug] status

运营商:
  1 校园用户
  2 校园电信
  3 校园联通
  4 校园移动
EOF
}

cli_login() {
    local username="" password="" operator="$DEFAULT_OPERATOR" state
    local positional=()
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -u|--username) username="${2:-}"; shift 2 ;;
            -p|--password) password="${2:-}"; shift 2 ;;
            -o|--operator) operator="$(normalize_operator "${2:-}")"; shift 2 ;;
            *) positional+=("$1"); shift ;;
        esac
    done
    [[ -n "$username" ]] || username="${positional[0]:-}"
    [[ -n "$password" ]] || password="${positional[1]:-}"
    if [[ -z "$username" || -z "$password" ]]; then
        return 1
    fi
    state="$(confirm_login_state)"
    case "$state" in
        online) return 0 ;;
        unstable) return 3 ;;
    esac
    login_with_password "$username" "$password" "$operator" || return 1
    state="$(confirm_login_state)"
    case "$state" in
        online) return 0 ;;
        unstable) return 3 ;;
        *) return 1 ;;
    esac
}

cli_logout() {
    local state i
    state="$(confirm_login_state)"
    case "$state" in
        offline) return 0 ;;
        unstable) return 3 ;;
    esac
    snapshot_refresh || return 0
    submit_browser_style_logout || return 2
    for ((i = 1; i <= CLI_LOGOUT_CHECKS; i++)); do
        snapshot_refresh
        [[ "$SNAPSHOT_ONLINE" != "1" ]] && return 0
        sleep "$CLI_LOGOUT_INTERVAL"
    done
    return 4
}

cli_status() {
    local state
    state="$(confirm_login_state)"
    case "$state" in
        offline) return 1 ;;
        unstable) return 3 ;;
    esac
    snapshot_refresh || return 1
    [[ -n "$SNAPSHOT_ACCOUNT" ]] || return 1
    printf '%s\n' "$SNAPSHOT_ACCOUNT"
    return 0
}

main() {
    init_device
    if [[ "${1:-}" == "--debug" ]]; then
        CLI_DEBUG=1
        shift
    fi
    case "${1:-}" in
        "")
            interactive_mode
            ;;
        -h|--help|help)
            show_help
            ;;
        login)
            shift
            cli_login "$@"
            ;;
        logout)
            shift
            cli_logout "$@"
            ;;
        status)
            shift
            cli_status "$@"
            ;;
        *)
            show_help
            return 1
            ;;
    esac
}

if [[ "${ZJNU_AUTH_SH_NO_MAIN:-0}" != "1" ]]; then
    main "$@"
fi
