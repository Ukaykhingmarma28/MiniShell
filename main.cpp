/*
 *  minishell.cpp
 *  -----------------------------
 *  A minimal, educational shell written in C++17.
 *  Supports:
 *    • Running programs via execvp()
 *    • Input/output redirection (<, >, >>)
 *    • Pipelining with |
 *    • Built-in commands: cd, exit
 *
 *  Author: Ukay Khing Marma Joy
 *  GitHub: https://github.com/ukaykhing
 *
 *  Created: October 2025
 *  Language: C++17 (POSIX / Linux)
 *  License: MIT License
 *
 *  -------------------------------------------------------------------------
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the “Software”),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included
 *  in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *  -------------------------------------------------------------------------
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
 
 struct Redir {
     std::string in, out;
     bool append = false;
 };
 
 // split by whitespace
 std::vector<std::string> split_ws(const std::string& s) {
     std::istringstream iss(s);
     std::vector<std::string> toks;
     std::string t;
     while (iss >> t) toks.push_back(t);
     return toks;
 }
 
 // split by |
 std::vector<std::string> split_pipeline(const std::string& line) {
     std::vector<std::string> parts;
     std::string cur;
     for (size_t i = 0; i < line.size(); ++i) {
         if (line[i] == '|') {
             if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
         } else {
             cur.push_back(line[i]);
         }
     }
     if (!cur.empty()) parts.push_back(cur);
     return parts;
 }
 
 // parse redirections
 void parse_redirections(std::vector<std::string>& argv, Redir& r) {
     std::vector<std::string> clean;
     for (size_t i = 0; i < argv.size(); ++i) {
         if (argv[i] == "<" && i + 1 < argv.size()) {
             r.in = argv[i + 1]; ++i;
         } else if (argv[i] == ">" && i + 1 < argv.size()) {
             r.out = argv[i + 1]; r.append = false; ++i;
         } else if (argv[i] == ">>" && i + 1 < argv.size()) {
             r.out = argv[i + 1]; r.append = true; ++i;
         } else {
             clean.push_back(argv[i]);
         }
     }
     argv.swap(clean);
 }
 
 char** vec_to_argv(const std::vector<std::string>& v) {
     char** args = new char*[v.size() + 1];
     for (size_t i = 0; i < v.size(); ++i)
         args[i] = strdup(v[i].c_str());
     args[v.size()] = nullptr;
     return args;
 }
 
 void free_argv(char** args) {
     if (!args) return;
     for (size_t i = 0; args[i] != nullptr; ++i) free(args[i]);
     delete[] args;
 }
 
 int run_pipeline(std::vector<std::vector<std::string>>& commands,
                  std::vector<Redir>& redirs) {
     int n = static_cast<int>(commands.size());
     std::vector<pid_t> pids;
     pids.reserve(n);
 
     std::vector<int> fds; fds.resize(std::max(0, (n - 1) * 2));
     for (int i = 0; i < n - 1; ++i) {
         if (pipe(&fds[2*i]) == -1) {
             perror("pipe");
             return 1;
         }
     }
 
     for (int i = 0; i < n; ++i) {
         pid_t pid = fork();
         if (pid < 0) { perror("fork"); return 1; }
         if (pid == 0) {
             if (i > 0) {
                 if (dup2(fds[2*(i-1)] , STDIN_FILENO) == -1) { perror("dup2 in"); _exit(1); }
             }
             if (i < n - 1) {
                 if (dup2(fds[2*i + 1], STDOUT_FILENO) == -1) { perror("dup2 out"); _exit(1); }
             }
             for (int k = 0; k < (int)fds.size(); ++k) close(fds[k]);
 
             // redirections
             if (!redirs[i].in.empty()) {
                 int fd = open(redirs[i].in.c_str(), O_RDONLY);
                 if (fd == -1 || dup2(fd, STDIN_FILENO) == -1) { perror("redir <"); _exit(1); }
                 close(fd);
             }
             if (!redirs[i].out.empty()) {
                 int flags = O_WRONLY | O_CREAT | (redirs[i].append ? O_APPEND : O_TRUNC);
                 int fd = open(redirs[i].out.c_str(), flags, 0644);
                 if (fd == -1 || dup2(fd, STDOUT_FILENO) == -1) { perror("redir >"); _exit(1); }
                 close(fd);
             }
 
             // exec
             if (commands[i].empty()) _exit(0);
             char** argv = vec_to_argv(commands[i]);
             execvp(argv[0], argv);
             std::cerr << "execvp: " << argv[0] << ": " << strerror(errno) << "\n";
             free_argv(argv);
             _exit(127);
         } else {
             pids.push_back(pid);
         }
     }
     for (int fd : fds) close(fd);
 
     // wait for all
     int status = 0, last_status = 0;
     for (pid_t pid : pids) {
         if (waitpid(pid, &status, 0) > 0) last_status = status;
     }
     return WIFEXITED(last_status) ? WEXITSTATUS(last_status) : 1;
 }
 
 int main() {
     // ignore Ctrl-C signals
     signal(SIGINT, SIG_IGN);
 
     while (true) {
         // Prompt
         char cwd_buf[4096];
         if (getcwd(cwd_buf, sizeof(cwd_buf))) {
             std::cout << "[mini] " << cwd_buf << " $ ";
         } else {
             std::cout << "[mini] $ ";
         }
         std::cout.flush();
 
         // Read
         std::string line;
         if (!std::getline(std::cin, line)) break;
         // trim
         if (std::all_of(line.begin(), line.end(), isspace)) continue;
 
         auto stages = split_pipeline(line);
         
         if (stages.size() == 1) {
             auto toks = split_ws(stages[0]);
             if (toks.empty()) continue;
 
             if (toks[0] == "exit") {
                 return 0;
             }
             if (toks[0] == "cd") {
                 const char* target = nullptr;
                 if (toks.size() >= 2) target = toks[1].c_str();
                 else target = getenv("HOME");
                 if (!target) target = "/";
                 if (chdir(target) != 0) {
                     std::cerr << "cd: " << strerror(errno) << "\n";
                 }
                 continue;
             }
         }
 
         // Build argv and redirs for each stage
         std::vector<std::vector<std::string>> commands;
         std::vector<Redir> redirs;
         commands.reserve(stages.size());
         redirs.resize(stages.size());
 
         for (size_t i = 0; i < stages.size(); ++i) {
             auto toks = split_ws(stages[i]);
             parse_redirections(toks, redirs[i]);
             commands.push_back(std::move(toks));
         }
 
         run_pipeline(commands, redirs);
     }
     std::cout << "\n";
     return 0;
 }
 