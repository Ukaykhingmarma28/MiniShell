#ifndef MINISHELL_EXPAND_HPP
#define MINISHELL_EXPAND_HPP

#include <string>
#include <vector>
#include <cstdlib>
#include <cctype>
#include <glob.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sstream>
#include <fstream>
#include <iostream>

namespace mshell {


inline std::string getenv_str(const std::string& key) {
    const char* v = std::getenv(key.c_str());
    return v ? std::string(v) : std::string();
}

inline std::string command_subst(const std::string& cmd) {
    int p[2];
    if (pipe(p) == -1) return "";

    pid_t pid = fork();
    if (pid == 0) {
        dup2(p[1], STDOUT_FILENO);
        close(p[0]);
        close(p[1]);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);
    }

    close(p[1]);
    std::ostringstream oss;
    char buf[4096];
    ssize_t n;
    while ((n = read(p[0], buf, sizeof(buf))) > 0)
        oss.write(buf, n);
    close(p[0]);

    int st = 0;
    waitpid(pid, &st, 0);

    std::string s = oss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

inline std::string expand_scalars(std::string s) {
    if (!s.empty() && s[0] == '~') {
        if (s.size() == 1 || s[1] == '/') {
            const char* home = std::getenv("HOME");
            if (home)
                s = std::string(home) + s.substr(1);
        }
    }

    for (size_t i = 0; i < s.size();) {
        if (s[i] == '`') {
            size_t j = s.find('`', i + 1);
            if (j != std::string::npos) {
                std::string sub = s.substr(i + 1, j - i - 1);
                std::string rep = command_subst(sub);
                s.replace(i, j - i + 1, rep);
                i += rep.size();
                continue;
            }
        } else if (i + 1 < s.size() && s[i] == '$' && s[i + 1] == '(') {
            size_t j = s.find(')', i + 2);
            if (j != std::string::npos) {
                std::string sub = s.substr(i + 2, j - (i + 2));
                std::string rep = command_subst(sub);
                s.replace(i, j - i + 1, rep);
                i += rep.size();
                continue;
            }
        }
        ++i;
    }

    for (size_t i = 0; i < s.size();) {
        if (s[i] == '$') {
            if (i + 1 < s.size() && s[i + 1] == '{') {
                size_t j = s.find('}', i + 2);
                if (j != std::string::npos) {
                    std::string key = s.substr(i + 2, j - (i + 2));
                    std::string val = getenv_str(key);
                    s.replace(i, j - i + 1, val);
                    i += val.size();
                    continue;
                }
            } else {
                size_t j = i + 1;
                while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '_'))
                    ++j;
                if (j > i + 1) {
                    std::string key = s.substr(i + 1, j - (i + 1));
                    std::string val = getenv_str(key);
                    s.replace(i, j - i, val);
                    i += val.size();
                    continue;
                }
            }
        }
        ++i;
    }

    return s;
}

// ─────────────────────────────────────────────────────────────
//  Globbing: expand wildcards (*, ?, [abc])
// ─────────────────────────────────────────────────────────────
inline std::vector<std::string> glob_expand(const std::string& s) {
    glob_t g{};
    std::vector<std::string> out;
    int flags = 0;

    if (glob(s.c_str(), flags, nullptr, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; ++i)
            out.emplace_back(g.gl_pathv[i]);
    } else {
        out.push_back(s);
    }

    globfree(&g);
    return out;
}

} 
#endif // MINISHELL_EXPAND_HPP
