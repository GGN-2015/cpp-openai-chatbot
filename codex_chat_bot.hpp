#ifndef CODEX_CHAT_BOT_HPP
#define CODEX_CHAT_BOT_HPP

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef CODEX_CHAT_BOT_HAS_TERMIOS
#if defined(__unix__) || defined(__APPLE__)
#define CODEX_CHAT_BOT_HAS_TERMIOS 1
#else
#define CODEX_CHAT_BOT_HAS_TERMIOS 0
#endif
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif CODEX_CHAT_BOT_HAS_TERMIOS
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace codex_chat_bot {

inline constexpr const char* VERSION = "0.1.0-cpp";
inline constexpr const char* DEFAULT_MODEL = "gpt-5.5";
inline constexpr const char* DEFAULT_SYSTEM_RULE = "You are a helpful assistant.";
inline constexpr double DEFAULT_TIMEOUT_SECONDS = 600.0;
inline constexpr int MAX_REQUEST_HISTORY_MESSAGES = 30;
inline constexpr const char* SYSTEM_USERNAME = "system";
inline constexpr const char* DEVELOPER_USERNAME = "developer";
inline constexpr const char* DEFAULT_USERNAME = "user";
inline constexpr const char* ASSISTANT_USERNAME = "assistant";

class ChatBotError : public std::runtime_error {
public:
    explicit ChatBotError(const std::string& message) : std::runtime_error(message) {}
};

class MissingAPIKeyError : public ChatBotError {
public:
    explicit MissingAPIKeyError(const std::string& message) : ChatBotError(message) {}
};

class MissingBaseURLError : public ChatBotError {
public:
    explicit MissingBaseURLError(const std::string& message) : ChatBotError(message) {}
};

class ResponseTextError : public ChatBotError {
public:
    explicit ResponseTextError(const std::string& message) : ChatBotError(message) {}
};

class RequestCanceledError : public ChatBotError {
public:
    explicit RequestCanceledError(const std::string& message) : ChatBotError(message) {}
};

namespace detail {

inline volatile std::sig_atomic_t interrupt_requested_flag = 0;

inline void handle_interrupt_signal(int) {
    interrupt_requested_flag = 1;
}

inline std::string trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

inline std::vector<std::string> split_non_empty_lines(const std::string& value) {
    std::vector<std::string> lines;
    std::istringstream input(value);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

inline std::string join_with_space(const std::vector<std::string>& values) {
    std::string out;
    for (const auto& value : values) {
        std::string cleaned = trim(value);
        if (cleaned.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += ' ';
        }
        out += cleaned;
    }
    return out;
}

inline bool is_integer_number(double value) {
    return std::isfinite(value) && std::floor(value) == value &&
           value >= static_cast<double>(std::numeric_limits<int>::min()) &&
           value <= static_cast<double>(std::numeric_limits<int>::max());
}

inline std::string path_for_error(const std::filesystem::path& path) {
    return path.string();
}

inline std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not open file for reading: " + path_for_error(path));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

inline void write_text_file(const std::filesystem::path& path, const std::string& data) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("could not open file for writing: " + path_for_error(path));
    }
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!output) {
        throw std::runtime_error("could not write file: " + path_for_error(path));
    }
}

inline std::string format_double(double value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("JSON numbers must be finite");
    }
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

inline void append_utf8(std::string& out, unsigned int code_point) {
    if (code_point <= 0x7F) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        throw std::runtime_error("invalid unicode code point");
    }
}

inline std::string shell_quote(const std::string& value) {
#ifdef _WIN32
    std::string out = "\"";
    for (char ch : value) {
        if (ch == '"') {
            out += "\\\"";
        } else {
            out += ch;
        }
    }
    out += '"';
    return out;
#else
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out += ch;
        }
    }
    out += "'";
    return out;
#endif
}

inline std::string shell_command_executable(const std::string& value) {
#ifdef _WIN32
    if (value.find_first_of(" \t\"&()[]{}^=;!'+,`~") == std::string::npos) {
        return value;
    }
    return "call " + shell_quote(value);
#else
    return shell_quote(value);
#endif
}

inline std::string curl_config_quote(const std::string& value) {
    std::string out = "\"";
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(static_cast<char>(ch));
                break;
        }
    }
    out += '"';
    return out;
}

inline std::string path_for_curl_config(const std::filesystem::path& path) {
    return path.generic_string();
}

inline std::filesystem::path unique_temp_path(const std::string& prefix, const std::string& suffix) {
    static std::atomic<unsigned long long> counter{0};
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    auto base = std::filesystem::temp_directory_path();
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::ostringstream name;
        name << prefix << now << "_" << id << "_" << attempt << suffix;
        auto candidate = base / name.str();
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("could not create a unique temporary file path");
}

class TempFile {
public:
    explicit TempFile(std::filesystem::path path) : path_(std::move(path)) {}
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    ~TempFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

#ifdef _WIN32
class WindowsHandle {
public:
    WindowsHandle() = default;
    explicit WindowsHandle(HANDLE handle) : handle_(handle) {}
    WindowsHandle(const WindowsHandle&) = delete;
    WindowsHandle& operator=(const WindowsHandle&) = delete;

    WindowsHandle(WindowsHandle&& other) noexcept : handle_(other.release()) {}

    WindowsHandle& operator=(WindowsHandle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    ~WindowsHandle() {
        reset();
    }

    HANDLE get() const {
        return handle_;
    }

    bool valid() const {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE release() {
        HANDLE handle = handle_;
        handle_ = nullptr;
        return handle;
    }

    void reset(HANDLE handle = nullptr) {
        if (valid()) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};

inline std::string windows_error_message(DWORD code) {
    LPSTR buffer = nullptr;
    DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    std::string message;
    if (size > 0 && buffer != nullptr) {
        message.assign(buffer, buffer + size);
        LocalFree(buffer);
        message = trim(std::move(message));
    }
    if (message.empty()) {
        message = "Windows error " + std::to_string(static_cast<unsigned long>(code));
    }
    return message;
}

inline WindowsHandle open_windows_output_handle(const std::filesystem::path& path) {
    SECURITY_ATTRIBUTES attributes;
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = nullptr;
    attributes.bInheritHandle = TRUE;

    HANDLE handle = CreateFileW(
        path.wstring().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &attributes,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("could not open process output file: " + path_for_error(path) +
                                 ": " + windows_error_message(GetLastError()));
    }
    return WindowsHandle(handle);
}

inline std::wstring windows_quote_argument(const std::wstring& value) {
    if (value.empty()) {
        return L"\"\"";
    }

    bool needs_quotes = false;
    for (wchar_t ch : value) {
        if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r' || ch == L'"') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return value;
    }

    std::wstring out = L"\"";
    std::size_t backslashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(ch);
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            backslashes = 0;
            out.push_back(ch);
        }
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}
#elif CODEX_CHAT_BOT_HAS_TERMIOS
class PosixFd {
public:
    PosixFd() = default;
    explicit PosixFd(int fd) : fd_(fd) {}
    PosixFd(const PosixFd&) = delete;
    PosixFd& operator=(const PosixFd&) = delete;

    PosixFd(PosixFd&& other) noexcept : fd_(other.release()) {}

    PosixFd& operator=(PosixFd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    ~PosixFd() {
        reset();
    }

    int get() const {
        return fd_;
    }

    bool valid() const {
        return fd_ >= 0;
    }

    int release() {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }

    void reset(int fd = -1) {
        if (valid()) {
            ::close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_ = -1;
};

inline std::string errno_message(int code) {
    const char* message = std::strerror(code);
    if (message == nullptr || *message == '\0') {
        return "errno " + std::to_string(code);
    }
    return message;
}

inline PosixFd open_posix_output_fd(const std::filesystem::path& path) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        throw std::runtime_error("could not open process output file: " + path_for_error(path) +
                                 ": " + errno_message(errno));
    }
    return PosixFd(fd);
}
#endif

}  // namespace detail

inline bool interrupt_requested() {
    return detail::interrupt_requested_flag != 0;
}

inline void reset_interrupt_requested() {
    detail::interrupt_requested_flag = 0;
}

inline void request_interrupt() {
    detail::interrupt_requested_flag = 1;
}

class InterruptGuard {
public:
    InterruptGuard() : previous_handler_(std::signal(SIGINT, detail::handle_interrupt_signal)) {
        reset_interrupt_requested();
    }

    InterruptGuard(const InterruptGuard&) = delete;
    InterruptGuard& operator=(const InterruptGuard&) = delete;

    ~InterruptGuard() {
        if (previous_handler_ != SIG_ERR) {
            std::signal(SIGINT, previous_handler_);
        }
    }

    bool interrupted() const {
        return interrupt_requested();
    }

private:
    using SignalHandler = void (*)(int);

    SignalHandler previous_handler_;
};

class ConsoleUtf8Guard {
public:
#ifdef _WIN32
    ConsoleUtf8Guard()
        : old_input_cp_(GetConsoleCP()),
          old_output_cp_(GetConsoleOutputCP()),
          changed_input_(SetConsoleCP(CP_UTF8) != 0),
          changed_output_(SetConsoleOutputCP(CP_UTF8) != 0) {}

    ~ConsoleUtf8Guard() {
        if (changed_input_) {
            SetConsoleCP(old_input_cp_);
        }
        if (changed_output_) {
            SetConsoleOutputCP(old_output_cp_);
        }
    }
#else
    ConsoleUtf8Guard() = default;
    ~ConsoleUtf8Guard() = default;
#endif

    ConsoleUtf8Guard(const ConsoleUtf8Guard&) = delete;
    ConsoleUtf8Guard& operator=(const ConsoleUtf8Guard&) = delete;

private:
#ifdef _WIN32
    UINT old_input_cp_;
    UINT old_output_cp_;
    bool changed_input_;
    bool changed_output_;
#endif
};

inline std::string read_line(const std::string& prompt) {
    if (interrupt_requested() || !std::cin.good()) {
        return {};
    }
    std::cout << prompt;
    std::cout.flush();
    std::string value;
    if (!std::getline(std::cin, value)) {
        request_interrupt();
        return {};
    }
    return detail::trim(value);
}

inline std::string read_required_line(const std::string& prompt, const std::string& retry_prompt) {
    std::string value = read_line(prompt);
    while (value.empty() && !interrupt_requested() && std::cin.good()) {
        value = read_line(retry_prompt);
    }
    return value;
}

inline std::string read_secret(const std::string& prompt) {
    if (interrupt_requested() || !std::cin.good()) {
        return {};
    }
    std::cout << prompt;
    std::cout.flush();

#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD old_mode = 0;
    if (input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &old_mode)) {
        std::string value;
        if (!std::getline(std::cin, value)) {
            request_interrupt();
            return {};
        }
        return detail::trim(value);
    }

    DWORD new_mode = old_mode & ~ENABLE_ECHO_INPUT;
    if (!SetConsoleMode(input, new_mode)) {
        std::string value;
        if (!std::getline(std::cin, value)) {
            request_interrupt();
            return {};
        }
        return detail::trim(value);
    }

    std::string value;
    if (!std::getline(std::cin, value)) {
        SetConsoleMode(input, old_mode);
        request_interrupt();
        return {};
    }
    SetConsoleMode(input, old_mode);
    std::cout << "\n";
    return detail::trim(value);
#elif CODEX_CHAT_BOT_HAS_TERMIOS
    termios old_term{};
    if (tcgetattr(STDIN_FILENO, &old_term) != 0) {
        std::string value;
        if (!std::getline(std::cin, value)) {
            request_interrupt();
            return {};
        }
        return detail::trim(value);
    }

    termios new_term = old_term;
    new_term.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_term) != 0) {
        std::string value;
        if (!std::getline(std::cin, value)) {
            request_interrupt();
            return {};
        }
        return detail::trim(value);
    }

    std::string value;
    if (!std::getline(std::cin, value)) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
        request_interrupt();
        return {};
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    std::cout << "\n";
    return detail::trim(value);
#else
    std::string value;
    if (!std::getline(std::cin, value)) {
        request_interrupt();
        return {};
    }
    return detail::trim(value);
#endif
}

inline std::string read_required_secret(const std::string& prompt, const std::string& retry_prompt) {
    std::string value = read_secret(prompt);
    while (value.empty() && !interrupt_requested() && std::cin.good()) {
        value = read_secret(retry_prompt);
    }
    return value;
}

inline std::string collapse_line_breaks(std::string value) {
    std::string out;
    out.reserve(value.size());

    bool in_line_break = false;
    for (char ch : value) {
        if (ch == '\r' || ch == '\n') {
            if (!in_line_break) {
                out.push_back(' ');
            }
            in_line_break = true;
            continue;
        }
        in_line_break = false;
        out.push_back(ch);
    }

    return detail::trim(out);
}

inline std::string format_elapsed(std::chrono::seconds elapsed) {
    const auto total_seconds = elapsed.count();
    const auto minutes = total_seconds / 60;
    const auto seconds = total_seconds % 60;

    if (minutes <= 0) {
        return std::to_string(seconds) + "s";
    }
    return std::to_string(minutes) + "m " + std::to_string(seconds) + "s";
}

class ThinkingStatus {
public:
    explicit ThinkingStatus(std::string prefix)
        : prefix_(std::move(prefix)),
          started_at_(std::chrono::steady_clock::now()),
          running_(true),
          max_width_(0),
          worker_([this] { run(); }) {}

    ThinkingStatus(const ThinkingStatus&) = delete;
    ThinkingStatus& operator=(const ThinkingStatus&) = delete;

    ~ThinkingStatus() {
        stop();
    }

    void stop() {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) {
            return;
        }

        if (worker_.joinable()) {
            worker_.join();
        }
        clear_line();
    }

private:
    std::string prefix_;
    std::chrono::steady_clock::time_point started_at_;
    std::atomic<bool> running_;
    std::size_t max_width_;
    std::thread worker_;

    void run() {
        while (running_.load()) {
            render();
            for (int i = 0; i < 10 && running_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    void render() {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - started_at_);
        const std::string text = prefix_ + " " + format_elapsed(elapsed);
        max_width_ = std::max(max_width_, text.size());
        std::cout << '\r' << text << std::string(max_width_ - text.size(), ' ');
        std::cout.flush();
    }

    void clear_line() {
        std::cout << '\r' << std::string(max_width_, ' ') << '\r';
        std::cout.flush();
    }
};

class Json {
public:
    enum class Type { Null, Boolean, Number, String, Array, Object };
    using array_t = std::vector<Json>;
    using object_t = std::vector<std::pair<std::string, Json>>;

    Json() : type_(Type::Null), boolean_(false), number_(0.0) {}
    Json(std::nullptr_t) : Json() {}
    Json(bool value) : type_(Type::Boolean), boolean_(value), number_(0.0) {}
    Json(int value) : type_(Type::Number), boolean_(false), number_(static_cast<double>(value)) {}
    Json(long value) : type_(Type::Number), boolean_(false), number_(static_cast<double>(value)) {}
    Json(long long value) : type_(Type::Number), boolean_(false), number_(static_cast<double>(value)) {}
    Json(double value) : type_(Type::Number), boolean_(false), number_(value) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("JSON numbers must be finite");
        }
    }
    Json(const char* value) : type_(Type::String), boolean_(false), number_(0.0), string_(value ? value : "") {}
    Json(std::string value) : type_(Type::String), boolean_(false), number_(0.0), string_(std::move(value)) {}
    Json(array_t value) : type_(Type::Array), boolean_(false), number_(0.0), array_(std::move(value)) {}
    Json(object_t value) : type_(Type::Object), boolean_(false), number_(0.0), object_(std::move(value)) {}

    static Json array() { return Json(array_t{}); }
    static Json array(std::initializer_list<Json> values) { return Json(array_t(values)); }
    static Json array(array_t values) { return Json(std::move(values)); }
    static Json object() { return Json(object_t{}); }
    static Json object(std::initializer_list<std::pair<std::string, Json>> values) { return Json(object_t(values)); }
    static Json object(object_t values) { return Json(std::move(values)); }
    static std::pair<std::string, Json> member(std::string key, Json value) {
        return {std::move(key), std::move(value)};
    }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_boolean() const { return type_ == Type::Boolean; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool as_bool() const {
        require(Type::Boolean, "boolean");
        return boolean_;
    }

    double as_number() const {
        require(Type::Number, "number");
        return number_;
    }

    int as_int() const {
        require(Type::Number, "number");
        if (!detail::is_integer_number(number_)) {
            throw std::runtime_error("JSON number is not a valid int");
        }
        return static_cast<int>(number_);
    }

    const std::string& as_string() const {
        require(Type::String, "string");
        return string_;
    }

    const array_t& as_array() const {
        require(Type::Array, "array");
        return array_;
    }

    array_t& as_array() {
        require(Type::Array, "array");
        return array_;
    }

    const object_t& as_object() const {
        require(Type::Object, "object");
        return object_;
    }

    object_t& as_object() {
        require(Type::Object, "object");
        return object_;
    }

    const Json* find(const std::string& key) const {
        if (type_ != Type::Object) {
            return nullptr;
        }
        for (const auto& item : object_) {
            if (item.first == key) {
                return &item.second;
            }
        }
        return nullptr;
    }

    Json* find(const std::string& key) {
        if (type_ != Type::Object) {
            return nullptr;
        }
        for (auto& item : object_) {
            if (item.first == key) {
                return &item.second;
            }
        }
        return nullptr;
    }

    bool contains(const std::string& key) const {
        return find(key) != nullptr;
    }

    void set(std::string key, Json value) {
        if (type_ != Type::Object) {
            type_ = Type::Object;
            object_.clear();
            array_.clear();
            string_.clear();
        }
        for (auto& item : object_) {
            if (item.first == key) {
                item.second = std::move(value);
                return;
            }
        }
        object_.emplace_back(std::move(key), std::move(value));
    }

    Json& operator[](const std::string& key) {
        if (type_ != Type::Object) {
            type_ = Type::Object;
            object_.clear();
            array_.clear();
            string_.clear();
        }
        if (auto* value = find(key)) {
            return *value;
        }
        object_.emplace_back(key, Json());
        return object_.back().second;
    }

    const Json& at(const std::string& key) const {
        const Json* value = find(key);
        if (!value) {
            throw std::out_of_range("JSON object has no key: " + key);
        }
        return *value;
    }

    std::string dump(int indent = -1) const {
        std::string out;
        dump_into(out, indent, 0);
        return out;
    }

    void write_file(const std::filesystem::path& path, int indent = 4) const {
        detail::write_text_file(path, dump(indent) + "\n");
    }

    static Json parse(const std::string& text) {
        Parser parser(text);
        return parser.parse();
    }

    static Json parse_file(const std::filesystem::path& path) {
        return parse(detail::read_text_file(path));
    }

private:
    class Parser {
    public:
        explicit Parser(const std::string& text) : text_(text), pos_(0) {}

        Json parse() {
            skip_utf8_bom();
            skip_ws();
            Json value = parse_value();
            skip_ws();
            if (pos_ != text_.size()) {
                throw std::runtime_error("unexpected trailing data in JSON");
            }
            return value;
        }

    private:
        const std::string& text_;
        std::size_t pos_;

        void skip_utf8_bom() {
            if (text_.size() >= 3 &&
                static_cast<unsigned char>(text_[0]) == 0xEF &&
                static_cast<unsigned char>(text_[1]) == 0xBB &&
                static_cast<unsigned char>(text_[2]) == 0xBF) {
                pos_ = 3;
            }
        }

        void skip_ws() {
            while (pos_ < text_.size()) {
                unsigned char ch = static_cast<unsigned char>(text_[pos_]);
                if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
                    ++pos_;
                } else {
                    break;
                }
            }
        }

        char peek() const {
            if (pos_ >= text_.size()) {
                throw std::runtime_error("unexpected end of JSON");
            }
            return text_[pos_];
        }

        char consume() {
            char ch = peek();
            ++pos_;
            return ch;
        }

        void expect(char expected) {
            char actual = consume();
            if (actual != expected) {
                std::string message = "expected '";
                message.push_back(expected);
                message += "' in JSON";
                throw std::runtime_error(message);
            }
        }

        Json parse_value() {
            skip_ws();
            char ch = peek();
            if (ch == 'n') {
                parse_literal("null");
                return Json();
            }
            if (ch == 't') {
                parse_literal("true");
                return Json(true);
            }
            if (ch == 'f') {
                parse_literal("false");
                return Json(false);
            }
            if (ch == '"') {
                return Json(parse_string());
            }
            if (ch == '[') {
                return parse_array();
            }
            if (ch == '{') {
                return parse_object();
            }
            if (ch == '-' || (ch >= '0' && ch <= '9')) {
                return parse_number();
            }
            throw std::runtime_error("unexpected token in JSON");
        }

        void parse_literal(const char* literal) {
            for (const char* p = literal; *p; ++p) {
                if (pos_ >= text_.size() || text_[pos_] != *p) {
                    throw std::runtime_error("invalid JSON literal");
                }
                ++pos_;
            }
        }

        Json parse_array() {
            expect('[');
            skip_ws();
            array_t values;
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return Json(std::move(values));
            }
            while (true) {
                values.push_back(parse_value());
                skip_ws();
                char ch = consume();
                if (ch == ']') {
                    break;
                }
                if (ch != ',') {
                    throw std::runtime_error("expected ',' or ']' in JSON array");
                }
            }
            return Json(std::move(values));
        }

        Json parse_object() {
            expect('{');
            skip_ws();
            Json object = Json::object();
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return object;
            }
            while (true) {
                skip_ws();
                if (peek() != '"') {
                    throw std::runtime_error("expected string key in JSON object");
                }
                std::string key = parse_string();
                skip_ws();
                expect(':');
                Json value = parse_value();
                object.set(std::move(key), std::move(value));
                skip_ws();
                char ch = consume();
                if (ch == '}') {
                    break;
                }
                if (ch != ',') {
                    throw std::runtime_error("expected ',' or '}' in JSON object");
                }
            }
            return object;
        }

        Json parse_number() {
            const std::size_t start = pos_;
            if (text_[pos_] == '-') {
                ++pos_;
            }
            if (pos_ >= text_.size()) {
                throw std::runtime_error("invalid JSON number");
            }
            if (text_[pos_] == '0') {
                ++pos_;
            } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
                while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                    ++pos_;
                }
            } else {
                throw std::runtime_error("invalid JSON number");
            }
            if (pos_ < text_.size() && text_[pos_] == '.') {
                ++pos_;
                if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                    throw std::runtime_error("invalid JSON number");
                }
                while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                    ++pos_;
                }
            }
            if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
                ++pos_;
                if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                    ++pos_;
                }
                if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                    throw std::runtime_error("invalid JSON number");
                }
                while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                    ++pos_;
                }
            }
            const std::string token = text_.substr(start, pos_ - start);
            std::size_t parsed = 0;
            double value = 0.0;
            try {
                value = std::stod(token, &parsed);
            } catch (const std::exception&) {
                throw std::runtime_error("invalid JSON number");
            }
            if (parsed != token.size() || !std::isfinite(value)) {
                throw std::runtime_error("invalid JSON number");
            }
            return Json(value);
        }

        std::string parse_string() {
            expect('"');
            std::string out;
            while (pos_ < text_.size()) {
                unsigned char ch = static_cast<unsigned char>(consume());
                if (ch == '"') {
                    return out;
                }
                if (ch < 0x20) {
                    throw std::runtime_error("control character in JSON string");
                }
                if (ch != '\\') {
                    out.push_back(static_cast<char>(ch));
                    continue;
                }
                if (pos_ >= text_.size()) {
                    throw std::runtime_error("invalid JSON escape");
                }
                char escaped = consume();
                switch (escaped) {
                    case '"':
                        out.push_back('"');
                        break;
                    case '\\':
                        out.push_back('\\');
                        break;
                    case '/':
                        out.push_back('/');
                        break;
                    case 'b':
                        out.push_back('\b');
                        break;
                    case 'f':
                        out.push_back('\f');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    case 'u':
                        parse_unicode_escape(out);
                        break;
                    default:
                        throw std::runtime_error("invalid JSON escape");
                }
            }
            throw std::runtime_error("unterminated JSON string");
        }

        unsigned int parse_hex4() {
            unsigned int value = 0;
            for (int i = 0; i < 4; ++i) {
                if (pos_ >= text_.size()) {
                    throw std::runtime_error("invalid unicode escape");
                }
                char ch = consume();
                value <<= 4;
                if (ch >= '0' && ch <= '9') {
                    value += static_cast<unsigned int>(ch - '0');
                } else if (ch >= 'a' && ch <= 'f') {
                    value += static_cast<unsigned int>(10 + ch - 'a');
                } else if (ch >= 'A' && ch <= 'F') {
                    value += static_cast<unsigned int>(10 + ch - 'A');
                } else {
                    throw std::runtime_error("invalid unicode escape");
                }
            }
            return value;
        }

        void parse_unicode_escape(std::string& out) {
            unsigned int code = parse_hex4();
            if (code >= 0xD800 && code <= 0xDBFF) {
                if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                    throw std::runtime_error("invalid unicode surrogate pair");
                }
                pos_ += 2;
                unsigned int low = parse_hex4();
                if (low < 0xDC00 || low > 0xDFFF) {
                    throw std::runtime_error("invalid unicode surrogate pair");
                }
                code = 0x10000 + (((code - 0xD800) << 10) | (low - 0xDC00));
            } else if (code >= 0xDC00 && code <= 0xDFFF) {
                throw std::runtime_error("invalid unicode surrogate pair");
            }
            detail::append_utf8(out, code);
        }
    };

    void require(Type expected, const char* name) const {
        if (type_ != expected) {
            throw std::runtime_error(std::string("JSON value is not a ") + name);
        }
    }

    static std::string escape_string(const std::string& value) {
        std::string out;
        out.reserve(value.size() + 2);
        out.push_back('"');
        for (unsigned char ch : value) {
            switch (ch) {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (ch < 0x20) {
                        std::ostringstream code;
                        code << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                             << static_cast<int>(ch);
                        out += code.str();
                    } else {
                        out.push_back(static_cast<char>(ch));
                    }
                    break;
            }
        }
        out.push_back('"');
        return out;
    }

    static void append_spaces(std::string& out, int count) {
        if (count > 0) {
            out.append(static_cast<std::size_t>(count), ' ');
        }
    }

    void dump_into(std::string& out, int indent, int level) const {
        switch (type_) {
            case Type::Null:
                out += "null";
                break;
            case Type::Boolean:
                out += boolean_ ? "true" : "false";
                break;
            case Type::Number:
                out += detail::format_double(number_);
                break;
            case Type::String:
                out += escape_string(string_);
                break;
            case Type::Array:
                dump_array(out, indent, level);
                break;
            case Type::Object:
                dump_object(out, indent, level);
                break;
        }
    }

    void dump_array(std::string& out, int indent, int level) const {
        if (array_.empty()) {
            out += "[]";
            return;
        }
        out.push_back('[');
        const bool pretty = indent >= 0;
        for (std::size_t i = 0; i < array_.size(); ++i) {
            if (pretty) {
                out.push_back('\n');
                append_spaces(out, level + indent);
            }
            array_[i].dump_into(out, indent, level + indent);
            if (i + 1 < array_.size()) {
                out.push_back(',');
            }
        }
        if (pretty) {
            out.push_back('\n');
            append_spaces(out, level);
        }
        out.push_back(']');
    }

    void dump_object(std::string& out, int indent, int level) const {
        if (object_.empty()) {
            out += "{}";
            return;
        }
        out.push_back('{');
        const bool pretty = indent >= 0;
        for (std::size_t i = 0; i < object_.size(); ++i) {
            if (pretty) {
                out.push_back('\n');
                append_spaces(out, level + indent);
            }
            out += escape_string(object_[i].first);
            out.push_back(':');
            if (pretty) {
                out.push_back(' ');
            }
            object_[i].second.dump_into(out, indent, level + indent);
            if (i + 1 < object_.size()) {
                out.push_back(',');
            }
        }
        if (pretty) {
            out.push_back('\n');
            append_spaces(out, level);
        }
        out.push_back('}');
    }

    Type type_;
    bool boolean_;
    double number_;
    std::string string_;
    array_t array_;
    object_t object_;
};

inline std::vector<std::string> normalize_system_rules(const std::vector<std::string>& rules) {
    std::vector<std::string> out;
    for (const auto& rule : rules) {
        std::string cleaned = detail::trim(rule);
        if (!cleaned.empty()) {
            out.push_back(cleaned);
        }
    }
    return out;
}

inline std::vector<std::string> normalize_system_rules(const std::string& rules) {
    return detail::split_non_empty_lines(rules);
}

inline std::string normalize_username(std::string username) {
    username = detail::trim(std::move(username));
    if (username.empty()) {
        throw std::invalid_argument("username cannot be empty");
    }
    if (username.find('\n') != std::string::npos || username.find('\r') != std::string::npos) {
        throw std::invalid_argument("username cannot contain line breaks");
    }
    return username;
}

inline std::string format_user_content_for_api(const std::string& content, const std::string& username) {
    return username + ": " + content;
}

inline bool is_valid_role(const std::string& role) {
    return role == "system" || role == "developer" || role == "user" || role == "assistant";
}

inline std::string default_username_for_role(const std::string& role) {
    if (role == "system") {
        return SYSTEM_USERNAME;
    }
    if (role == "developer") {
        return DEVELOPER_USERNAME;
    }
    if (role == "user") {
        return DEFAULT_USERNAME;
    }
    if (role == "assistant") {
        return ASSISTANT_USERNAME;
    }
    throw std::invalid_argument("invalid role");
}

struct ChatConfig {
    std::string api_key;
    std::string base_url;
    std::string model = DEFAULT_MODEL;
    std::vector<std::string> system_rules = {DEFAULT_SYSTEM_RULE};
    std::optional<double> temperature;
    std::optional<int> max_completion_tokens;
    std::optional<int> max_history_messages;
    std::optional<double> timeout = DEFAULT_TIMEOUT_SECONDS;
    std::string organization;
    std::string project;
    std::string curl_path = "curl";

    void normalize() {
        system_rules = normalize_system_rules(system_rules);
        if (model.empty()) {
            model = DEFAULT_MODEL;
        }
        curl_path = detail::trim(std::move(curl_path));
        if (curl_path.empty()) {
            curl_path = "curl";
        }
    }

    static ChatConfig from_json(const Json& json) {
        if (!json.is_object()) {
            throw std::runtime_error("config JSON must be an object");
        }

        ChatConfig config;
        for (const auto& item : json.as_object()) {
            const std::string& key = item.first;
            const Json& value = item.second;
            if (value.is_null()) {
                if (key == "system_rules") {
                    config.system_rules.clear();
                    continue;
                }
                if (key == "temperature") {
                    config.temperature.reset();
                    continue;
                }
                if (key == "max_completion_tokens") {
                    config.max_completion_tokens.reset();
                    continue;
                }
                if (key == "max_history_messages") {
                    config.max_history_messages.reset();
                    continue;
                }
                if (key == "timeout") {
                    config.timeout.reset();
                    continue;
                }
                if (key == "curl_path") {
                    config.curl_path.clear();
                    continue;
                }
                continue;
            }

            if (key == "api_key") {
                if (!value.is_string()) {
                    throw std::runtime_error("config file api_key must be a string");
                }
                config.api_key = value.as_string();
            } else if (key == "base_url") {
                if (!value.is_string()) {
                    throw std::runtime_error("config file base_url must be a string");
                }
                config.base_url = value.as_string();
            } else if (key == "model") {
                if (!value.is_string()) {
                    throw std::runtime_error("config file model must be a string");
                }
                config.model = value.as_string();
            } else if (key == "system_rules") {
                if (value.is_string()) {
                    config.system_rules = normalize_system_rules(value.as_string());
                } else if (value.is_array()) {
                    std::vector<std::string> rules;
                    for (const auto& rule : value.as_array()) {
                        if (!rule.is_string()) {
                            throw std::runtime_error("config file system_rules must contain only strings");
                        }
                        rules.push_back(rule.as_string());
                    }
                    config.system_rules = normalize_system_rules(rules);
                } else {
                    throw std::runtime_error("config file system_rules must be a string, an array of strings, or null");
                }
            } else if (key == "temperature") {
                if (!value.is_number()) {
                    throw std::runtime_error("config file temperature must be a number");
                }
                config.temperature = value.as_number();
            } else if (key == "max_completion_tokens") {
                config.max_completion_tokens = value.as_int();
            } else if (key == "max_history_messages") {
                config.max_history_messages = value.as_int();
            } else if (key == "timeout") {
                if (!value.is_number()) {
                    throw std::runtime_error("config file timeout must be a number");
                }
                config.timeout = value.as_number();
            } else if (key == "organization") {
                if (!value.is_string()) {
                    throw std::runtime_error("config file organization must be a string");
                }
                config.organization = value.as_string();
            } else if (key == "project") {
                if (!value.is_string()) {
                    throw std::runtime_error("config file project must be a string");
                }
                config.project = value.as_string();
            } else if (key == "curl_path") {
                if (!value.is_string()) {
                    throw std::runtime_error("config file curl_path must be a string");
                }
                config.curl_path = value.as_string();
            } else {
                throw std::runtime_error("config file has unsupported key: " + key);
            }
        }
        config.normalize();
        return config;
    }

    static ChatConfig from_json_file(const std::filesystem::path& path) {
        return from_json(Json::parse_file(path));
    }
};

struct Message {
    std::string role;
    std::string content;
    std::optional<std::string> username;

    Message() = default;
    Message(std::string role_value, std::string content_value, std::optional<std::string> username_value = std::nullopt)
        : role(std::move(role_value)), content(std::move(content_value)), username(std::move(username_value)) {}

    Json to_chat_message() const {
        std::string api_content = content;
        if (role == "user") {
            api_content = format_user_content_for_api(content, username.value_or(DEFAULT_USERNAME));
        }
        return Json::object({
            Json::member("role", role),
            Json::member("content", api_content),
        });
    }

    Json to_history() const {
        Json::object_t object;
        object.emplace_back("role", role);
        object.emplace_back("content", content);
        if (username.has_value()) {
            object.emplace_back("username", *username);
        }
        return Json(std::move(object));
    }
};

struct HttpRequest {
    std::string method = "POST";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::optional<double> timeout_seconds;
};

struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::string error;
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;

    virtual bool cancel_current_request() {
        return false;
    }
};

class FunctionTransport : public Transport {
public:
    using Callback = std::function<HttpResponse(const HttpRequest&)>;

    explicit FunctionTransport(Callback callback) : callback_(std::move(callback)) {
        if (!callback_) {
            throw std::invalid_argument("transport callback cannot be empty");
        }
    }

    HttpResponse send(const HttpRequest& request) override {
        return callback_(request);
    }

private:
    Callback callback_;
};

class CurlTransport : public Transport {
public:
    explicit CurlTransport(std::string curl_executable = "curl") : curl_executable_(std::move(curl_executable)) {}

    CurlTransport(const CurlTransport& other) {
        std::lock_guard<std::mutex> lock(other.active_mutex_);
        curl_executable_ = other.curl_executable_;
    }

    CurlTransport& operator=(const CurlTransport& other) {
        if (this == &other) {
            return *this;
        }

        std::scoped_lock lock(active_mutex_, other.active_mutex_);
        curl_executable_ = other.curl_executable_;
        active_state_.reset();
        return *this;
    }

    CurlTransport(CurlTransport&& other) {
        std::lock_guard<std::mutex> lock(other.active_mutex_);
        curl_executable_ = std::move(other.curl_executable_);
    }

    CurlTransport& operator=(CurlTransport&& other) {
        if (this == &other) {
            return *this;
        }

        std::scoped_lock lock(active_mutex_, other.active_mutex_);
        curl_executable_ = std::move(other.curl_executable_);
        active_state_.reset();
        return *this;
    }

    const std::string& curl_executable() const {
        return curl_executable_;
    }

    bool cancel_current_request() override {
        std::shared_ptr<ProcessState> state;
        {
            std::lock_guard<std::mutex> lock(active_mutex_);
            state = active_state_;
        }
        if (!state) {
            return false;
        }
        return state->cancel();
    }

    HttpResponse send(const HttpRequest& request) override {
        if (request.method != "POST") {
            throw ChatBotError("CurlTransport currently supports only POST requests");
        }

        auto state = std::make_shared<ProcessState>();
        ActiveRequestScope active_request(*this, state);

        detail::TempFile request_file(detail::unique_temp_path("codex_chat_bot_request_", ".json"));
        detail::TempFile response_file(detail::unique_temp_path("codex_chat_bot_response_", ".json"));
        detail::TempFile code_file(detail::unique_temp_path("codex_chat_bot_status_", ".txt"));
        detail::TempFile error_file(detail::unique_temp_path("codex_chat_bot_error_", ".txt"));
        detail::TempFile config_file(detail::unique_temp_path("codex_chat_bot_curl_", ".conf"));

        detail::write_text_file(request_file.path(), request.body);
        detail::write_text_file(config_file.path(), build_curl_config(request, request_file.path(), response_file.path()));

        throw_if_canceled(state);

        int result = run_curl_process(config_file.path(), code_file.path(), error_file.path(), state);
        throw_if_canceled(state);

        HttpResponse response;
        response.body = read_if_exists(response_file.path());
        response.error = read_if_exists(error_file.path());

        std::string status_text = detail::trim(read_if_exists(code_file.path()));
        if (!status_text.empty()) {
            try {
                response.status_code = std::stoi(status_text);
            } catch (const std::exception&) {
                response.status_code = 0;
            }
        }

        if (result != 0 && response.error.empty()) {
            response.error = "curl command failed with exit code " + std::to_string(result);
        }
        return response;
    }

private:
    class ProcessState {
    public:
        bool cancel() {
            std::lock_guard<std::mutex> lock(mutex_);
            cancel_requested_ = true;
#ifdef _WIN32
            if (process_ != nullptr) {
                TerminateProcess(process_, kCanceledExitCode);
                return true;
            }
#elif CODEX_CHAT_BOT_HAS_TERMIOS
            if (pid_ > 0) {
                ::kill(pid_, SIGTERM);
                return true;
            }
#endif
            return true;
        }

        bool canceled() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return cancel_requested_;
        }

#ifdef _WIN32
        void mark_started(HANDLE process) {
            std::lock_guard<std::mutex> lock(mutex_);
            process_ = process;
            if (cancel_requested_ && process_ != nullptr) {
                TerminateProcess(process_, kCanceledExitCode);
            }
        }

        void mark_finished() {
            std::lock_guard<std::mutex> lock(mutex_);
            process_ = nullptr;
        }
#elif CODEX_CHAT_BOT_HAS_TERMIOS
        void mark_started(pid_t pid) {
            std::lock_guard<std::mutex> lock(mutex_);
            pid_ = pid;
            if (cancel_requested_ && pid_ > 0) {
                ::kill(pid_, SIGTERM);
            }
        }

        void mark_finished() {
            std::lock_guard<std::mutex> lock(mutex_);
            pid_ = -1;
        }
#else
        void mark_started() {}
        void mark_finished() {}
#endif

    private:
#ifdef _WIN32
        static constexpr UINT kCanceledExitCode = 1223;
#endif
        mutable std::mutex mutex_;
        bool cancel_requested_ = false;
#ifdef _WIN32
        HANDLE process_ = nullptr;
#elif CODEX_CHAT_BOT_HAS_TERMIOS
        pid_t pid_ = -1;
#endif
    };

    class ActiveRequestScope {
    public:
        ActiveRequestScope(CurlTransport& transport, std::shared_ptr<ProcessState> state)
            : transport_(transport), state_(std::move(state)) {
            std::lock_guard<std::mutex> lock(transport_.active_mutex_);
            transport_.active_state_ = state_;
        }

        ActiveRequestScope(const ActiveRequestScope&) = delete;
        ActiveRequestScope& operator=(const ActiveRequestScope&) = delete;

        ~ActiveRequestScope() {
            std::lock_guard<std::mutex> lock(transport_.active_mutex_);
            if (transport_.active_state_ == state_) {
                transport_.active_state_.reset();
            }
        }

    private:
        CurlTransport& transport_;
        std::shared_ptr<ProcessState> state_;
    };

    std::string curl_executable_;
    mutable std::mutex active_mutex_;
    std::shared_ptr<ProcessState> active_state_;

    static void throw_if_canceled(const std::shared_ptr<ProcessState>& state) {
        if (state && state->canceled()) {
            throw RequestCanceledError("chat completion request canceled");
        }
    }

    static std::string read_if_exists(const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return {};
        }
        try {
            return detail::read_text_file(path);
        } catch (const std::exception&) {
            return {};
        }
    }

    static std::string build_curl_config(
        const HttpRequest& request,
        const std::filesystem::path& request_file,
        const std::filesystem::path& response_file
    ) {
        std::ostringstream config;
        config << "request = " << detail::curl_config_quote(request.method) << "\n";
        config << "url = " << detail::curl_config_quote(request.url) << "\n";
        config << "location\n";
        config << "silent\n";
        config << "show-error\n";
        config << "data-binary = "
               << detail::curl_config_quote("@" + detail::path_for_curl_config(request_file)) << "\n";
        config << "output = " << detail::curl_config_quote(detail::path_for_curl_config(response_file)) << "\n";
        config << "write-out = " << detail::curl_config_quote("%{http_code}") << "\n";
        if (request.timeout_seconds.has_value() && *request.timeout_seconds > 0) {
            config << "max-time = " << detail::curl_config_quote(detail::format_double(*request.timeout_seconds)) << "\n";
        }
        for (const auto& header : request.headers) {
            config << "header = " << detail::curl_config_quote(header.first + ": " + header.second) << "\n";
        }
        return config.str();
    }

#ifdef _WIN32
    int run_curl_process(
        const std::filesystem::path& config_file,
        const std::filesystem::path& code_file,
        const std::filesystem::path& error_file,
        const std::shared_ptr<ProcessState>& state
    ) const {
        detail::WindowsHandle code_handle = detail::open_windows_output_handle(code_file);
        detail::WindowsHandle error_handle = detail::open_windows_output_handle(error_file);
        detail::WindowsHandle input_handle = open_windows_null_input_handle();

        STARTUPINFOW startup;
        std::memset(&startup, 0, sizeof(startup));
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input_handle.get();
        startup.hStdOutput = code_handle.get();
        startup.hStdError = error_handle.get();

        PROCESS_INFORMATION process_info;
        std::memset(&process_info, 0, sizeof(process_info));

        std::wstring command_line = detail::windows_quote_argument(std::filesystem::path(curl_executable_).wstring()) +
                                    L" --config " + detail::windows_quote_argument(config_file.wstring());
        std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
        mutable_command.push_back(L'\0');

        if (!CreateProcessW(
                nullptr,
                mutable_command.data(),
                nullptr,
                nullptr,
                TRUE,
                0,
                nullptr,
                nullptr,
                &startup,
                &process_info)) {
            throw ChatBotError("could not launch curl: " + detail::windows_error_message(GetLastError()));
        }

        detail::WindowsHandle process_handle(process_info.hProcess);
        detail::WindowsHandle thread_handle(process_info.hThread);
        state->mark_started(process_handle.get());
        WaitForSingleObject(process_handle.get(), INFINITE);

        DWORD exit_code = 1;
        if (!GetExitCodeProcess(process_handle.get(), &exit_code)) {
            exit_code = 1;
        }
        state->mark_finished();
        return static_cast<int>(exit_code);
    }

    static detail::WindowsHandle open_windows_null_input_handle() {
        SECURITY_ATTRIBUTES attributes;
        attributes.nLength = sizeof(attributes);
        attributes.lpSecurityDescriptor = nullptr;
        attributes.bInheritHandle = TRUE;

        HANDLE handle = CreateFileW(
            L"NUL",
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &attributes,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("could not open NUL for curl stdin: " +
                                     detail::windows_error_message(GetLastError()));
        }
        return detail::WindowsHandle(handle);
    }
#elif CODEX_CHAT_BOT_HAS_TERMIOS
    int run_curl_process(
        const std::filesystem::path& config_file,
        const std::filesystem::path& code_file,
        const std::filesystem::path& error_file,
        const std::shared_ptr<ProcessState>& state
    ) const {
        detail::PosixFd code_fd = detail::open_posix_output_fd(code_file);
        detail::PosixFd error_fd = detail::open_posix_output_fd(error_file);
        std::string executable = curl_executable_;
        std::string config_path = config_file.string();

        pid_t pid = ::fork();
        if (pid < 0) {
            throw ChatBotError("could not launch curl: " + detail::errno_message(errno));
        }

        if (pid == 0) {
            if (::dup2(code_fd.get(), STDOUT_FILENO) < 0 || ::dup2(error_fd.get(), STDERR_FILENO) < 0) {
                _exit(126);
            }

            code_fd.reset();
            error_fd.reset();

            char* const args[] = {
                const_cast<char*>(executable.c_str()),
                const_cast<char*>("--config"),
                const_cast<char*>(config_path.c_str()),
                nullptr,
            };
            ::execvp(args[0], args);

            const char message[] = "could not execute curl\n";
            ::write(STDERR_FILENO, message, sizeof(message) - 1);
            _exit(127);
        }

        state->mark_started(pid);

        int status = 0;
        while (true) {
            pid_t waited = ::waitpid(pid, &status, 0);
            if (waited == pid) {
                break;
            }
            if (waited < 0 && errno == EINTR) {
                continue;
            }
            state->mark_finished();
            throw ChatBotError("could not wait for curl: " + detail::errno_message(errno));
        }

        state->mark_finished();
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status)) {
            return 128 + WTERMSIG(status);
        }
        return 1;
    }
#else
    int run_curl_process(
        const std::filesystem::path& config_file,
        const std::filesystem::path& code_file,
        const std::filesystem::path& error_file,
        const std::shared_ptr<ProcessState>& state
    ) const {
        state->mark_started();
        std::string command = detail::shell_command_executable(curl_executable_) + " --config " +
                              detail::shell_quote(config_file.string()) + " > " +
                              detail::shell_quote(code_file.string()) + " 2> " +
                              detail::shell_quote(error_file.string());
        int result = std::system(command.c_str());
        state->mark_finished();
        return result;
    }
#endif
};

struct ChatResponse {
    std::string text;
    Json raw;
    std::vector<Message> messages;
};

namespace detail {

inline std::string chat_completions_url(std::string base_url) {
    base_url = trim(std::move(base_url));
    while (!base_url.empty() && base_url.back() == '/') {
        base_url.pop_back();
    }
    const std::string suffix = "/chat/completions";
    if (base_url.size() >= suffix.size() &&
        base_url.compare(base_url.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return base_url;
    }
    return base_url + suffix;
}

inline Json message_array_json(const std::vector<Message>& messages) {
    Json::array_t array;
    array.reserve(messages.size());
    for (const auto& message : messages) {
        array.push_back(message.to_chat_message());
    }
    return Json(std::move(array));
}

inline bool json_is_non_empty(const Json& value) {
    if (value.is_null()) {
        return false;
    }
    if (value.is_string() && value.as_string().empty()) {
        return false;
    }
    return true;
}

inline std::optional<Json> find_non_empty_value(const Json& value, const std::string& key) {
    if (value.is_object()) {
        if (const Json* direct = value.find(key)) {
            if (json_is_non_empty(*direct)) {
                return *direct;
            }
        }
        for (const auto& item : value.as_object()) {
            auto found = find_non_empty_value(item.second, key);
            if (found.has_value()) {
                return found;
            }
        }
    } else if (value.is_array()) {
        for (const auto& item : value.as_array()) {
            auto found = find_non_empty_value(item, key);
            if (found.has_value()) {
                return found;
            }
        }
    }
    return std::nullopt;
}

inline std::string extract_response_error_json(const Json& response) {
    if (const Json* error = response.find("error")) {
        return Json::object({Json::member("error", *error)}).dump(4);
    }
    return {};
}

inline std::string extract_response_text(const Json& response) {
    std::string error_json = extract_response_error_json(response);
    if (!error_json.empty()) {
        return error_json;
    }

    std::vector<std::string> chunks;
    std::vector<std::string> refusals;

    const Json* choices = response.find("choices");
    if (choices && choices->is_array()) {
        for (const auto& choice : choices->as_array()) {
            const Json* message = choice.find("message");
            if (!message || !message->is_object()) {
                continue;
            }

            const Json* content = message->find("content");
            if (content && content->is_string()) {
                chunks.push_back(content->as_string());
            } else if (content && content->is_array()) {
                for (const auto& part : content->as_array()) {
                    const Json* text = part.find("text");
                    if (text && text->is_string()) {
                        chunks.push_back(text->as_string());
                    }
                }
            }

            const Json* refusal = message->find("refusal");
            if (refusal && refusal->is_string()) {
                refusals.push_back(refusal->as_string());
            }
        }
    }

    if (!chunks.empty()) {
        std::string out;
        for (const auto& chunk : chunks) {
            out += chunk;
        }
        return out;
    }
    if (!refusals.empty()) {
        std::string out;
        for (const auto& refusal : refusals) {
            out += refusal;
        }
        return out;
    }

    throw ResponseTextError("could not extract text from the chat completion");
}

inline std::optional<std::string> extract_finish_reason_text(const Json& response) {
    auto finish_reason = find_non_empty_value(response, "finish_reason");
    if (!finish_reason.has_value()) {
        return std::nullopt;
    }
    if (finish_reason->is_string()) {
        return finish_reason->as_string();
    }
    return Json::object({Json::member("finish_reason", *finish_reason)}).dump(4);
}

inline void warn_empty_response_retry() {
    std::cerr << "\033[1;33m"
              << "Warning: The robot returned an empty string, and is currently retrying."
              << "\033[0m" << std::endl;
}

inline std::vector<Message> latest_messages(const std::vector<Message>& messages, int max_messages) {
    if (max_messages <= 0) {
        return messages;
    }

    std::vector<std::size_t> non_system_indexes;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].role != "system") {
            non_system_indexes.push_back(i);
        }
    }

    const std::size_t keep_from =
        non_system_indexes.size() > static_cast<std::size_t>(max_messages)
            ? non_system_indexes.size() - static_cast<std::size_t>(max_messages)
            : 0;

    std::vector<Message> kept;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        bool keep = messages[i].role == "system";
        if (!keep) {
            keep = std::find(non_system_indexes.begin() + static_cast<std::ptrdiff_t>(keep_from),
                             non_system_indexes.end(), i) != non_system_indexes.end();
        }
        if (keep) {
            kept.push_back(messages[i]);
        }
    }
    return kept;
}

inline std::string default_username_or_validate(const Json& item, int index, const std::string& role) {
    const Json* username = item.find("username");
    if (!username) {
        return default_username_for_role(role);
    }
    if (!username->is_string()) {
        throw std::runtime_error("chat history message " + std::to_string(index) + " username must be a string");
    }
    try {
        return normalize_username(username->as_string());
    } catch (const std::exception& error) {
        throw std::runtime_error("chat history message " + std::to_string(index) + " " + error.what());
    }
}

inline std::vector<Message> messages_from_history_payload(const Json& payload) {
    const Json::array_t* raw_messages = nullptr;
    if (payload.is_array()) {
        raw_messages = &payload.as_array();
    } else if (payload.is_object()) {
        const Json* messages = payload.find("messages");
        if (messages && messages->is_array()) {
            raw_messages = &messages->as_array();
        }
    }
    if (!raw_messages) {
        throw std::runtime_error("chat history JSON must be an object with a messages array");
    }

    std::vector<Message> messages;
    for (std::size_t i = 0; i < raw_messages->size(); ++i) {
        const Json& item = (*raw_messages)[i];
        if (!item.is_object()) {
            throw std::runtime_error("chat history message " + std::to_string(i) + " must be an object");
        }

        const Json* role_json = item.find("role");
        const Json* content_json = item.find("content");
        if (!role_json || !role_json->is_string() || !is_valid_role(role_json->as_string())) {
            throw std::runtime_error("chat history message " + std::to_string(i) + " has an invalid role");
        }
        if (!content_json || !content_json->is_string()) {
            throw std::runtime_error("chat history message " + std::to_string(i) + " content must be a string");
        }

        std::string role = role_json->as_string();
        std::string username = default_username_or_validate(item, static_cast<int>(i), role);
        messages.emplace_back(std::move(role), content_json->as_string(), std::move(username));
    }
    return messages;
}

inline void write_empty_history(const std::filesystem::path& path) {
    Json::array_t messages;
    Json payload = Json::object({Json::member("messages", Json(std::move(messages)))});
    detail::write_text_file(path, payload.dump(4) + "\n");
}

}  // namespace detail

class ChatSession {
public:
    explicit ChatSession(ChatConfig config = ChatConfig(), std::shared_ptr<Transport> transport = nullptr)
        : config_(std::move(config)), transport_(std::move(transport)) {
        config_.normalize();
        reset();
    }

    ChatSession(ChatConfig config, FunctionTransport::Callback callback)
        : ChatSession(std::move(config), std::make_shared<FunctionTransport>(std::move(callback))) {}

    ChatSession(const ChatSession& other)
        : config_(other.config_),
          transport_(other.current_transport()),
          messages_(other.messages_),
          history_path_(other.history_path_) {}

    ChatSession& operator=(const ChatSession& other) {
        if (this == &other) {
            return *this;
        }

        std::shared_ptr<Transport> copied_transport = other.current_transport();
        config_ = other.config_;
        messages_ = other.messages_;
        history_path_ = other.history_path_;
        {
            std::lock_guard<std::mutex> lock(transport_mutex_);
            transport_ = std::move(copied_transport);
        }
        return *this;
    }

    ChatSession(ChatSession&& other)
        : config_(std::move(other.config_)),
          messages_(std::move(other.messages_)),
          history_path_(std::move(other.history_path_)) {
        std::lock_guard<std::mutex> lock(other.transport_mutex_);
        transport_ = std::move(other.transport_);
    }

    ChatSession& operator=(ChatSession&& other) {
        if (this == &other) {
            return *this;
        }

        std::shared_ptr<Transport> moved_transport;
        {
            std::lock_guard<std::mutex> lock(other.transport_mutex_);
            moved_transport = std::move(other.transport_);
        }
        config_ = std::move(other.config_);
        messages_ = std::move(other.messages_);
        history_path_ = std::move(other.history_path_);
        {
            std::lock_guard<std::mutex> lock(transport_mutex_);
            transport_ = std::move(moved_transport);
        }
        return *this;
    }

    const ChatConfig& config() const { return config_; }
    ChatConfig& config() { return config_; }

    void set_transport(std::shared_ptr<Transport> transport) {
        std::lock_guard<std::mutex> lock(transport_mutex_);
        transport_ = std::move(transport);
    }

    void set_transport(FunctionTransport::Callback callback) {
        set_transport(std::make_shared<FunctionTransport>(std::move(callback)));
    }

    bool cancel_current_request() {
        std::shared_ptr<Transport> transport = current_transport();
        if (!transport) {
            return false;
        }
        return transport->cancel_current_request();
    }

    const std::vector<Message>& messages() const {
        return messages_;
    }

    std::string export_history_json() const {
        Json::array_t messages;
        messages.reserve(messages_.size());
        for (const auto& message : messages_) {
            messages.push_back(message.to_history());
        }
        return Json::object({Json::member("messages", Json(std::move(messages)))}).dump(4);
    }

    void save_history(const std::filesystem::path& path) const {
        detail::write_text_file(path, export_history_json() + "\n");
    }

    bool bind_history(const std::filesystem::path& path, bool reset_on_invalid = false) {
        std::optional<std::filesystem::path> previous = history_path_;
        history_path_.reset();

        if (!std::filesystem::exists(path)) {
            try {
                detail::write_empty_history(path);
            } catch (...) {
                history_path_ = previous;
                throw;
            }
            history_path_ = path;
            return false;
        }

        try {
            load_history(path);
        } catch (const std::runtime_error&) {
            history_path_ = previous;
            if (!reset_on_invalid) {
                throw;
            }
            reset();
            try {
                detail::write_empty_history(path);
            } catch (...) {
                history_path_ = previous;
                throw;
            }
            history_path_ = path;
            return true;
        } catch (...) {
            history_path_ = previous;
            throw;
        }

        history_path_ = path;
        try {
            save_bound_history();
        } catch (...) {
            history_path_ = previous;
            throw;
        }
        return false;
    }

    void load_history_json(const std::string& data) {
        Json payload;
        try {
            payload = Json::parse(data);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string("chat history must be valid JSON: ") + error.what());
        }
        messages_ = detail::messages_from_history_payload(payload);
        trim_history();
        save_bound_history();
    }

    void load_history(const std::filesystem::path& path) {
        load_history_json(detail::read_text_file(path));
    }

    void reset(std::optional<std::vector<std::string>> system_rules = std::nullopt) {
        messages_.clear();
        append_system_message(messages_, system_rules);
        save_bound_history();
    }

    void refresh_system_rules(std::optional<std::vector<std::string>> system_rules = std::nullopt) {
        std::vector<Message> updated;
        append_system_message(updated, system_rules);

        std::size_t first_non_system = 0;
        while (first_non_system < messages_.size() && messages_[first_non_system].role == "system") {
            ++first_non_system;
        }

        updated.insert(updated.end(),
                       messages_.begin() + static_cast<std::ptrdiff_t>(first_non_system),
                       messages_.end());
        messages_ = std::move(updated);
        trim_history();
        save_bound_history();
    }

    void add_message(Message message) {
        validate_message(message);
        if (message.username.has_value()) {
            message.username = normalize_username(*message.username);
        } else {
            message.username = default_username_for_role(message.role);
        }
        messages_.push_back(std::move(message));
        trim_history();
        save_bound_history();
    }

    void insert_msg(Message message) {
        if (message.role != "user" && message.role != "assistant") {
            throw std::invalid_argument("insert_msg role must be user or assistant");
        }
        std::string clean_content = detail::trim(message.content);
        if (clean_content.empty()) {
            throw std::invalid_argument("insert_msg content cannot be empty");
        }

        message.content = std::move(clean_content);
        if (message.username.has_value()) {
            message.username = normalize_username(*message.username);
        } else {
            message.username = default_username_for_role(message.role);
        }
        messages_.push_back(std::move(message));
        trim_history();
        save_bound_history();
    }

    void insert_msg(
        const std::string& role,
        const std::string& content,
        const std::string& username = std::string()
    ) {
        std::optional<std::string> clean_username;
        if (!username.empty()) {
            clean_username = username;
        }
        insert_msg(Message(role, content, clean_username));
    }

    std::string ask(const std::string& message, const std::string& username = DEFAULT_USERNAME, const Json& extra_request_args = Json::object()) {
        return send(message, username, extra_request_args).text;
    }

    ChatResponse send(const std::string& message, const std::string& username = DEFAULT_USERNAME, const Json& extra_request_args = Json::object()) {
        std::string clean_message = detail::trim(message);
        if (clean_message.empty()) {
            throw std::invalid_argument("message cannot be empty");
        }
        std::string clean_username = normalize_username(username);

        messages_.emplace_back("user", clean_message, clean_username);

        Json raw;
        std::string text;
        try {
            auto result = create_response_until_text(extra_request_args);
            raw = std::move(result.first);
            text = std::move(result.second);
        } catch (...) {
            messages_.pop_back();
            try {
                save_bound_history();
            } catch (...) {
            }
            throw;
        }

        messages_.emplace_back("assistant", text, std::string(ASSISTANT_USERNAME));
        trim_history();
        save_bound_history();
        return ChatResponse{text, std::move(raw), messages_};
    }

private:
    ChatConfig config_;
    std::shared_ptr<Transport> transport_;
    mutable std::mutex transport_mutex_;
    std::vector<Message> messages_;
    std::optional<std::filesystem::path> history_path_;

    void validate_message(const Message& message) const {
        if (!is_valid_role(message.role)) {
            throw std::invalid_argument("invalid message role");
        }
        if (message.role.empty()) {
            throw std::invalid_argument("message role cannot be empty");
        }
    }

    void ensure_transport() {
        std::lock_guard<std::mutex> lock(transport_mutex_);
        if (!transport_) {
            if (config_.api_key.empty()) {
                throw MissingAPIKeyError("missing API key; set ChatConfig::api_key before sending a message");
            }
            if (config_.base_url.empty()) {
                throw MissingBaseURLError("missing API base URL; set ChatConfig::base_url before sending a message");
            }
            transport_ = std::make_shared<CurlTransport>(config_.curl_path);
        }
    }

    std::shared_ptr<Transport> current_transport() const {
        std::lock_guard<std::mutex> lock(transport_mutex_);
        return transport_;
    }

    std::pair<Json, std::string> create_response_until_text(const Json& extra_request_args) {
        while (true) {
            Json response = create_response(extra_request_args);
            std::string text = detail::extract_response_text(response);
            if (!text.empty()) {
                return {std::move(response), std::move(text)};
            }
            auto finish_reason = detail::extract_finish_reason_text(response);
            if (finish_reason.has_value()) {
                return {std::move(response), *finish_reason};
            }
            detail::warn_empty_response_retry();
        }
    }

    Json create_response(const Json& extra_request_args) {
        ensure_transport();

        HttpRequest request;
        request.method = "POST";
        request.url = detail::chat_completions_url(config_.base_url);
        request.timeout_seconds = config_.timeout;
        request.headers.emplace_back("Content-Type", "application/json");
        if (!config_.api_key.empty()) {
            request.headers.emplace_back("Authorization", "Bearer " + config_.api_key);
        }
        if (!config_.organization.empty()) {
            request.headers.emplace_back("OpenAI-Organization", config_.organization);
        }
        if (!config_.project.empty()) {
            request.headers.emplace_back("OpenAI-Project", config_.project);
        }
        request.body = request_payload(extra_request_args).dump();

        std::shared_ptr<Transport> transport = current_transport();
        if (!transport) {
            throw ChatBotError("missing HTTP transport");
        }

        HttpResponse response = transport->send(request);
        if (response.status_code == 0) {
            std::string message = "chat completion request failed";
            if (!response.error.empty()) {
                message += ": " + detail::trim(response.error);
            }
            throw ChatBotError(message);
        }
        if (response.status_code < 200 || response.status_code >= 300) {
            std::string message = "chat completion request failed with HTTP status " + std::to_string(response.status_code);
            if (!response.body.empty()) {
                message += ": " + detail::trim(response.body);
            } else if (!response.error.empty()) {
                message += ": " + detail::trim(response.error);
            }
            throw ChatBotError(message);
        }

        try {
            return Json::parse(response.body);
        } catch (const std::exception& error) {
            throw ResponseTextError(std::string("chat completion response must be valid JSON: ") + error.what());
        }
    }

    Json request_payload(const Json& extra_request_args) const {
        if (!extra_request_args.is_object()) {
            throw std::invalid_argument("extra_request_args must be a JSON object");
        }

        Json payload = Json::object();
        payload.set("model", config_.model);
        payload.set("messages", detail::message_array_json(detail::latest_messages(messages_, MAX_REQUEST_HISTORY_MESSAGES)));
        if (config_.temperature.has_value()) {
            payload.set("temperature", *config_.temperature);
        }
        if (config_.max_completion_tokens.has_value()) {
            payload.set("max_completion_tokens", *config_.max_completion_tokens);
        }
        for (const auto& item : extra_request_args.as_object()) {
            payload.set(item.first, item.second);
        }
        return payload;
    }

    std::vector<std::string> build_system_rules(const std::optional<std::vector<std::string>>& system_rules) const {
        if (system_rules.has_value()) {
            return normalize_system_rules(*system_rules);
        }
        return normalize_system_rules(config_.system_rules);
    }

    std::string build_system_content(const std::optional<std::vector<std::string>>& system_rules) const {
        std::vector<std::string> rules = build_system_rules(system_rules);
        if (rules.empty()) {
            return {};
        }
        if (rules.size() == 1) {
            return rules.front();
        }

        std::ostringstream out;
        out << "Follow all of these system rules. Later rules are equally important and must not be ignored:\n";
        for (std::size_t i = 0; i < rules.size(); ++i) {
            out << (i + 1) << ". " << rules[i];
            if (i + 1 < rules.size()) {
                out << '\n';
            }
        }
        return out.str();
    }

    void append_system_message(std::vector<Message>& target, const std::optional<std::vector<std::string>>& system_rules) const {
        std::string system_content = build_system_content(system_rules);
        if (!system_content.empty()) {
            target.emplace_back("system", std::move(system_content), std::string(SYSTEM_USERNAME));
        }
    }

    void trim_history() {
        if (!config_.max_history_messages.has_value() || *config_.max_history_messages <= 0) {
            return;
        }

        std::vector<Message> prefix;
        std::size_t first_rest = 0;
        while (first_rest < messages_.size() &&
               (messages_[first_rest].role == "system" || messages_[first_rest].role == "developer")) {
            prefix.push_back(messages_[first_rest]);
            ++first_rest;
        }

        std::vector<Message> rest(messages_.begin() + static_cast<std::ptrdiff_t>(first_rest), messages_.end());
        const std::size_t max_messages = static_cast<std::size_t>(*config_.max_history_messages);
        if (rest.size() > max_messages) {
            rest.erase(rest.begin(), rest.end() - static_cast<std::ptrdiff_t>(max_messages));
        }

        messages_ = std::move(prefix);
        messages_.insert(messages_.end(), rest.begin(), rest.end());
    }

    void save_bound_history() const {
        if (history_path_.has_value()) {
            save_history(*history_path_);
        }
    }
};

}  // namespace codex_chat_bot

#endif  // CODEX_CHAT_BOT_HPP
