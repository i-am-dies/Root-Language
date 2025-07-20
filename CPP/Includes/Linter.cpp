#pragma once

#include "Node.cpp"

// NOTE:
// It will be used as the replacement for old parser live messaging mechanism.
// Maybe this new static code analysis stage will include type checking.
// The linter should be used at back-end because only back-end can check code validity before execution.
// Result of its work should be similar to lexer's token-like structures array, including ranges, types and messages.
// Front-end should use it both in code input area to lint colors and in console area to warn users about errors.
struct Linter {
    Linter(const Node& tree);
};