#ifndef MINISHELL_COLORS_HPP
#define MINISHELL_COLORS_HPP

#include <string>
#include <unistd.h>
#include <cstdlib>
#include <string_view>

namespace mshell {

namespace ansi {

// ─────────────────────────────────────────────────────────────
// Basic ANSI Style Sequences
// ─────────────────────────────────────────────────────────────

inline constexpr const char* RESET    = "\x1b[0m";
inline constexpr const char* BOLD     = "\x1b[1m";
inline constexpr const char* DIM      = "\x1b[2m";
inline constexpr const char* ITALIC   = "\x1b[3m";
inline constexpr const char* UNDER    = "\x1b[4m";
inline constexpr const char* BLINK    = "\x1b[5m";
inline constexpr const char* REVERSE  = "\x1b[7m";

// ─────────────────────────────────────────────────────────────
// 8-Color Foreground
// ─────────────────────────────────────────────────────────────

inline constexpr const char* FG_BLACK   = "\x1b[30m";
inline constexpr const char* FG_RED     = "\x1b[31m";
inline constexpr const char* FG_GREEN   = "\x1b[32m";
inline constexpr const char* FG_YELLOW  = "\x1b[33m";
inline constexpr const char* FG_BLUE    = "\x1b[34m";
inline constexpr const char* FG_MAGENTA = "\x1b[35m";
inline constexpr const char* FG_CYAN    = "\x1b[36m";
inline constexpr const char* FG_WHITE   = "\x1b[37m";

// ─────────────────────────────────────────────────────────────
// 8-Color Background
// ─────────────────────────────────────────────────────────────

inline constexpr const char* BG_BLACK   = "\x1b[40m";
inline constexpr const char* BG_RED     = "\x1b[41m";
inline constexpr const char* BG_GREEN   = "\x1b[42m";
inline constexpr const char* BG_YELLOW  = "\x1b[43m";
inline constexpr const char* BG_BLUE    = "\x1b[44m";
inline constexpr const char* BG_MAGENTA = "\x1b[45m";
inline constexpr const char* BG_CYAN    = "\x1b[46m";
inline constexpr const char* BG_WHITE   = "\x1b[47m";

// ─────────────────────────────────────────────────────────────
// Bright Foreground & Background
// ─────────────────────────────────────────────────────────────

inline constexpr const char* FG_BBLACK   = "\x1b[90m";
inline constexpr const char* FG_BRED     = "\x1b[91m";
inline constexpr const char* FG_BGREEN   = "\x1b[92m";
inline constexpr const char* FG_BYELLOW  = "\x1b[93m";
inline constexpr const char* FG_BBLUE    = "\x1b[94m";
inline constexpr const char* FG_BMAGENTA = "\x1b[95m";
inline constexpr const char* FG_BCYAN    = "\x1b[96m";
inline constexpr const char* FG_BWHITE   = "\x1b[97m";

inline constexpr const char* BG_BBLACK   = "\x1b[100m";
inline constexpr const char* BG_BRED     = "\x1b[101m";
inline constexpr const char* BG_BGREEN   = "\x1b[102m";
inline constexpr const char* BG_BYELLOW  = "\x1b[103m";
inline constexpr const char* BG_BBLUE    = "\x1b[104m";
inline constexpr const char* BG_BMAGENTA = "\x1b[105m";
inline constexpr const char* BG_BCYAN    = "\x1b[106m";
inline constexpr const char* BG_BWHITE   = "\x1b[107m";

// ─────────────────────────────────────────────────────────────
// 256-Color and TrueColor Support
// ─────────────────────────────────────────────────────────────

inline std::string FG256(int n) {
    return "\x1b[38;5;" + std::to_string(n) + "m";
}
inline std::string BG256(int n) {
    return "\x1b[48;5;" + std::to_string(n) + "m";
}
inline std::string RGB(int r, int g, int b) {
    return "\x1b[38;2;" + std::to_string(r) + ";" +
           std::to_string(g) + ";" + std::to_string(b) + "m";
}
inline std::string RGBBG(int r, int g, int b) {
    return "\x1b[48;2;" + std::to_string(r) + ";" +
           std::to_string(g) + ";" + std::to_string(b) + "m";
}

}

// ─────────────────────────────────────────────────────────────
// Helper: Check if color should be enabled
// ─────────────────────────────────────────────────────────────
inline bool color_enabled() {
    if (!isatty(STDOUT_FILENO)) return false;
    if (std::getenv("NO_COLOR")) return false;
    const char* term = std::getenv("TERM");
    if (term && std::string_view(term) == "dumb") return false;
    return true;
}

}

#endif // MINISHELL_COLORS_HPP
