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
    config.system_rules = {"You are a helpful assistant."};

    codex_chat_bot::ChatSession bot(config);
    bot.bind_history("chat-history.json");

    std::cout << bot.ask("Hello!") << "\n";
}
```

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
