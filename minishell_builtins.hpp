#ifndef MINISHELL_BUILTINS_HPP
#define MINISHELL_BUILTINS_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>   
#include <cctype>    
#include <cerrno>    
#include <cstring>      
#include <unistd.h>  
#include <sys/types.h>
#include <pwd.h>       
#include <filesystem> 

namespace mshell {

// -------------------------------------------------------------------------------------------------
// Environment for builtins (currently: alias table).
// -------------------------------------------------------------------------------------------------
struct BuiltinEnv {
    std::unordered_map<std::string, std::string> aliases;
};

// -------------------------------------------------------------------------------------------------
// Small helpers
// -------------------------------------------------------------------------------------------------
inline std::string home_dir() {
    if (const char* h = std::getenv("HOME")) return h;
    if (passwd* pw = getpwuid(getuid())) {
        if (pw->pw_dir) return pw->pw_dir;
    }
    return "/";
}

inline void trim_inplace(std::string& s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.pop_back();
}

inline std::string unquote_if(const std::string& s) {
    if (s.size() >= 2) {
        char q = s.front();
        if ((q == '\'' || q == '"') && s.back() == q) return s.substr(1, s.size() - 2);
    }
    return s;
}

// -------------------------------------------------------------------------------------------------
// Minimal rc-line evaluator (supports: alias, export, echo, setprompt) — used by load_rc and source
// -------------------------------------------------------------------------------------------------
inline void eval_rc_line(BuiltinEnv& env, std::string line) {
    // strip comments
    auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    trim_inplace(line);
    if (line.empty()) return;

    if (line.rfind("alias ", 0) == 0) {
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = line.substr(6, eq - 6);
            std::string val = line.substr(eq + 1);
            trim_inplace(key); trim_inplace(val);
            env.aliases[key] = unquote_if(val);
        }
        return;
    }

    if (line.rfind("export ", 0) == 0) {
        std::string rest = line.substr(7);
        auto eq = rest.find('=');
        if (eq != std::string::npos) {
            std::string k = rest.substr(0, eq);
            std::string v = rest.substr(eq + 1);
            trim_inplace(k); trim_inplace(v);
            setenv(k.c_str(), v.c_str(), 1);
        }
        return;
    }

    if (line.rfind("echo ", 0) == 0) {
        std::cout << line.substr(5) << "\n";
        return;
    }

    if (line.rfind("setprompt ", 0) == 0) {
        std::string v = unquote_if(line.substr(10));
        setenv("MINISHELL_PROMPT", v.c_str(), 1);
        return;
    }

}

// -------------------------------------------------------------------------------------------------
// Load ~/.minishellrc
// -------------------------------------------------------------------------------------------------
inline void load_rc(BuiltinEnv& env) {
    std::ifstream in(home_dir() + "/.minishellrc");
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        eval_rc_line(env, line);
    }
}

// -------------------------------------------------------------------------------------------------
// Builtin dispatcher
// Return true if command handled; set exit_status accordingly.
// -------------------------------------------------------------------------------------------------
inline bool builtin_dispatch(BuiltinEnv& env,
                             const std::vector<std::string>& argv,
                             int& exit_status)
{
    if (argv.empty()) { exit_status = 0; return true; }

    const std::string& cmd = argv[0];

    if (cmd == "cd") {
        const char* target = (argv.size() > 1) ? argv[1].c_str() : std::getenv("HOME");
        if (!target) target = "/";
        if (chdir(target) != 0) std::cerr << "cd: " << std::strerror(errno) << "\n";
        exit_status = 0; return true;
    }

    if (cmd == "pwd") {
        char buf[4096];
        if (getcwd(buf, sizeof(buf))) std::cout << buf << "\n";
        exit_status = 0; return true;
    }

    if (cmd == "echo") {
        for (size_t i = 1; i < argv.size(); ++i) {
            if (i > 1) std::cout << ' ';
            std::cout << argv[i];
        }
        std::cout << "\n";
        exit_status = 0; return true;
    }

    if (cmd == "export") {
        for (size_t i = 1; i < argv.size(); ++i) {
            auto eq = argv[i].find('=');
            if (eq != std::string::npos) {
                std::string k = argv[i].substr(0, eq);
                std::string v = argv[i].substr(eq + 1);
                setenv(k.c_str(), v.c_str(), 1);
            }
        }
        exit_status = 0; return true;
    }

    if (cmd == "unset") {
        for (size_t i = 1; i < argv.size(); ++i) unsetenv(argv[i].c_str());
        exit_status = 0; return true;
    }

    if (cmd == "alias") {
        if (argv.size() == 1) {
            for (const auto& kv : env.aliases)
                std::cout << "alias " << kv.first << "='" << kv.second << "'\n";
        } else {
            for (size_t i = 1; i < argv.size(); ++i) {
                auto eq = argv[i].find('=');
                if (eq != std::string::npos) {
                    std::string k = argv[i].substr(0, eq);
                    std::string v = unquote_if(argv[i].substr(eq + 1));
                    env.aliases[k] = v;
                }
            }
        }
        exit_status = 0; return true;
    }

    if (cmd == "unalias") {
        if (argv.size() > 1) env.aliases.erase(argv[1]);
        exit_status = 0; return true;
    }

    if ((cmd == "source" || cmd == ".") && argv.size() > 1) {
        std::ifstream in(argv[1]);
        if (!in) {
            std::cerr << cmd << ": cannot open " << argv[1] << "\n";
            exit_status = 1; return true;
        }
        std::string line;
        while (std::getline(in, line)) eval_rc_line(env, line);
        exit_status = 0; return true;
    }

    (void)env;
    return false; 
}

// -------------------------------------------------------------------------------------------------
// Alias expansion (single-word head) with safety:
//  - Ignores empty/whitespace alias bodies
//  - Prevents simple recursion (depth cap)
// -------------------------------------------------------------------------------------------------
inline std::vector<std::string> alias_expand(BuiltinEnv& env,
                                             const std::vector<std::string>& argv)
{
    if (argv.empty()) return argv;

    auto it = env.aliases.find(argv[0]);
    if (it == env.aliases.end()) return argv;

    std::string body = it->second;
    trim_inplace(body);
    if (body.empty()) return argv;

    std::vector<std::string> head;
    {
        std::istringstream iss(body);
        std::string w;
        while (iss >> w) head.push_back(w);
    }
    if (head.empty()) return argv;

    static thread_local int depth = 0;
    if (depth > 10) return argv;
    ++depth;

    if (head[0] == argv[0]) {
        head.insert(head.end(), argv.begin() + 1, argv.end());
        --depth;
        return head;
    }

    head.insert(head.end(), argv.begin() + 1, argv.end());
    --depth;
    return head;
}

inline bool try_autocd(const std::vector<std::string>& argv) {
    if (argv.empty()) return false;
    std::error_code ec;
    if (std::filesystem::is_directory(argv[0], ec)) {
        if (chdir(argv[0].c_str()) != 0)
            std::cerr << "cd: " << std::strerror(errno) << "\n";
        return true;
    }
    return false;
}

} 
#endif // MINISHELL_BUILTINS_HPP
