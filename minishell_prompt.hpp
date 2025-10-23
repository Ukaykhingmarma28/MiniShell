#ifndef MINISHELL_PROMPT_HPP
#define MINISHELL_PROMPT_HPP

#include <string>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <limits.h>

#include "minishell_colors.hpp"

namespace mshell {

// ─────────────────────────────────────────────────────────────
// Git branch + dirty flag (e.g., "main" or "main*")
// ─────────────────────────────────────────────────────────────
inline std::string git_branch() {
    FILE* fp = popen("git rev-parse --abbrev-ref HEAD 2>/dev/null", "r");
    if (!fp) return "";
    std::array<char,128> buf{};
    std::string out;
    while (fgets(buf.data(), buf.size(), fp)) out += buf.data();
    pclose(fp);
    if (!out.empty() && out.back() == '\n') out.pop_back();
    if (out.empty()) return "";

    FILE* fp2 = popen("git status --porcelain 2>/dev/null", "r");
    bool dirty = false;
    if (fp2) { if (fgetc(fp2) != EOF) dirty = true; pclose(fp2); }

    if (dirty) out.push_back('*');
    return out;
}

// ─────────────────────────────────────────────────────────────
// Optional env override
// ─────────────────────────────────────────────────────────────
inline const char* rc_prompt_override() {
    const char* s = std::getenv("MINISHELL_PROMPT");
    return (s && *s) ? s : nullptr;
}

// ─────────────────────────────────────────────────────────────
// Readline wrapping for non-printing ANSI sequences
// (mark each ESC...[m sequence with \001 ... \002)
// ─────────────────────────────────────────────────────────────
inline std::string readline_wrap_nonprinting(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b') {
            out.push_back('\001');
            do {
                out.push_back(s[i]);
                if (s[i] == 'm') break;
                ++i;
            } while (i < s.size());
            out.push_back('\002');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────
// Prompt builder (plain string with ANSI colors)
// Style requested: "λ <user> <cwdBase> →  [λ git <branch> →]"
// Arrow color = green if last_status==0 else red
// ─────────────────────────────────────────────────────────────
inline std::string build_prompt_plain(int last_status) {
    if (auto* rc = rc_prompt_override()) return std::string(rc);

    // Gather context
    char cwd[PATH_MAX] = {};
    getcwd(cwd, sizeof(cwd));
    std::string cwd_base = cwd[0] ? std::string(cwd) : std::string("?");
    if (auto pos = cwd_base.find_last_of('/'); pos != std::string::npos)
        cwd_base = cwd_base.substr(pos + 1);

    const char* user = std::getenv("USER");
    if (!user || !*user) user = "user";

    std::string branch = git_branch();

    if (!color_enabled()) {
        std::string p = "λ ";
        p += user; p += " ";
        p += cwd_base; p += " → ";
        if (!branch.empty()) {
            p += "λ git ";
            p += branch;
            p += " → ";
        }
        return p;
    }

    // Colors
    const char* SYM   = ansi::FG_CYAN;
    const char* USERC = ansi::FG_BWHITE;
    const char* DIR   = ansi::FG_GREEN; 
    const char* GITL  = ansi::FG_MAGENTA;
    const char* GITB  = ansi::FG_YELLOW;
    const char* OK    = ansi::FG_GREEN;
    const char* ERR   = ansi::FG_RED;
    const char* SEP   = (last_status == 0) ? OK : ERR;

    // Build: λ <user> <dir> → [λ git <branch> →]
    std::string p;
    p += ansi::BOLD; p += SYM; p += "λ"; p += ansi::RESET; p += " ";
    p += ansi::BOLD; p += USERC; p += user; p += ansi::RESET; p += " ";
    p += DIR; p += cwd_base; p += ansi::RESET;
    p += ansi::BOLD; p += SEP; p += " → "; p += ansi::RESET;

    if (!branch.empty()) {
        p += ansi::BOLD; p += SYM; p += "λ"; p += ansi::RESET; p += " ";
        p += GITL; p += "git"; p += ansi::RESET; p += " ";
        p += GITB; p += branch; p += ansi::RESET;
        p += ansi::BOLD; p += SEP; p += " → "; p += ansi::RESET;
    }
    return p;
}

// ─────────────────────────────────────────────────────────────
// Public builders used by main.cpp
// ─────────────────────────────────────────────────────────────
inline std::string build_prompt(int last_status) {
    if (auto* rc = rc_prompt_override()) return std::string(rc);
    return build_prompt_plain(last_status);
}

inline std::string build_prompt_readline(int last_status) {
    if (auto* rc = rc_prompt_override()) return std::string(rc);
    return readline_wrap_nonprinting(build_prompt_plain(last_status));
}

} // namespace mshell

#endif // MINISHELL_PROMPT_HPP
