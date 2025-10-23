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
 
 
 struct Redir {
     std::string in;
     std::string out;
     bool append = false;
 };
 
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

 int main() {
     setpgid(0, 0);
     g_shell_pgid = getpgrp();
     tcsetpgrp(STDIN_FILENO, g_shell_pgid);
 
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
 
         // Built-ins & job commands
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
 