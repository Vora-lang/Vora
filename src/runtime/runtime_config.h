#pragma once

namespace vora {

enum class RuntimeMode {
    Script,
    REPL
};

struct RuntimeConfig {
    RuntimeMode mode = RuntimeMode::Script;

    bool repl() const noexcept {
        return mode == RuntimeMode::REPL;
    }
};

}
