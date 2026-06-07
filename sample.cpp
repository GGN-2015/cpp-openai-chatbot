#include "codex_chat_bot.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
class ConsoleUtf8Guard {
public:
    ConsoleUtf8Guard()
        : old_input_cp_(GetConsoleCP()),
          old_output_cp_(GetConsoleOutputCP()),
          changed_input_(SetConsoleCP(CP_UTF8) != 0),
          changed_output_(SetConsoleOutputCP(CP_UTF8) != 0) {}

    ConsoleUtf8Guard(const ConsoleUtf8Guard&) = delete;
    ConsoleUtf8Guard& operator=(const ConsoleUtf8Guard&) = delete;

    ~ConsoleUtf8Guard() {
        if (changed_input_) {
            SetConsoleCP(old_input_cp_);
        }
        if (changed_output_) {
            SetConsoleOutputCP(old_output_cp_);
        }
    }

private:
    UINT old_input_cp_;
    UINT old_output_cp_;
    bool changed_input_;
    bool changed_output_;
};
#endif

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string read_line(const std::string& prompt) {
    std::cout << prompt;
    std::string value;
    std::getline(std::cin, value);
    return trim(value);
}

std::string read_secret(const std::string& prompt) {
    std::cout << prompt;

#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD old_mode = 0;
    if (input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &old_mode)) {
        std::string value;
        std::getline(std::cin, value);
        return trim(value);
    }

    DWORD new_mode = old_mode & ~ENABLE_ECHO_INPUT;
    SetConsoleMode(input, new_mode);
    std::string value;
    std::getline(std::cin, value);
    SetConsoleMode(input, old_mode);
    std::cout << "\n";
    return trim(value);
#elif defined(__unix__) || defined(__APPLE__)
    termios old_term{};
    if (tcgetattr(STDIN_FILENO, &old_term) != 0) {
        std::string value;
        std::getline(std::cin, value);
        return trim(value);
    }

    termios new_term = old_term;
    new_term.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
    std::string value;
    std::getline(std::cin, value);
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    std::cout << "\n";
    return trim(value);
#else
    std::string value;
    std::getline(std::cin, value);
    return trim(value);
#endif
}

std::string format_elapsed(std::chrono::seconds elapsed) {
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

}  // namespace

int main() {
    try {
#ifdef _WIN32
        ConsoleUtf8Guard console_utf8;
#endif

        std::cout << "codex_chat_bot sample\n";
        std::cout << "Type /exit to quit, /reset to clear the local session.\n\n";

        std::string base_url = read_line("Base URL, for example https://api.openai.com/v1: ");
        while (base_url.empty()) {
            base_url = read_line("Base URL cannot be empty: ");
        }

        std::string api_key = read_secret("API key: ");
        while (api_key.empty()) {
            api_key = read_secret("API key cannot be empty: ");
        }

        std::string model = read_line("Model [gpt-5.5]: ");
        if (model.empty()) {
            model = codex_chat_bot::DEFAULT_MODEL;
        }

        codex_chat_bot::ChatConfig config;
        config.base_url = base_url;
        config.api_key = api_key;
        config.model = model;
        config.system_rules = {"You are a helpful assistant. Reply in the user's language."};

        codex_chat_bot::ChatSession bot(config);
        bot.bind_history("sample-chat-history.json");

        while (true) {
            std::string message = read_line("\nYou> ");
            if (message.empty()) {
                continue;
            }
            if (message == "/exit" || message == "/quit") {
                break;
            }
            if (message == "/reset") {
                bot.reset();
                std::cout << "Session reset.\n";
                continue;
            }

            std::string answer;
            {
                ThinkingStatus status("Bot is thinking...");
                answer = bot.ask(message);
            }
            std::cout << "Bot> " << answer << "\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
