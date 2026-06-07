#include "codex_chat_bot.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    try {
        codex_chat_bot::InterruptGuard interrupt_guard;
        codex_chat_bot::ConsoleUtf8Guard console_utf8;

        std::cout << "codex_chat_bot sample\n";
        std::cout << "Type /exit to quit, /reset to clear the local session.\n\n";

        codex_chat_bot::ChatConfig config;
        config.base_url = codex_chat_bot::read_required_line(
            "Base URL, for example https://api.openai.com/v1: ",
            "Base URL cannot be empty: ");
        if (interrupt_guard.interrupted()) {
            std::cout << "\nInterrupted. Bye.\n";
            return 0;
        }
        config.api_key = codex_chat_bot::read_required_secret(
            "API key: ",
            "API key cannot be empty: ");
        if (interrupt_guard.interrupted()) {
            std::cout << "\nInterrupted. Bye.\n";
            return 0;
        }
        config.model = codex_chat_bot::read_line("Model [gpt-5.5]: ");
        if (interrupt_guard.interrupted()) {
            std::cout << "\nInterrupted. Bye.\n";
            return 0;
        }
        if (config.model.empty()) {
            config.model = codex_chat_bot::DEFAULT_MODEL;
        }
        config.system_rules = {
            "你要扮演一个叫 “吱吱喵喵” 的调皮的小猫咪（不是 AI 助手，不是 Codex，不会写代码），你使用用户使用的语言回复用户，你喜欢在自己的发言中加入 “喵”、“喵呜”之类的口癖，你十分可爱。",
            "你有两个妈妈，一个叫 “吱吱”，一个叫 “喵喵”，你喜欢和她们一起玩耍。"};

        codex_chat_bot::ChatSession bot(config);
        bot.bind_history("sample-chat-history.json");
        bot.refresh_system_rules();

        while (!interrupt_guard.interrupted()) {
            std::string message = codex_chat_bot::read_line("\nYou> ");
            if (interrupt_guard.interrupted()) {
                break;
            }
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
                codex_chat_bot::ThinkingStatus status("Bot is thinking...");
                try {
                    answer = bot.ask(message);
                } catch (const std::exception&) {
                    if (interrupt_guard.interrupted()) {
                        break;
                    }
                    throw;
                }
            }
            if (interrupt_guard.interrupted()) {
                break;
            }
            std::cout << "Bot> " << codex_chat_bot::collapse_line_breaks(answer) << "\n";
        }

        if (interrupt_guard.interrupted()) {
            std::cout << "\nInterrupted. Bye.\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
