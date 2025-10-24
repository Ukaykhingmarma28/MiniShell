/*
 *  minishell.cpp
 *  -----------------------------
 *  A minimal, educational shell written in C++17.
 *
 *  Author: Ukay Khing Marma Joy
 *  GitHub: https://github.com/Ukaykhingmarma28
 *
 *  Created: October 2025
 *  Language: C++17 (POSIX / Linux)
 *  License: MIT License
 *
 */
 
 #include <algorithm>
 #include <cerrno>
 #include <csignal>
 #include <cstring>
 #include <fcntl.h>
 #include <iostream>
 #include <sstream>
 #include <string>
 #include <sys/types.h>
 #include <sys/wait.h>
 #include <unistd.h>
 #include <vector>
 #include <termios.h>
 #include <limits.h>
 
 #include "minishell_colors.hpp"
 #include "minishell_tokenize.hpp"
 #include "minishell_expand.hpp"
 #include "minishell_jobs.hpp"
 #include "minishell_builtins.hpp"
 #include "minishell_prompt.hpp"
 
 #ifdef MINISHELL_HAVE_READLINE
 #  include <readline/readline.h>
 #  include <readline/history.h>
 extern "C" int rl_catch_signals;
 #endif
 
 // Version info for --version flag
 #define MINISHELL_VERSION "1.0.0"
 #define MINISHELL_RELEASE_DATE "October 2025"
 
 struct Redir {
     std::string in;
     std::string out;
     bool append = false;
 };
 
 // 🌟 Added: $$ variable expansion
 std::string expand_variables(const std::string& input) {
     std::string output = input;
     size_t pos = 0;
     while ((pos = output.find("$$", pos)) != std::string::npos) {
         output.replace(pos, 2, std::to_string(getpid()));
         pos += std::to_string(getpid()).length();
     }
     return output;
 }
 
 // -----------------------------------------------------------
 //  Get executable path dynamically
 // -----------------------------------------------------------
 std::string get_executable_path() {
     char path[PATH_MAX];
     ssize_t len;
     
     #ifdef __linux__
     len = readlink("/proc/self/exe", path, sizeof(path) - 1);
     #elif defined(__APPLE__)
     uint32_t size = sizeof(path);
     if (_NSGetExecutablePath(path, &size) == 0) {
         len = strlen(path);
     } else {
         len = -1;
     }
     #elif defined(__FreeBSD__)
     len = readlink("/proc/curproc/file", path, sizeof(path) - 1);
     #else
     // Fallback: try to use argv[0] or default
     return "/usr/local/bin/minishell";
     #endif
     
     if (len != -1) {
         path[len] = '\0';
         return std::string(path);
     }
     
     // Fallback
     return "/usr/local/bin/minishell";
 }
 
 // -----------------------------------------------------------
 //  Print version info
 // -----------------------------------------------------------
 void print_version() {
     std::cout << "MiniShell version " << MINISHELL_VERSION << "\n";
     std::cout << "Release Date: " << MINISHELL_RELEASE_DATE << "\n";
     std::cout << "Built with C++17 for POSIX systems\n";
     std::cout << "Copyright (c) 2025 Ukay Khing Marma Joy\n";
     std::cout << "License: MIT\n";
 }
 
 // -----------------------------------------------------------
 //  Print help info
 // -----------------------------------------------------------
 void print_help(const char* prog_name) {
     std::cout << "Usage: " << prog_name << " [OPTIONS]\n\n";
     std::cout << "A minimal Shell Like Water.\n\n";
     std::cout << "Options:\n";
     std::cout << "  -c COMMAND    Execute COMMAND and exit\n";
     std::cout << "  --version     Display version information\n";
     std::cout << "  --help        Display this help message\n";
     std::cout << "  -h            Display this help message\n\n";
     std::cout << "Features:\n";
     std::cout << "  • Pipelines and I/O redirection (|, <, >, >>)\n";
     std::cout << "  • Variable expansion ($VAR, ${VAR})\n";
     std::cout << "  • Command substitution (`cmd` or $(cmd))\n";
     std::cout << "  • Globbing (*, ?, [...])\n";
     std::cout << "  • Job control (bg, fg, jobs, &)\n";
     std::cout << "  • Aliases and built-in commands\n";
     std::cout << "  • Customizable prompt with Git integration\n\n";
     std::cout << "Built-in Commands:\n";
     std::cout << "  cd, pwd, echo, export, unset, alias, unalias,\n";
     std::cout << "  source, jobs, fg, bg, exit\n\n";
     std::cout << "Config File: ~/.minishellrc\n";
     std::cout << "GitHub: https://github.com/Ukaykhingmarma28/minishell\n";
 }
 
 // -----------------------------------------------------------
 //  Global State
 // -----------------------------------------------------------
 static pid_t g_shell_pgid = -1;
 static mshell::JobTable g_jobs;
 
 // -----------------------------------------------------------
 //  Helpers
 // -----------------------------------------------------------
 
 std::vector<std::string> split_pipeline(const std::string& line) {
     std::vector<std::string> parts;
     std::string cur;
     int dq = 0, sq = 0;
     for (char c : line) {
         if (c == '"' && !sq) dq ^= 1;
         else if (c == '\'' && !dq) sq ^= 1;
         if (c == '|' && !dq && !sq) {
             if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
         } else cur.push_back(c);
     }
     if (!cur.empty()) parts.push_back(cur);
     return parts;
 }
 
 void parse_redirections(std::vector<std::string>& argv, Redir& r) {
     std::vector<std::string> clean;
     for (size_t i = 0; i < argv.size(); ++i) {
         if (argv[i] == "<" && i + 1 < argv.size()) { r.in = argv[i + 1]; ++i; }
         else if (argv[i] == ">" && i + 1 < argv.size()) { r.out = argv[i + 1]; r.append = false; ++i; }
         else if (argv[i] == ">>" && i + 1 < argv.size()) { r.out = argv[i + 1]; r.append = true; ++i; }
         else clean.push_back(argv[i]);
     }
     argv.swap(clean);
 }
 
 char** vec_to_argv(const std::vector<std::string>& v) {
     char** a = new char*[v.size() + 1];
     for (size_t i = 0; i < v.size(); ++i)
         a[i] = strdup(v[i].c_str());
     a[v.size()] = nullptr;
     return a;
 }
 
 void free_argv(char** a) {
     if (!a) return;
     for (size_t i = 0; a[i]; ++i) free(a[i]);
     delete[] a;
 }
 
 // -----------------------------------------------------------
 //  Execute single command (for -c option)
 // -----------------------------------------------------------
 int execute_command_string(const std::string& cmdline, mshell::BuiltinEnv& benv) {
     std::string line = expand_variables(cmdline);
     
     if (line.empty()) return 0;
     
     bool background = false;
     std::string trimmed = line;
     while (!trimmed.empty() && isspace((unsigned char)trimmed.back())) trimmed.pop_back();
     if (!trimmed.empty() && trimmed.back() == '&') {
         background = true;
         trimmed.pop_back();
     }
     
     auto stages = split_pipeline(trimmed);
     std::vector<std::vector<std::string>> commands;
     std::vector<Redir> redirs(stages.size());
     
     for (size_t i = 0; i < stages.size(); ++i) {
         auto toks = mshell::tokenize(stages[i]);
         std::vector<std::string> words; words.reserve(toks.size());
         for (auto& t : toks) {
             auto scalar = mshell::expand_scalars(t.text);
             auto expanded = mshell::glob_expand(scalar);
             words.insert(words.end(), expanded.begin(), expanded.end());
         }
         parse_redirections(words, redirs[i]);
         commands.push_back(std::move(words));
     }
     
     if (commands.empty()) return 0;
     commands.front() = mshell::alias_expand(benv, commands.front());
     
     if (stages.size() == 1 && !background) {
         if (mshell::try_autocd(commands[0])) return 0;
         int es = 0;
         if (mshell::builtin_dispatch(benv, commands[0], es)) return es;
         if (!commands[0].empty()) {
             const std::string& cmd = commands[0][0];
             if (cmd == "exit") exit(0);
         }
     }
     
     // For -c mode, we don't have full job control, so just run foreground
     int n = (int)commands.size();
     std::vector<int> fds(std::max(0, (n - 1) * 2));
     for (int i = 0; i < n - 1; ++i)
         if (pipe(&fds[2 * i]) == -1) { perror("pipe"); return 1; }
     
     pid_t pgid = -1;
     
     for (int i = 0; i < n; ++i) {
         pid_t pid = fork();
         if (pid < 0) { perror("fork"); return 1; }
         if (pid == 0) {
             if (i == 0) setpgid(0, 0); else setpgid(0, pgid);
             signal(SIGINT, SIG_DFL);
             signal(SIGTSTP, SIG_DFL);
             signal(SIGQUIT, SIG_DFL);
             
             if (i > 0) dup2(fds[2 * (i - 1)], STDIN_FILENO);
             if (i < n - 1) dup2(fds[2 * i + 1], STDOUT_FILENO);
             for (int fd : fds) close(fd);
             
             if (!redirs[i].in.empty()) {
                 int fd = open(redirs[i].in.c_str(), O_RDONLY);
                 if (fd == -1 || dup2(fd, STDIN_FILENO) == -1) { perror("redir <"); _exit(1); }
                 close(fd);
             }
             if (!redirs[i].out.empty()) {
                 int fd = open(redirs[i].out.c_str(),
                               O_WRONLY | O_CREAT | (redirs[i].append ? O_APPEND : O_TRUNC), 0644);
                 if (fd == -1 || dup2(fd, STDOUT_FILENO) == -1) { perror("redir >"); _exit(1); }
                 close(fd);
             }
             
             if (commands[i].empty()) _exit(0);
             char** argv = vec_to_argv(commands[i]);
             execvp(argv[0], argv);
             std::cerr << "execvp: " << argv[0] << ": " << strerror(errno) << "\n";
             free_argv(argv);
             _exit(127);
         } else {
             if (i == 0) { pgid = pid; setpgid(pid, pgid); }
             else setpgid(pid, pgid);
         }
     }
     
     for (int fd : fds) close(fd);
     
     int status = 0;
     waitpid(-pgid, &status, 0);
     return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
 }
 
 // -----------------------------------------------------------
 //  Run pipeline
 // -----------------------------------------------------------
 
 int run_pipeline(std::vector<std::vector<std::string>>& commands,
                  std::vector<Redir>& redirs,
                  mshell::JobTable* jt,
                  bool background)
 {
     int n = (int)commands.size();
     std::vector<int> fds(std::max(0, (n - 1) * 2));
     for (int i = 0; i < n - 1; ++i)
         if (pipe(&fds[2 * i]) == -1) { perror("pipe"); return 1; }
 
     pid_t pgid = -1;
 
     for (int i = 0; i < n; ++i) {
         pid_t pid = fork();
         if (pid < 0) { perror("fork"); return 1; }
         if (pid == 0) {
             // ---- Child ----
             if (i == 0) setpgid(0, 0); else setpgid(0, pgid);
             signal(SIGINT, SIG_DFL);
             signal(SIGTSTP, SIG_DFL);
             signal(SIGQUIT, SIG_DFL);
             signal(SIGTTIN, SIG_DFL);
             signal(SIGTTOU, SIG_DFL);
 
             if (!background) tcsetpgrp(STDIN_FILENO, getpgrp());
             if (i > 0) dup2(fds[2 * (i - 1)], STDIN_FILENO);
             if (i < n - 1) dup2(fds[2 * i + 1], STDOUT_FILENO);
             for (int fd : fds) close(fd);
 
             if (!redirs[i].in.empty()) {
                 int fd = open(redirs[i].in.c_str(), O_RDONLY);
                 if (fd == -1 || dup2(fd, STDIN_FILENO) == -1) { perror("redir <"); _exit(1); }
                 close(fd);
             }
             if (!redirs[i].out.empty()) {
                 int fd = open(redirs[i].out.c_str(),
                               O_WRONLY | O_CREAT | (redirs[i].append ? O_APPEND : O_TRUNC), 0644);
                 if (fd == -1 || dup2(fd, STDOUT_FILENO) == -1) { perror("redir >"); _exit(1); }
                 close(fd);
             }
 
             if (commands[i].empty()) _exit(0);
             char** argv = vec_to_argv(commands[i]);
             execvp(argv[0], argv);
             std::cerr << "execvp: " << argv[0] << ": " << strerror(errno) << "\n";
             free_argv(argv);
             _exit(127);
         } else {
             if (i == 0) { pgid = pid; setpgid(pid, pgid); }
             else setpgid(pid, pgid);
         }
     }
 
     for (int fd : fds) close(fd);
 
     if (background && jt) {
         std::ostringstream oss;
         for (size_t i = 0; i < commands.size(); ++i) {
             for (auto& s : commands[i]) oss << s << ' ';
             if (i + 1 < commands.size()) oss << "| ";
         }
         int id = jt->add(pgid, oss.str());
         std::cout << "[" << id << "] " << pgid << "\n";
         return 0;
     }
 
     // Foreground execution
     tcsetpgrp(STDIN_FILENO, pgid);
     int status = 0;
     waitpid(-pgid, &status, 0);
     tcsetpgrp(STDIN_FILENO, g_shell_pgid);
     return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
 }
 
 static void sigchld_handler(int) { g_jobs.on_sigchld(); }
 
 // -----------------------------------------------------------
 //  Entry point
 // -----------------------------------------------------------
 
 int main(int argc, char* argv[]) {
     // Parse command-line arguments
     bool exec_mode = false;
     std::string exec_command;
     
     for (int i = 1; i < argc; ++i) {
         std::string arg = argv[i];
         
         if (arg == "--version") {
             print_version();
             return 0;
         } else if (arg == "--help" || arg == "-h") {
             print_help(argv[0]);
             return 0;
         } else if (arg == "-c") {
             if (i + 1 < argc) {
                 exec_mode = true;
                 exec_command = argv[++i];
             } else {
                 std::cerr << "minishell: -c requires an argument\n";
                 return 1;
             }
         } else {
             std::cerr << "minishell: unknown option: " << arg << "\n";
             std::cerr << "Try 'minishell --help' for more information.\n";
             return 1;
         }
     }
     
     // Set $SHELL to actual executable path (not hardcoded)
     std::string shell_path = get_executable_path();
     setenv("SHELL", shell_path.c_str(), 1);
     
     // Set MINISHELL_VERSION for detection by tools
     setenv("MINISHELL_VERSION", MINISHELL_VERSION, 1);
     
     // -c mode: execute command and exit
     if (exec_mode) {
         mshell::BuiltinEnv benv;
         mshell::load_rc(benv);
         return execute_command_string(exec_command, benv);
     }
     
     // Interactive mode
     setpgid(0, 0);
     g_shell_pgid = getpgrp();
     tcsetpgrp(STDIN_FILENO, g_shell_pgid);
 
     // 🌟 Ignore background job signals
     signal(SIGTTIN, SIG_IGN);
     signal(SIGTTOU, SIG_IGN);
     signal(SIGCHLD, sigchld_handler);
 
 #ifdef MINISHELL_HAVE_READLINE
     rl_catch_signals = 0;
 #endif
 
     mshell::BuiltinEnv benv;
     mshell::load_rc(benv);
 
     int last_status = 0;
     while (true) {
         std::string prompt =
 #ifdef MINISHELL_HAVE_READLINE
             mshell::build_prompt_readline(last_status);
 #else
             mshell::build_prompt_plain(last_status);
 #endif
 
         std::string line;
 #ifdef MINISHELL_HAVE_READLINE
         static bool rl_init = false;
         if (!rl_init) { using_history(); rl_init = true; }
         if (char* buf = readline(prompt.c_str())) {
             line = buf;
             free(buf);
             if (!line.empty()) add_history(line.c_str());
         } else { std::cout << "\n"; break; }
 #else
         std::cout << prompt << std::flush;
         if (!std::getline(std::cin, line)) { std::cout << "\n"; break; }
 #endif
 
         // 🌟 $$ expansion
         line = expand_variables(line);
 
         if (line.empty()) continue;
 
         bool background = false;
         std::string trimmed = line;
         while (!trimmed.empty() && isspace((unsigned char)trimmed.back())) trimmed.pop_back();
         if (!trimmed.empty() && trimmed.back() == '&') {
             background = true;
             trimmed.pop_back();
         }
 
         auto stages = split_pipeline(trimmed);
         std::vector<std::vector<std::string>> commands;
         std::vector<Redir> redirs(stages.size());
 
         for (size_t i = 0; i < stages.size(); ++i) {
             auto toks = mshell::tokenize(stages[i]);
             std::vector<std::string> words; words.reserve(toks.size());
             for (auto& t : toks) {
                 auto scalar = mshell::expand_scalars(t.text);
                 auto expanded = mshell::glob_expand(scalar);
                 words.insert(words.end(), expanded.begin(), expanded.end());
             }
             parse_redirections(words, redirs[i]);
             commands.push_back(std::move(words));
         }
 
         if (commands.empty()) continue;
         commands.front() = mshell::alias_expand(benv, commands.front());
 
         if (stages.size() == 1 && !background) {
             if (mshell::try_autocd(commands[0])) { last_status = 0; continue; }
             int es = 0;
             if (mshell::builtin_dispatch(benv, commands[0], es)) { last_status = es; continue; }
             if (!commands[0].empty()) {
                 const std::string& cmd = commands[0][0];
                 if (cmd == "jobs") { g_jobs.list(); last_status = 0; continue; }
                 if (cmd == "fg" && commands[0].size() > 1) { last_status = g_jobs.fg(std::stoi(commands[0][1])) ? 0 : 1; continue; }
                 if (cmd == "bg" && commands[0].size() > 1) { last_status = g_jobs.bg(std::stoi(commands[0][1])) ? 0 : 1; continue; }
                 if (cmd == "exit") break;
             }
         }
         last_status = run_pipeline(commands, redirs, &g_jobs, background);
     }
 
     std::cout << "\n";
     return 0;
 }