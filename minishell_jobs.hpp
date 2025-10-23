#ifndef MINISHELL_JOBS_HPP
#define MINISHELL_JOBS_HPP

#include <vector>
#include <string>
#include <map>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <termios.h>
#include <iostream>
#include <cstring>
#include <cerrno>

namespace mshell {

// ─────────────────────────────────────────────────────────────
// Represents one background or foreground job
// ─────────────────────────────────────────────────────────────
struct Job {
    int id = 0;                 // internal job id
    pid_t pgid = -1;            // process group id
    std::string cmdline;        // the command line string
    bool stopped = false;       // stopped by SIGTSTP
    bool running = true;        // still running?
};

// ─────────────────────────────────────────────────────────────
// Job Table: manages active jobs (fg/bg control)
// ─────────────────────────────────────────────────────────────
class JobTable {
public:
    JobTable() {
        // capture shell pgid and terminal attributes
        tcgetattr(STDIN_FILENO, &shell_tmodes);
        shell_pgid = getpgrp();
    }

    // Add new background job
    int add(pid_t pgid, std::string cmd) {
        Job j;
        j.id = next_id++;
        j.pgid = pgid;
        j.cmdline = std::move(cmd);
        jobs[j.id] = j;
        return j.id;
    }

    void on_sigchld() {
        int status;
        pid_t pid;

        while ((pid = waitpid(-1, &status, WUNTRACED | WNOHANG | WCONTINUED)) > 0) {
            bool stopped = WIFSTOPPED(status);
            bool exited  = WIFEXITED(status) || WIFSIGNALED(status);
            pid_t pg = getpgid(pid);

            for (auto& [id, job] : jobs) {
                if (job.pgid == pg) {
                    job.stopped = stopped;
                    job.running = !exited;
                    if (exited)
                        remove(pg);
                    break;
                }
            }
        }
    }

    void list() const {
        if (jobs.empty()) {
            std::cout << "No background jobs.\n";
            return;
        }

        for (auto& [id, job] : jobs) {
            std::cout << "[" << id << "] "
                      << job.pgid << "  "
                      << (job.stopped ? "stopped" : "running")
                      << "  " << job.cmdline << "\n";
        }
    }

    bool fg(int id) {
        auto it = jobs.find(id);
        if (it == jobs.end()) {
            std::cerr << "fg: job not found\n";
            return false;
        }

        Job& job = it->second;
        tcsetpgrp(STDIN_FILENO, job.pgid);
        kill(-job.pgid, SIGCONT);

        int status;
        waitpid(-job.pgid, &status, WUNTRACED);
        tcsetpgrp(STDIN_FILENO, shell_pgid);

        if (WIFEXITED(status) || WIFSIGNALED(status))
            remove(job.pgid);

        return true;
    }

    bool bg(int id) {
        auto it = jobs.find(id);
        if (it == jobs.end()) {
            std::cerr << "bg: job not found\n";
            return false;
        }

        Job& job = it->second;
        kill(-job.pgid, SIGCONT);
        job.stopped = false;
        job.running = true;

        std::cout << "[" << job.id << "] " << job.pgid << " continued in background\n";
        return true;
    }

    void remove(pid_t pg) {
        for (auto it = jobs.begin(); it != jobs.end();) {
            if (it->second.pgid == pg)
                it = jobs.erase(it);
            else
                ++it;
        }
    }

private:
    std::map<int, Job> jobs;
    int next_id = 1;
    pid_t shell_pgid = -1;
    struct termios shell_tmodes {};
};

} // namespace mshell

#endif // MINISHELL_JOBS_HPP
