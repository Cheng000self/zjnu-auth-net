#include <arpa/inet.h>
#ifndef ZJNU_AUTH_NO_CURL
#include <curl/curl.h>
#endif
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

// Portal 协议常量，与 Python 版保持一致。
const std::string JS_VERSION = "4.2.1";
const std::string TERMINAL_TYPE = "1";
const std::string DEFAULT_API_BASE = "http://10.1.116.8:801/eportal/portal";
const std::string DEFAULT_OPERATOR = "1";
const std::vector<std::string> PING_HOSTS = {"114.114.114.114", "223.5.5.5", "www.baidu.com"};
const double CLI_STATE_PAIR_INTERVAL = 0.5;
const double CLI_STATE_RETRY_DELAY = 1.0;
const int CLI_LOGOUT_CHECKS = 4;
const int CLI_LOGOUT_INTERVAL = 1;
const int UI_WIDTH = 74;

namespace C {
const std::string RST = "\033[0m";
const std::string DG = "\033[90m";
const std::string W = "\033[37m";
const std::string BW = "\033[97m";
const std::string BC = "\033[96m";
const std::string BB = "\033[94m";
const std::string BM = "\033[95m";
const std::string BG = "\033[92m";
const std::string BY = "\033[93m";
const std::string R = "\033[31m";
}  // namespace C

bool g_debug = false;

struct RequestResult {
    bool ok = false;
    std::string message;
    std::string json;
};

struct Device {
    std::string user_account;
    std::string online_ip;
    std::string online_mac;
    std::string online_time;
    std::string time_long;
    std::string is_owner_ip;
    std::string is_perceive;
};

struct AuthSnapshot {
    bool online = false;
    std::string message;
    std::string account;
    Device current_device;
    std::vector<Device> devices;
};

void debug_log(const std::string& message) {
    if (g_debug) {
        std::cerr << "[DEBUG] " << message << '\n';
    }
}

std::string trim(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string lower_copy(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string upper_copy(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::vector<uint32_t> decode_utf8(const std::string& value) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < value.size();) {
        unsigned char ch = static_cast<unsigned char>(value[i]);
        if (ch < 0x80) {
            out.push_back(ch);
            ++i;
        } else if ((ch & 0xE0) == 0xC0 && i + 1 < value.size()) {
            out.push_back(((ch & 0x1F) << 6U) | (static_cast<unsigned char>(value[i + 1]) & 0x3FU));
            i += 2;
        } else if ((ch & 0xF0) == 0xE0 && i + 2 < value.size()) {
            out.push_back(((ch & 0x0F) << 12U) |
                          ((static_cast<unsigned char>(value[i + 1]) & 0x3FU) << 6U) |
                          (static_cast<unsigned char>(value[i + 2]) & 0x3FU));
            i += 3;
        } else if ((ch & 0xF8) == 0xF0 && i + 3 < value.size()) {
            out.push_back(((ch & 0x07) << 18U) |
                          ((static_cast<unsigned char>(value[i + 1]) & 0x3FU) << 12U) |
                          ((static_cast<unsigned char>(value[i + 2]) & 0x3FU) << 6U) |
                          (static_cast<unsigned char>(value[i + 3]) & 0x3FU));
            i += 4;
        } else {
            out.push_back(ch);
            ++i;
        }
    }
    return out;
}

bool is_wide_codepoint(uint32_t cp) {
    return (cp >= 0x1100 && cp <= 0x115F) ||
           cp == 0x2329 || cp == 0x232A ||
           (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE10 && cp <= 0xFE19) ||
           (cp >= 0xFE30 && cp <= 0xFE6F) ||
           (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6);
}

int visible_width(const std::string& value) {
    int width = 0;
    for (uint32_t cp : decode_utf8(value)) {
        width += is_wide_codepoint(cp) ? 2 : 1;
    }
    return width;
}

std::string repeat_char(char ch, int count) {
    if (count <= 0) {
        return "";
    }
    return std::string(static_cast<size_t>(count), ch);
}

std::string center_text(const std::string& value, int width = UI_WIDTH) {
    int padding = width - visible_width(value);
    if (padding < 0) {
        padding = 0;
    }
    int left = padding / 2;
    int right = padding - left;
    return repeat_char(' ', left) + value + repeat_char(' ', right);
}

std::string separator(char ch = '=') {
    return repeat_char(ch, UI_WIDTH);
}

std::string current_time_minute() {
    std::time_t now = std::time(nullptr);
    std::tm tm_value{};
    localtime_r(&now, &tm_value);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm_value);
    return buffer;
}

std::string format_dashboard_row(const std::string& label1,
                                 const std::string& value1,
                                 const std::string& color1,
                                 const std::string& label2,
                                 const std::string& value2,
                                 const std::string& color2) {
    int padding = 24 - visible_width(value1);
    if (padding < 1) {
        padding = 1;
    }
    return " " + C::BM + "[*]" + C::RST + " " + C::W + label1 + C::RST + " " + C::DG + "::" +
           C::RST + " " + color1 + value1 + C::RST + repeat_char(' ', padding) +
           C::BM + "[*]" + C::RST + " " + C::W + label2 + C::RST + " " + C::DG + "::" +
           C::RST + " " + color2 + value2 + C::RST;
}

std::string now_mod_100000() {
    using namespace std::chrono;
    auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    return std::to_string(ms % 100000);
}

std::string callback_name() {
    return "dr" + now_mod_100000();
}

std::string normalize_mac(std::string mac) {
    std::string out;
    for (char ch : mac) {
        if (ch != ':' && ch != '-' && !std::isspace(static_cast<unsigned char>(ch))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return out;
}

std::string portal_host_from_base(const std::string& api_base) {
    std::regex pattern(R"(^[a-zA-Z]+://([^/:]+))");
    std::smatch match;
    if (std::regex_search(api_base, match, pattern)) {
        return match[1].str();
    }
    return "10.1.116.8";
}

#ifdef ZJNU_AUTH_NO_CURL
int portal_port_from_base(const std::string& api_base) {
    size_t scheme = api_base.find("://");
    size_t begin = (scheme == std::string::npos) ? 0 : scheme + 3;
    size_t slash = api_base.find('/', begin);
    size_t colon = api_base.find(':', begin);
    if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
        try {
            return std::stoi(api_base.substr(colon + 1, slash == std::string::npos ? std::string::npos : slash - colon - 1));
        } catch (...) {
            return 80;
        }
    }
    return starts_with(lower_copy(api_base), "https://") ? 443 : 80;
}

std::string portal_path_from_base(const std::string& api_base) {
    size_t scheme = api_base.find("://");
    size_t begin = (scheme == std::string::npos) ? 0 : scheme + 3;
    size_t slash = api_base.find('/', begin);
    if (slash == std::string::npos) {
        return "";
    }
    std::string path = api_base.substr(slash);
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

bool api_base_is_https(const std::string& api_base) {
    return starts_with(lower_copy(api_base), "https://");
}
#endif

std::string read_file_trimmed(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return "";
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return trim(buffer.str());
}

std::string get_default_interface() {
    std::ifstream in("/proc/net/route");
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string iface;
        std::string destination;
        if (iss >> iface >> destination && destination == "00000000") {
            return iface;
        }
    }
    return "";
}

std::string get_local_ip(const std::string& server_host) {
    sockaddr_in direct_addr{};
    direct_addr.sin_family = AF_INET;
    direct_addr.sin_port = htons(801);
    if (inet_pton(AF_INET, server_host.c_str(), &direct_addr.sin_addr) == 1) {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock >= 0) {
            if (connect(sock, reinterpret_cast<sockaddr*>(&direct_addr), sizeof(direct_addr)) == 0) {
                sockaddr_in local_addr{};
                socklen_t addr_len = sizeof(local_addr);
                if (getsockname(sock, reinterpret_cast<sockaddr*>(&local_addr), &addr_len) == 0) {
                    char buffer[INET_ADDRSTRLEN] = {0};
                    if (inet_ntop(AF_INET, &local_addr.sin_addr, buffer, sizeof(buffer)) != nullptr) {
                        close(sock);
                        return buffer;
                    }
                }
            }
            close(sock);
        }
    }

#ifndef ZJNU_AUTH_NO_CURL
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(server_host.c_str(), "801", &hints, &result) == 0 && result != nullptr) {
        int sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (sock >= 0) {
            if (connect(sock, result->ai_addr, result->ai_addrlen) == 0) {
                sockaddr_in local_addr{};
                socklen_t addr_len = sizeof(local_addr);
                if (getsockname(sock, reinterpret_cast<sockaddr*>(&local_addr), &addr_len) == 0) {
                    char buffer[INET_ADDRSTRLEN] = {0};
                    if (inet_ntop(AF_INET, &local_addr.sin_addr, buffer, sizeof(buffer)) != nullptr) {
                        close(sock);
                        freeaddrinfo(result);
                        return buffer;
                    }
                }
            }
            close(sock);
        }
        freeaddrinfo(result);
    }
#endif

    // fallback：没有 Python 时仍尽量用系统 ip 命令查全局 IPv4。
    FILE* pipe = popen("ip -4 -o addr show scope global 2>/dev/null", "r");
    if (pipe != nullptr) {
        char line[512];
        std::regex pattern(R"(\binet\s+([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)/)");
        std::smatch match;
        while (fgets(line, sizeof(line), pipe) != nullptr) {
            std::string text(line);
            if (std::regex_search(text, match, pattern)) {
                std::string ip = match[1].str();
                if (!starts_with(ip, "127.") && !starts_with(ip, "172.17.")) {
                    pclose(pipe);
                    return ip;
                }
            }
        }
        pclose(pipe);
    }
    return "0.0.0.0";
}

std::string get_mac_address() {
    std::string iface = get_default_interface();
    if (!iface.empty()) {
        std::string mac = normalize_mac(read_file_trimmed("/sys/class/net/" + iface + "/address"));
        if (!mac.empty() && mac != "000000000000") {
            return mac;
        }
    }

    DIR* dir = opendir("/sys/class/net");
    if (dir == nullptr) {
        return "000000000000";
    }
    std::vector<std::string> ifaces;
    while (dirent* entry = readdir(dir)) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        ifaces.push_back(name);
    }
    closedir(dir);

    for (const std::string& name : ifaces) {
        if (name == "lo" || name == "docker0" || starts_with(name, "virbr") || starts_with(name, "veth")) {
            continue;
        }
        std::string mac = normalize_mac(read_file_trimmed("/sys/class/net/" + name + "/address"));
        if (!mac.empty() && mac != "000000000000") {
            return mac;
        }
    }
    return "000000000000";
}

std::string format_duration(const std::string& seconds_text) {
    long long seconds = 0;
    try {
        seconds = std::stoll(seconds_text.empty() ? "0" : seconds_text);
    } catch (...) {
        return seconds_text.empty() ? "0秒" : seconds_text;
    }
    long long days = seconds / 86400;
    long long remainder = seconds % 86400;
    long long hours = remainder / 3600;
    remainder %= 3600;
    long long minutes = remainder / 60;
    long long secs = remainder % 60;

    char buffer[128];
    if (days > 0) {
        std::snprintf(buffer, sizeof(buffer), "%lld天%lld时%02lld分%02lld秒", days, hours, minutes, secs);
    } else if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%lld时%02lld分%02lld秒", hours, minutes, secs);
    } else if (minutes > 0) {
        std::snprintf(buffer, sizeof(buffer), "%lld分%02lld秒", minutes, secs);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lld秒", secs);
    }
    return buffer;
}

uint32_t ip_to_uint(const std::string& ip) {
    unsigned int a = 0;
    unsigned int b = 0;
    unsigned int c = 0;
    unsigned int d = 0;
    std::sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d);
    return (a << 24U) + (b << 16U) + (c << 8U) + d;
}

std::string base64_encode(const std::string& input) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    int val = 0;
    int valb = -6;
    for (unsigned char ch : input) {
        val = (val << 8) + ch;
        valb += 8;
        while (valb >= 0) {
            output.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        output.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (output.size() % 4) {
        output.push_back('=');
    }
    return output;
}

std::string strip_jsonp(std::string text) {
    text = trim(text);
    if (!text.empty() && text.front() == '{') {
        return text;
    }
    size_t left = text.find('(');
    size_t right = text.rfind(')');
    if (left != std::string::npos && right != std::string::npos && right > left) {
        return text.substr(left + 1, right - left - 1);
    }
    return text;
}

std::optional<std::string> json_value(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size()) {
        return std::nullopt;
    }
    if (json[pos] == '"') {
        ++pos;
        std::string value;
        bool escaped = false;
        for (; pos < json.size(); ++pos) {
            char ch = json[pos];
            if (escaped) {
                value.push_back(ch);
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                break;
            } else {
                value.push_back(ch);
            }
        }
        return value;
    }
    size_t end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']') {
        ++end;
    }
    return trim(json.substr(pos, end - pos));
}

std::string json_value_or_empty(const std::string& json, const std::string& key) {
    auto value = json_value(json, key);
    return value ? *value : "";
}

std::string response_message(const std::string& json, const std::string& fallback = "") {
    std::string message = fallback;
    for (const char* key : {"msg", "message", "ret_msg", "error", "error_msg", "content"}) {
        std::string value = json_value_or_empty(json, key);
        if (!value.empty()) {
            message = value;
            break;
        }
    }

    std::vector<std::string> codes;
    for (const char* key : {"ret_code", "code", "err_code", "error_code"}) {
        std::string value = json_value_or_empty(json, key);
        if (!value.empty()) {
            codes.push_back(std::string(key) + "=" + value);
        }
    }
    if (!codes.empty()) {
        std::string joined;
        for (size_t i = 0; i < codes.size(); ++i) {
            if (i > 0) {
                joined += "，";
            }
            joined += codes[i];
        }
        return message.empty() ? joined : message + "（" + joined + "）";
    }
    return message;
}

bool response_ok(const std::string& json) {
    std::string result = lower_copy(json_value_or_empty(json, "result"));
    return result == "1" || result == "ok" || result == "true";
}

std::string list_array_content(const std::string& json) {
    size_t key_pos = json.find("\"list\"");
    if (key_pos == std::string::npos) {
        return "";
    }
    size_t left = json.find('[', key_pos);
    if (left == std::string::npos) {
        return "";
    }
    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    for (size_t i = left; i < json.size(); ++i) {
        char ch = json[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '[') {
            ++depth;
        } else if (ch == ']') {
            --depth;
            if (depth == 0) {
                return json.substr(left + 1, i - left - 1);
            }
        }
    }
    return "";
}

std::vector<std::string> object_strings_from_array(const std::string& array_content) {
    std::vector<std::string> objects;
    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    size_t begin = std::string::npos;
    for (size_t i = 0; i < array_content.size(); ++i) {
        char ch = array_content[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '{') {
            if (depth == 0) {
                begin = i;
            }
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && begin != std::string::npos) {
                objects.push_back(array_content.substr(begin, i - begin + 1));
                begin = std::string::npos;
            }
        }
    }
    return objects;
}

Device parse_device(const std::string& json) {
    Device device;
    device.user_account = json_value_or_empty(json, "user_account");
    device.online_ip = json_value_or_empty(json, "online_ip");
    device.online_mac = upper_copy(json_value_or_empty(json, "online_mac"));
    device.online_time = json_value_or_empty(json, "online_time");
    device.time_long = json_value_or_empty(json, "time_long");
    device.is_owner_ip = json_value_or_empty(json, "is_owner_ip");
    device.is_perceive = json_value_or_empty(json, "is_perceive");
    return device;
}

std::vector<Device> parse_devices(const std::string& json) {
    std::vector<Device> devices;
    std::string content = list_array_content(json);
    for (const std::string& object : object_strings_from_array(content)) {
        Device device = parse_device(object);
        if (!device.online_ip.empty() || !device.online_mac.empty() || !device.user_account.empty()) {
            devices.push_back(device);
        }
    }
    return devices;
}

bool same_device(const Device& left, const Device& right) {
    return (!left.online_ip.empty() && left.online_ip == right.online_ip) ||
           (!left.online_mac.empty() && left.online_mac == right.online_mac);
}

std::string auth_method_label(const Device& device) {
    std::string account = device.user_account;
    bool phone = account.size() == 11 && account[0] == '1';
    for (char ch : account) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            phone = false;
            break;
        }
    }
    std::string method = phone ? "手机验证码登录" : "账号密码登录";
    if (device.is_perceive == "1") {
        method += " / 感知上线";
    }
    return method;
}

#ifndef ZJNU_AUTH_NO_CURL
size_t curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* text = static_cast<std::string*>(userp);
    text->append(static_cast<char*>(contents), total);
    return total;
}

std::string curl_escape(CURL* curl, const std::string& value) {
    char* escaped = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    if (escaped == nullptr) {
        return "";
    }
    std::string out(escaped);
    curl_free(escaped);
    return out;
}

std::string build_query(CURL* curl, const std::vector<std::pair<std::string, std::string>>& params) {
    std::string query;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            query += "&";
        }
        query += curl_escape(curl, params[i].first);
        query += "=";
        query += curl_escape(curl, params[i].second);
    }
    return query;
}
#else
bool url_unreserved(unsigned char ch) {
    return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

std::string url_escape(const std::string& value) {
    const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char ch : value) {
        if (url_unreserved(ch)) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[(ch >> 4U) & 0x0FU]);
            out.push_back(hex[ch & 0x0FU]);
        }
    }
    return out;
}

std::string build_query(const std::vector<std::pair<std::string, std::string>>& params) {
    std::string query;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            query += "&";
        }
        query += url_escape(params[i].first);
        query += "=";
        query += url_escape(params[i].second);
    }
    return query;
}

int connect_tcp(const std::string& host, int port, int timeout_seconds, std::string* error) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        if (error != nullptr) {
            *error = "静态无 curl 构建只支持 IP 地址形式的 Portal 主机";
        }
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        if (error != nullptr) {
            *error = "创建 socket 失败";
        }
        return -1;
    }

    timeval tv{};
    tv.tv_sec = timeout_seconds;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (error != nullptr) {
            *error = std::string("连接失败: ") + std::strerror(errno);
        }
        close(sock);
        return -1;
    }
    return sock;
}

bool send_all(int sock, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(sock, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

RequestResult simple_http_get(const std::string& api_base,
                              const std::string& endpoint,
                              const std::vector<std::pair<std::string, std::string>>& params,
                              int timeout_seconds) {
    if (api_base_is_https(api_base)) {
        return {false, "请求失败: 静态无 curl 构建不支持 HTTPS，请使用默认 HTTP Portal API", ""};
    }

    std::string host = portal_host_from_base(api_base);
    int port = portal_port_from_base(api_base);
    std::string path = portal_path_from_base(api_base) + endpoint + "?" + build_query(params);

    std::string error;
    int sock = connect_tcp(host, port, timeout_seconds, &error);
    if (sock < 0) {
        return {false, "请求失败: " + error, ""};
    }

    std::ostringstream request;
    request << "GET " << path << " HTTP/1.1\r\n"
            << "Host: " << host << "\r\n"
            << "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124 Safari/537.36\r\n"
            << "Accept: */*\r\n"
            << "Referer: http://10.1.116.8/\r\n"
            << "Connection: close\r\n\r\n";

    if (!send_all(sock, request.str())) {
        close(sock);
        return {false, "请求失败: 发送 HTTP 请求失败", ""};
    }

    std::string raw;
    char buffer[4096];
    while (true) {
        ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
        if (n > 0) {
            raw.append(buffer, static_cast<size_t>(n));
        } else {
            break;
        }
    }
    close(sock);

    if (raw.empty()) {
        return {false, "请求失败: 空响应", ""};
    }

    size_t status_end = raw.find("\r\n");
    std::string status_line = status_end == std::string::npos ? raw : raw.substr(0, status_end);
    int http_code = 0;
    std::sscanf(status_line.c_str(), "HTTP/%*s %d", &http_code);
    if (http_code >= 400) {
        return {false, "请求失败: HTTP " + std::to_string(http_code), ""};
    }

    size_t body_pos = raw.find("\r\n\r\n");
    std::string body = body_pos == std::string::npos ? raw : raw.substr(body_pos + 4);
    std::string json = strip_jsonp(body);
    if (json.empty()) {
        return {false, "响应解析失败: 空响应", ""};
    }
    bool ok = response_ok(json);
    return {ok, response_message(json), json};
}
#endif

std::string read_password(const std::string& prompt) {
    std::cerr << prompt;
    termios old_term{};
    termios new_term{};
    bool changed = false;
    if (tcgetattr(STDIN_FILENO, &old_term) == 0) {
        new_term = old_term;
        new_term.c_lflag &= static_cast<unsigned int>(~ECHO);
        changed = tcsetattr(STDIN_FILENO, TCSANOW, &new_term) == 0;
    }
    std::string password;
    std::getline(std::cin, password);
    if (changed) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    }
    std::cerr << '\n';
    return password;
}

void sleep_seconds(double seconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000)));
}

template <typename Func>
auto run_with_spinner(const std::string& message, Func operation) -> decltype(operation()) {
    auto future = std::async(std::launch::async, std::move(operation));
    const std::string frames = "|/-\\";
    size_t index = 0;
    while (future.wait_for(std::chrono::milliseconds(120)) != std::future_status::ready) {
        std::cout << "\r " << C::BY << frames[index % frames.size()] << C::RST << " " << message << "..." << std::flush;
        ++index;
    }
    int clear_len = visible_width(message) + 12;
    std::cout << "\r" << repeat_char(' ', clear_len) << "\r" << std::flush;
    return future.get();
}

class ZJNUAuth {
public:
    explicit ZJNUAuth(std::string base = DEFAULT_API_BASE, int timeout = 10, bool use_proxy = false)
        : api_base_(std::move(base)), timeout_(timeout), use_proxy_(use_proxy) {
        while (!api_base_.empty() && api_base_.back() == '/') {
            api_base_.pop_back();
        }
        portal_host_ = portal_host_from_base(api_base_);
        local_ip_ = get_local_ip(portal_host_);
        local_mac_ = get_mac_address();
    }

    const std::string& api_base() const { return api_base_; }
    const std::string& portal_host() const { return portal_host_; }
    const std::string& local_ip() const { return local_ip_; }
    const std::string& local_mac() const { return local_mac_; }

    RequestResult request(const std::string& endpoint, std::vector<std::pair<std::string, std::string>> params) const {
        params.emplace_back("callback", callback_name());
        params.emplace_back("jsVersion", JS_VERSION);
        params.emplace_back("v", now_mod_100000());
        params.emplace_back("lang", "zh-cn");

#ifdef ZJNU_AUTH_NO_CURL
        return simple_http_get(api_base_, endpoint, params, timeout_);
#else
        CURL* curl = curl_easy_init();
        if (curl == nullptr) {
            return {false, "请求失败: curl 初始化失败", ""};
        }

        std::string response;
        std::string url = api_base_ + endpoint + "?" + build_query(curl, params);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout_);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        // 默认禁用系统代理，避免干扰校园网认证
        if (!use_proxy_) {
            curl_easy_setopt(curl, CURLOPT_PROXY, "");
            curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
        }

        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124 Safari/537.36");
        headers = curl_slist_append(headers, "Accept: */*");
        headers = curl_slist_append(headers, "Referer: http://10.1.116.8/");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        CURLcode code = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (code != CURLE_OK) {
            return {false, std::string("请求失败: ") + curl_easy_strerror(code), ""};
        }
        if (http_code >= 400) {
            return {false, "请求失败: HTTP " + std::to_string(http_code), ""};
        }

        std::string json = strip_jsonp(response);
        if (json.empty()) {
            return {false, "响应解析失败: 空响应", ""};
        }
        bool ok = response_ok(json);
        return {ok, response_message(json), json};
#endif
    }

    std::string make_user_account(std::string account, const std::string& operator_id) const {
        std::string op = normalize_operator(operator_id);
        if (op == "2" && !ends_with(account, "@dx")) {
            account += "@dx";
        } else if (op == "3" && !ends_with(account, "@lt")) {
            account += "@lt";
        }
        return "," + op + "," + account;
    }

    std::pair<bool, std::string> login_with_password(const std::string& username,
                                                     const std::string& password,
                                                     const std::string& operator_id) const {
        RequestResult result = request("/login", {
            {"login_method", "1"},
            {"user_account", make_user_account(username, operator_id)},
            {"user_password", password},
            {"wlan_user_ip", local_ip_},
            {"wlan_user_ipv6", ""},
            {"wlan_user_mac", local_mac_},
            {"wlan_ac_ip", ""},
            {"wlan_ac_name", ""},
            {"terminal_type", TERMINAL_TYPE},
        });
        if (result.ok) {
            return {true, result.message.empty() ? "Portal协议认证成功" : result.message};
        }
        if (json_value_or_empty(result.json, "ret_code") == "2") {
            return {true, result.message.empty() ? "当前 IP 已在线" : result.message};
        }
        return {false, result.message.empty() ? "登录失败" : result.message};
    }

    std::pair<bool, std::string> send_sms(const std::string& phone) const {
        RequestResult result = request("/sms", {
            {"telephone", phone},
            {"mac", local_mac_},
            {"ip", local_ip_},
            {"ipv6", ""},
            {"bind", "0"},
            {"page_index", ""},
            {"prefix", ""},
            {"sms_type", "0"},
        });
        return {result.ok, result.message.empty() ? (result.ok ? "验证码发送成功" : "发送验证码失败") : result.message};
    }

    std::pair<bool, std::string> login_with_phone(const std::string& phone,
                                                  const std::string& code,
                                                  const std::string& operator_id) const {
        return login_with_password(phone, code, operator_id);
    }

    std::pair<bool, std::vector<Device>> query_current_online() const {
        RequestResult result = request("/online_list", {});
        if (!result.ok) {
            return {false, {}};
        }
        std::vector<Device> devices = parse_devices(result.json);
        return {!devices.empty(), devices};
    }

    AuthSnapshot get_auth_snapshot() const {
        auto [ok, devices] = query_current_online();
        if (!ok || devices.empty()) {
            return {false, "当前设备未登录，或当前环境无法访问认证网关", "", {}, {}};
        }
        Device current = devices.front();
        for (const Device& device : devices) {
            if (device.online_ip == local_ip_ || device.is_owner_ip == "1") {
                current = device;
                break;
            }
        }
        return {true, "当前设备已认证在线", current.user_account, current, devices};
    }

    std::vector<Device> query_account_devices_with_current(const std::string& account,
                                                           const AuthSnapshot& snapshot) const {
        std::vector<Device> merged;
        RequestResult mac_result = request("/mac/find", {{"user_account", account}});
        RequestResult online_result = request("/online_list", {{"user_account", account}});

        auto append = [&](const std::vector<Device>& devices) {
            for (Device device : devices) {
                if (!account.empty() && !device.user_account.empty() && device.user_account != account) {
                    continue;
                }
                bool exists = false;
                for (const Device& item : merged) {
                    if (same_device(device, item)) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    merged.push_back(std::move(device));
                }
            }
        };

        append(parse_devices(mac_result.json));
        append(parse_devices(online_result.json));

        if (snapshot.online) {
            Device current = snapshot.current_device;
            if (current.user_account.empty()) {
                current.user_account = snapshot.account.empty() ? account : snapshot.account;
            }
            bool exists = false;
            for (const Device& item : merged) {
                if (same_device(current, item)) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                merged.push_back(current);
            }
        }
        return merged;
    }

    std::map<std::string, std::string> load_portal_config() const {
        RequestResult result = request("/page/loadConfig", {
            {"program_index", ""},
            {"wlan_vlan_id", "1"},
            {"wlan_user_ip", base64_encode(local_ip_)},
            {"wlan_user_ipv6", ""},
            {"wlan_user_ssid", ""},
            {"wlan_user_areaid", ""},
            {"wlan_ac_ip", ""},
            {"wlan_ap_mac", "000000000000"},
            {"gw_id", "000000000000"},
        });
        std::map<std::string, std::string> config;
        config["un_bind_mac"] = json_value_or_empty(result.json, "un_bind_mac");
        config["register_mode"] = json_value_or_empty(result.json, "register_mode");
        if (config["un_bind_mac"].empty()) {
            config["un_bind_mac"] = "1";
        }
        if (config["register_mode"].empty()) {
            config["register_mode"] = "1";
        }
        return config;
    }

    std::pair<bool, std::string> browser_style_unbind_logout(const AuthSnapshot& snapshot) const {
        std::string ip = snapshot.current_device.online_ip.empty() ? local_ip_ : snapshot.current_device.online_ip;
        std::string mac = upper_copy(snapshot.current_device.online_mac.empty() ? local_mac_ : snapshot.current_device.online_mac);
        RequestResult result = request("/mac/unbind", {
            {"user_account", snapshot.account},
            {"wlan_user_mac", mac},
            {"wlan_user_ip", std::to_string(ip_to_uint(ip))},
        });
        return {result.ok, result.message.empty() ? (result.ok ? "解绑终端MAC成功！" : "注销并解绑MAC失败") : result.message};
    }

    std::pair<bool, std::string> browser_style_portal_logout(const AuthSnapshot& snapshot) const {
        std::string ip = snapshot.current_device.online_ip.empty() ? local_ip_ : snapshot.current_device.online_ip;
        std::string mac = upper_copy(snapshot.current_device.online_mac.empty() ? local_mac_ : snapshot.current_device.online_mac);
        RequestResult result = request("/logout", {
            {"login_method", "1"},
            {"user_account", "drcom"},
            {"user_password", "123"},
            {"ac_logout", "0"},
            {"register_mode", "1"},
            {"wlan_user_ip", ip},
            {"wlan_user_ipv6", ""},
            {"wlan_vlan_id", "1"},
            {"wlan_user_mac", mac},
            {"wlan_ac_ip", ""},
            {"wlan_ac_name", ""},
        });
        return {result.ok, result.message.empty() ? (result.ok ? "Portal注销成功" : "Portal注销失败") : result.message};
    }

    std::pair<bool, std::string> submit_browser_style_logout(const AuthSnapshot& snapshot) const {
        std::map<std::string, std::string> config = load_portal_config();
        std::string un_bind_mac = config["un_bind_mac"];
        std::string register_mode = config["register_mode"];

        if (un_bind_mac == "1" && (register_mode == "1" || register_mode == "3" || register_mode == "4")) {
            auto [ok, msg] = browser_style_unbind_logout(snapshot);
            if (ok) {
                return {true, msg};
            }
            auto [fallback_ok, fallback_msg] = browser_style_portal_logout(snapshot);
            if (fallback_ok) {
                return {true, fallback_msg};
            }
            return {false, msg + "；fallback: " + fallback_msg};
        }
        return browser_style_portal_logout(snapshot);
    }

    std::pair<bool, std::string> logout_current_safe() const {
        AuthSnapshot snapshot = get_auth_snapshot();
        if (!snapshot.online) {
            return {false, "当前设备未登录，无法注销"};
        }
        auto [ok, msg] = submit_browser_style_logout(snapshot);
        if (!ok) {
            return {false, msg};
        }

        for (int i = 0; i < 6; ++i) {
            sleep_seconds(0.5);
            AuthSnapshot after = get_auth_snapshot();
            if (!after.online) {
                return {true, msg};
            }
        }
        return {false, msg + "\n ! 3秒内当前设备仍显示在线，可能注销失败，请稍后刷新认证页确认。"};
    }

private:
    std::string normalize_operator(const std::string& operator_id) const {
        if (operator_id == "1" || operator_id == "2" || operator_id == "3" || operator_id == "4") {
            return operator_id;
        }
        return DEFAULT_OPERATOR;
    }

    std::string api_base_;
    std::string portal_host_;
    std::string local_ip_;
    std::string local_mac_;
    int timeout_;
    bool use_proxy_;
};

std::string normalize_operator_global(const std::string& operator_id) {
    if (operator_id == "1" || operator_id == "2" || operator_id == "3" || operator_id == "4") {
        return operator_id;
    }
    return DEFAULT_OPERATOR;
}

std::string select_operator() {
    std::cout << "\n " << C::BM << "*" << C::RST << " 请选择运营商:\n";
    std::cout << "   " << C::BG << "[1]" << C::RST << " " << C::W << "校园用户" << C::RST << " " << C::DG << "(默认)" << C::RST << '\n';
    std::cout << "   " << C::BG << "[2]" << C::RST << " " << C::W << "校园电信" << C::RST << '\n';
    std::cout << "   " << C::BG << "[3]" << C::RST << " " << C::W << "校园联通" << C::RST << '\n';
    std::cout << "   " << C::BG << "[4]" << C::RST << " " << C::W << "校园移动" << C::RST << '\n';
    while (true) {
        std::cout << " " << C::BM << "➤" << C::RST << " 请输入运营商编号[" << C::DG << DEFAULT_OPERATOR << C::RST << "]: ";
        std::string value;
        std::getline(std::cin, value);
        value = trim(value);
        if (value.empty()) {
            value = DEFAULT_OPERATOR;
        }
        if (value == "1" || value == "2" || value == "3" || value == "4") {
            return value;
        }
        std::cout << " " << C::R << "✗ 无效的运营商编号" << C::RST << '\n';
    }
}

std::pair<bool, std::string> test_external_network() {
    for (const std::string& host : PING_HOSTS) {
        std::string command = "ping -c 1 -W 2 " + host + " >/dev/null 2>&1";
        int code = std::system(command.c_str());
        if (code == 0) {
            return {true, host};
        }
    }
    return {false, "外网测试未通过"};
}

void print_device_list(const std::vector<Device>& devices, const std::string& current_ip, const std::string& current_mac) {
    if (devices.empty()) {
        std::cout << C::DG << "没有获取到在线设备列表" << C::RST << '\n';
        return;
    }
    std::cout << "\n" << C::BC << "=== 在线设备：" << devices.size() << " 台 ===" << C::RST << '\n';
    for (size_t i = 0; i < devices.size(); ++i) {
        const Device& device = devices[i];
        std::string ip = device.online_ip.empty() ? current_ip : device.online_ip;
        std::string mac = device.online_mac.empty() ? upper_copy(current_mac) : upper_copy(device.online_mac);
        std::cout << "  " << C::BB << "[" << (i + 1) << "]" << C::RST;
        if (ip == current_ip) {
            std::cout << " " << C::BG << "[当前]" << C::RST;
        }
        std::cout << " " << C::BY << ip << C::RST << " / " << C::BY << mac << C::RST << '\n';
        std::cout << "      账号: " << C::BW << (device.user_account.empty() ? "N/A" : device.user_account) << C::RST
                  << " | 登录: " << C::BY << (device.online_time.empty() ? "N/A" : device.online_time) << C::RST
                  << " | 在线: " << C::BY << format_duration(device.time_long.empty() ? "0" : device.time_long) << C::RST << '\n';
    }
}

AuthSnapshot wait_snapshot(const ZJNUAuth& auth, int attempts = 3, double delay = 1.0) {
    AuthSnapshot snapshot;
    for (int i = 0; i < attempts; ++i) {
        snapshot = auth.get_auth_snapshot();
        if (snapshot.online) {
            return snapshot;
        }
        sleep_seconds(delay);
    }
    return snapshot;
}

void interactive_password_login(const ZJNUAuth& auth) {
    std::cout << "\n" << C::BC << "=== 账号密码登录 ===" << C::RST << '\n';
    std::cout << " " << C::BM << "*" << C::RST << " 请输入账号: ";
    std::string username;
    std::getline(std::cin, username);
    username = trim(username);
    if (username.empty()) {
        std::cout << C::R << "✗ 账号不能为空" << C::RST << '\n';
        return;
    }

    std::string password = read_password(" " + C::BM + "*" + C::RST + " 请输入密码: ");
    password = trim(password);
    if (password.empty()) {
        std::cout << C::R << "✗ 密码不能为空" << C::RST << '\n';
        return;
    }

    std::string operator_id = select_operator();
    auto [ok, msg] = auth.login_with_password(username, password, operator_id);
    std::cout << "\n" << (ok ? C::BG + "✓" : C::R + "✗") << " " << C::W << msg << C::RST << '\n';
    if (!ok) {
        return;
    }

    auto [net_ok, net_msg] = run_with_spinner("正在测试外网连通", []() { return test_external_network(); });
    std::cout << (net_ok ? C::BG + "✓ 外网连通测试通过: " : C::BY + "! 外网连通测试未通过: ") << net_msg << C::RST << '\n';

    AuthSnapshot snapshot = wait_snapshot(auth);
    std::string account = snapshot.account.empty() ? username : snapshot.account;
    std::vector<Device> devices = auth.query_account_devices_with_current(account, snapshot);
    print_device_list(devices, auth.local_ip(), auth.local_mac());
}

void interactive_phone_login(const ZJNUAuth& auth) {
    std::cout << "\n" << C::BC << "=== 手机验证码登录 ===" << C::RST << '\n';
    std::cout << " " << C::BM << "*" << C::RST << " 请输入手机号: ";
    std::string phone;
    std::getline(std::cin, phone);
    phone = trim(phone);
    if (phone.empty()) {
        std::cout << C::R << "✗ 手机号不能为空" << C::RST << '\n';
        return;
    }

    auto [sms_ok, sms_msg] = run_with_spinner("正在发送短信验证码", [&auth, &phone]() { return auth.send_sms(phone); });
    (void)sms_msg;
    std::cout << " " << (sms_ok ? C::BG + "✓" : C::R + "✗") << " " << C::W << "验证码正在下发，稍有延迟，请用户注意查收！" << C::RST << '\n';
    if (!sms_ok) {
        std::cout << " ! " << C::BY << "请检查手机号码是否正确或者重复发送验证码，请退出重新登录..." << C::RST << '\n';
        debug_log("短信接口消息: " + sms_msg);
        return;
    }

    std::cout << " " << C::BM << "*" << C::RST << " 请输入短信验证码: ";
    std::string code;
    std::getline(std::cin, code);
    code = trim(code);
    if (code.empty()) {
        std::cout << " " << C::R << "✗ 验证码不能为空，请重新输入一次" << C::RST << '\n';
        std::cout << " " << C::BM << "*" << C::RST << " 请输入短信验证码: ";
        std::getline(std::cin, code);
        code = trim(code);
        if (code.empty()) {
            std::cout << " " << C::R << "✗ 验证码不能为空" << C::RST << '\n';
            return;
        }
    }

    std::string operator_id = select_operator();
    auto [ok, msg] = auth.login_with_phone(phone, code, operator_id);
    std::cout << "\n" << (ok ? C::BG + "✓" : C::R + "✗") << " " << C::W << msg << C::RST << '\n';
    if (!ok) {
        std::cout << " " << C::BM << "*" << C::RST << " 登录失败，请重新输入短信验证码（直接回车取消）: ";
        std::getline(std::cin, code);
        code = trim(code);
        if (code.empty()) {
            return;
        }
        std::tie(ok, msg) = auth.login_with_phone(phone, code, operator_id);
        std::cout << "\n" << (ok ? C::BG + "✓" : C::R + "✗") << " " << C::W << msg << C::RST << '\n';
    }
    if (!ok) {
        return;
    }

    auto [net_ok, net_msg] = run_with_spinner("正在测试外网连通", []() { return test_external_network(); });
    std::cout << (net_ok ? C::BG + "✓ 外网连通测试通过: " : C::BY + "! 外网连通测试未通过: ") << net_msg << C::RST << '\n';

    AuthSnapshot snapshot = wait_snapshot(auth);
    if (snapshot.online) {
        std::string account = snapshot.account.empty() ? phone : snapshot.account;
        std::vector<Device> devices = auth.query_account_devices_with_current(account, snapshot);
        print_device_list(devices, auth.local_ip(), auth.local_mac());
    } else {
        std::cout << C::BY << "! 登录接口返回成功，但暂未查询到当前在线详情，请稍后刷新。" << C::RST << '\n';
    }
}

void interactive_logout(const ZJNUAuth& auth) {
    std::cout << "\n" << C::BC << "=== 注销当前设备 ===" << C::RST << '\n';
    std::cout << " " << C::BB << "[+]" << C::RST << " 当前IP: " << C::BY << auth.local_ip() << C::RST << '\n';
    std::cout << " " << C::BB << "[+]" << C::RST << " 当前MAC: " << C::BY << upper_copy(auth.local_mac()) << C::RST << '\n';
    if (std::getenv("SSH_CONNECTION") != nullptr) {
        std::cout << " " << C::BY << "！注销认证本身可能让远程连接短暂中断。" << C::RST << '\n';
    }
    std::cout << "\n " << C::BM << "➤" << C::RST << " 确认注销当前设备吗? [" << C::DG << "y/N" << C::RST << "]: ";
    std::string confirm;
    std::getline(std::cin, confirm);
    if (lower_copy(trim(confirm)) != "y") {
        std::cout << C::DG << "已取消" << C::RST << '\n';
        return;
    }

    auto [ok, msg] = run_with_spinner("正在注销当前设备", [&auth]() { return auth.logout_current_safe(); });
    std::cout << (ok ? C::BG + " ✓ " : C::BY + " ! ") << msg << C::RST << '\n';
}

void print_dashboard(const ZJNUAuth& auth, const AuthSnapshot& snapshot) {
    int clear_code = std::system("clear 2>/dev/null");
    (void)clear_code;
    std::string header = "///  ZJNU Campus Network Auth  ///    [ " + current_time_minute() + " ]";
    std::cout << C::DG << separator('=') << C::RST << '\n';
    std::cout << C::BM << center_text(header) << C::RST << '\n';
    std::cout << C::BC << center_text("浙江师范大学校园网认证工具") << C::RST << '\n';
    std::cout << C::DG << separator('=') << C::RST << "\n\n";
    std::cout << " " << C::BB << "[+]" << C::RST << " " << C::W << "认证网关" << C::RST << " " << C::DG << "::" << C::RST
              << " " << C::BC << auth.portal_host() << C::RST << '\n';
    std::cout << " " << C::BB << "[+]" << C::RST << " " << C::W << "本机设备" << C::RST << " " << C::DG << "::" << C::RST
              << " " << C::BY << auth.local_ip() << C::RST << " " << C::DG << "/" << C::RST << " " << C::BY << upper_copy(auth.local_mac()) << C::RST << '\n';
    std::cout << C::DG << separator('-') << C::RST << '\n';
    if (snapshot.online) {
        const Device& device = snapshot.current_device;
        std::cout << " " << C::BG << "✓ 当前设备已认证在线" << C::RST << '\n';
        std::cout << format_dashboard_row("用户账号", snapshot.account.empty() ? "N/A" : snapshot.account, C::BW,
                                          "认证方式", auth_method_label(device), C::BW)
                  << '\n';
        std::cout << format_dashboard_row("IP地址  ", device.online_ip.empty() ? auth.local_ip() : device.online_ip, C::BY,
                                          "MAC地址 ", upper_copy(device.online_mac.empty() ? auth.local_mac() : device.online_mac), C::BY)
                  << '\n';
        std::cout << format_dashboard_row("登录时间", device.online_time.empty() ? "N/A" : device.online_time, C::BY,
                                          "在线时长", format_duration(device.time_long.empty() ? "0" : device.time_long), C::BY)
                  << '\n';
    } else {
        std::cout << " " << C::R << "✗ 当前设备未登录认证" << C::RST << '\n';
        std::cout << " " << C::DG << (snapshot.message.empty() ? "请先使用账号密码或手机验证码登录。" : snapshot.message) << C::RST << '\n';
    }
    std::cout << C::DG << separator('-') << C::RST << '\n';
    if (snapshot.online) {
        std::cout << "  " << C::DG << "[1] 账号密码登录" << C::RST << '\n';
        std::cout << "  " << C::DG << "[2] 手机验证码登录" << C::RST << '\n';
        std::cout << "  " << C::BG << "[3]" << C::RST << " " << C::W << "注销当前设备" << C::RST << '\n';
    } else {
        std::cout << "  " << C::BG << "[1]" << C::RST << " " << C::W << "账号密码登录" << C::RST << '\n';
        std::cout << "  " << C::BG << "[2]" << C::RST << " " << C::W << "手机验证码登录" << C::RST << '\n';
        std::cout << "  " << C::DG << "[3] 注销当前设备" << C::RST << '\n';
    }
    std::cout << "  " << C::BG << "[0]" << C::RST << " " << C::W << "退出工具" << C::RST << "\n\n";
}

void interactive_mode() {
    ZJNUAuth auth;
    while (true) {
        AuthSnapshot snapshot = auth.get_auth_snapshot();
        print_dashboard(auth, snapshot);
        std::cout << C::BG << "➤" << C::RST << " " << C::BW << "请输入操作编号[0-3]:" << C::RST << " ";
        std::string choice;
        std::getline(std::cin, choice);
        choice = trim(choice);

        if ((choice == "1" || choice == "2") && snapshot.online) {
            std::cout << "\n" << C::R << "✗ 当前设备已登录，请先注销当前设备后再重新登录。" << C::RST << '\n';
        } else if (choice == "3" && !snapshot.online) {
            std::cout << "\n" << C::R << "✗ 当前设备未登录，无法使用注销功能。" << C::RST << '\n';
        } else if (choice == "1") {
            interactive_password_login(auth);
        } else if (choice == "2") {
            interactive_phone_login(auth);
        } else if (choice == "3") {
            interactive_logout(auth);
        } else if (choice == "0") {
            std::cout << "\n" << C::BC << "再见！" << C::RST << '\n';
            return;
        } else {
            std::cout << "\n" << C::R << "✗ 无效选择" << C::RST << '\n';
        }

        std::cout << "\n" << C::DG << "按回车继续..." << C::RST;
        std::string ignored;
        std::getline(std::cin, ignored);
    }
}

std::optional<bool> confirm_login_state(const ZJNUAuth& auth) {
    auto check_once = [&auth]() {
        return auth.get_auth_snapshot().online;
    };

    bool first = check_once();
    debug_log(std::string("状态检测第1次: ") + (first ? "已登录" : "未登录"));
    sleep_seconds(CLI_STATE_PAIR_INTERVAL);
    bool second = check_once();
    debug_log(std::string("状态检测第2次: ") + (second ? "已登录" : "未登录"));

    if (first && second) {
        return true;
    }
    if (!first && !second) {
        return false;
    }

    sleep_seconds(CLI_STATE_RETRY_DELAY);
    first = check_once();
    debug_log(std::string("重试状态检测第1次: ") + (first ? "已登录" : "未登录"));
    sleep_seconds(CLI_STATE_PAIR_INTERVAL);
    second = check_once();
    debug_log(std::string("重试状态检测第2次: ") + (second ? "已登录" : "未登录"));

    if (first && second) {
        return true;
    }
    if (!first && !second) {
        return false;
    }
    return std::nullopt;
}

int cli_login(const std::vector<std::string>& args) {
    std::string username;
    std::string password;
    std::string operator_id = DEFAULT_OPERATOR;
    bool use_proxy = false;
    std::vector<std::string> positional;

    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "-u" || args[i] == "--username") && i + 1 < args.size()) {
            username = args[++i];
        } else if ((args[i] == "-p" || args[i] == "--password") && i + 1 < args.size()) {
            password = args[++i];
     } else if ((args[i] == "-o" || args[i] == "--operator") && i + 1 < args.size()) {
            operator_id = normalize_operator_global(args[++i]);
     } else if (args[i] == "--use-proxy") {
            use_proxy = true;
        } else {
            positional.push_back(args[i]);
        }
    }

    if (username.empty() && !positional.empty()) {
        username = positional[0];
    }
    if (password.empty() && positional.size() > 1) {
        password = positional[1];
    }
    if (username.empty() || password.empty()) {
        return 1;
    }

    ZJNUAuth auth(DEFAULT_API_BASE, 10, use_proxy);
    debug_log("认证网关=" + auth.api_base() + "，本机IP=" + auth.local_ip() + "，本机MAC=" + auth.local_mac());
    std::optional<bool> state = confirm_login_state(auth);
    if (!state.has_value()) {
        return 3;
    }
    if (*state) {
        return 0;
    }

    auto [ok, msg] = auth.login_with_password(username, password, operator_id);
    debug_log("登录请求结果: " + std::string(ok ? "ok" : "fail") + "，message=" + msg);
    if (!ok) {
        return 1;
    }
    state = confirm_login_state(auth);
    if (!state.has_value()) {
        return 3;
    }
    return *state ? 0 : 1;
}

int cli_logout(const std::vector<std::string>& args) {
    bool use_proxy = false;
    for (const std::string& arg : args) {
        if (arg == "--use-proxy") {
          use_proxy = true;
            break;
        }
    }
    ZJNUAuth auth(DEFAULT_API_BASE, 10, use_proxy);
    std::optional<bool> state = confirm_login_state(auth);
    if (!state.has_value()) {
        return 3;
    }
    if (!*state) {
        return 0;
    }

    AuthSnapshot snapshot = auth.get_auth_snapshot();
    if (!snapshot.online) {
        return 0;
    }

    auto [ok, msg] = auth.submit_browser_style_logout(snapshot);
    debug_log("注销请求结果: " + std::string(ok ? "ok" : "fail") + "，message=" + msg);
    if (!ok) {
        return 2;
    }

    for (int i = 0; i < CLI_LOGOUT_CHECKS; ++i) {
        bool online = auth.get_auth_snapshot().online;
        debug_log("注销后第 " + std::to_string(i + 1) + "/" + std::to_string(CLI_LOGOUT_CHECKS) +
                  " 次检测: " + (online ? "已登录" : "未登录"));
        if (!online) {
            return 0;
        }
        sleep_seconds(CLI_LOGOUT_INTERVAL);
    }
    return 4;
}

int cli_status(const std::vector<std::string>& args) {
    bool use_proxy = false;
    for (const std::string& arg : args) {
        if (arg == "--use-proxy") {
            use_proxy = true;
            break;
        }
    }
    ZJNUAuth auth(DEFAULT_API_BASE, 10, use_proxy);
    std::optional<bool> state = confirm_login_state(auth);
    if (!state.has_value()) {
        return 3;
    }
    if (!*state) {
        return 1;
    }

    AuthSnapshot snapshot = auth.get_auth_snapshot();
    if (!snapshot.online || snapshot.account.empty()) {
        return 1;
    }
    std::cout << snapshot.account << '\n';
    return 0;
}

void print_help() {
    std::cout << "浙江师范大学校园网认证工具 C++ 版\n\n";
    std::cout << "用法:\n";
    std::cout << "  ./zjnu_auth_lit\n";
    std::cout << "  ./zjnu_auth_lit [--debug] [--use-proxy] login <账号> <密码> [-o 运营商]\n";
    std::cout << "  ./zjnu_auth_lit [--debug] [--use-proxy] login -u <账号> -p <密码> [-o 运营商]\n";
    std::cout << "  ./zjnu_auth_lit [--debug] [--use-proxy] logout\n";
    std::cout << "  ./zjnu_auth_lit [--debug] [--use-proxy] status\n\n";
    std::cout << "选项:\n";
    std::cout << "  --use-proxy    使用系统代理（默认禁用代理）\n\n";
    std::cout << "运营商: 1 校园用户，2 校园电信，3 校园联通，4 校园移动\n";
}

int cli_mode(std::vector<std::string> args) {
    if (!args.empty() && args[0] == "--debug") {
        g_debug = true;
        args.erase(args.begin());
    }
    if (args.empty() || args[0] == "-h" || args[0] == "--help" || args[0] == "help") {
        print_help();
        return 0;
    }
    std::string command = args[0];
    args.erase(args.begin());
    if (command == "login") {
        return cli_login(args);
    }
    if (command == "logout") {
        return cli_logout(args);
    }
    if (command == "status") {
        return cli_status(args);
    }
    print_help();
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
#ifndef ZJNU_AUTH_NO_CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
    int code = 0;
    try {
        if (argc > 1) {
            std::vector<std::string> args;
            for (int i = 1; i < argc; ++i) {
                args.emplace_back(argv[i]);
            }
            code = cli_mode(args);
        } else {
            interactive_mode();
            code = 0;
        }
    } catch (const std::exception& exc) {
        std::cerr << "程序异常: " << exc.what() << '\n';
        code = 1;
    }
#ifndef ZJNU_AUTH_NO_CURL
    curl_global_cleanup();
#endif
    return code;
}
