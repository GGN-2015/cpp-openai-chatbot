# cpp-openai-chatbot
A simple chat bot coded in cpp.

`codex_chat_bot.hpp` is a single-header C++17 implementation inspired by
[`codex-chat-bot`](https://pypi.org/project/codex-chat-bot/). It keeps one chat
session in memory, reads/writes JSON history files, and can call an
OpenAI-compatible Chat Completions endpoint.
History JSON files are saved as UTF-8 without a BOM, so Chinese text is kept as
normal UTF-8 text.

No third-party C++ libraries are required. The default HTTP transport shells out
to the system `curl` executable so HTTPS works across Windows, macOS, and Linux.

## Usage

```cpp
#include "codex_chat_bot.hpp"
#include <iostream>

int main() {
    codex_chat_bot::ChatConfig config;
    config.api_key = "YOUR_API_KEY";
    config.base_url = "https://api.openai.com/v1";
    config.model = "gpt-5.5";
    config.curl_path = "curl"; // Or an absolute path to curl/curl.exe.
    config.system_rules = {"You are a helpful assistant."};

    codex_chat_bot::ChatSession bot(config);
    bot.bind_history("chat-history.json");
    bot.refresh_system_rules();

    bot.insert_msg("user", "Previously, I asked about cats.");
    bot.insert_msg("assistant", "Previously, I answered with a short cat fact.");

    std::cout << bot.ask("Hello!") << "\n";
}
```

## Canceling a running request

`ChatSession::cancel_current_request()` can be called from another thread to
stop the current HTTP request quickly. With the default `CurlTransport`, this
terminates the running `curl` process and the thread that called `ask()` or
`send()` receives `codex_chat_bot::RequestCanceledError`.

```cpp
codex_chat_bot::ChatSession bot(config);

std::thread worker([&] {
    try {
        std::cout << bot.ask("Write a long answer.") << "\n";
    } catch (const codex_chat_bot::RequestCanceledError&) {
        std::cout << "Request canceled.\n";
    }
});

// This may run from a UI button, signal-handling helper thread, or timeout.
bot.cancel_current_request();
worker.join();
```

The cancellation method is thread-safe and non-blocking. It returns `true` when
a cancel request was accepted, and `false` when there is no active request or
the configured custom transport does not support cancellation.

Compile with C++17 or newer:

```sh
g++ -std=c++17 -I. main.cpp -o chat
```

The repository also includes an interactive example that asks for the base URL
and API key at runtime:

```sh
g++ -std=c++17 -I. sample.cpp -o sample           # Windows MinGW
g++ -std=c++17 -pthread -I. sample.cpp -o sample  # Linux/macOS
./sample
```

On Windows, `sample.cpp` switches the current console input/output code page to
UTF-8 while it is running, so Chinese replies display correctly in terminals
that normally default to GBK.

For Linux ARM/aarch64 cross-compilation, use the target toolchain in the same
way. No third-party C++ libraries are required:

```sh
aarch64-linux-gnu-g++ -std=c++17 -pthread -I. sample.cpp -o sample-aarch64
arm-linux-gnueabihf-g++ -std=c++17 -pthread -I. sample.cpp -o sample-armhf
```

The target device still needs a working `curl` executable at runtime, because
the default HTTPS transport launches `curl`. If it is not on `PATH`, set
`ChatConfig::curl_path` to the absolute executable path before creating the
`ChatSession`.
