#include "../../header/prod/common.h"
#include "../../header/prod/process.h"
#include "../../header/prod/log.h"
#include "../../header/prod/connection.h"

#define MAX_THREAD_NAME_LENGTH 16

pid_t worker_pids[WORKERS];
volatile sig_atomic_t shutdown_requested = 0;

void set_proc_name(const char *name)
{
    char buf[MAX_THREAD_NAME_LENGTH];
    if (!name) return;
    strncpy(buf, name, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    prctl(PR_SET_NAME, (unsigned long)buf, 0, 0, 0);
}

void drop_privileges_and_set_name(const char *user, const char *proc_name)
{
    if (proc_name) {
        set_proc_name(proc_name);
    }
    if (getuid() != 0) {
        return;
    }
    if (!user) {
        fprintf(stderr, "drop_privileges_and_set_name: user == NULL\n");
        _exit(EXIT_FAILURE);
    }
    struct passwd *pw = getpwnam(user);
    if (!pw) {
        fprintf(stderr, "User '%s' nicht gefunden\n", user);
        _exit(EXIT_FAILURE);
    }
    if (pw->pw_shell != NULL && strstr(pw->pw_shell, "sh") != NULL && strstr(pw->pw_shell, "nologin") == NULL && strcmp(pw->pw_shell, "/bin/false") != 0) {
        fprintf(stderr, "\n[FEHLER] Sicherheitsrisiko: Der Benutzer %s hat eine aktive Login-Shell (%s)!\n", user, pw->pw_shell);
        fprintf(stderr, "Bitte sperren Sie die Shell dieses Benutzers mit folgendem Befehl:\n\n");
        fprintf(stderr, "usermod -s /usr/sbin/nologin %s\n\n", user);
        _exit(EXIT_FAILURE);
    }
    if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
        perror("initgroups");
        _exit(EXIT_FAILURE);
    }
    if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) != 0) {
        perror("setresgid");
        _exit(EXIT_FAILURE);
    }
    if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) != 0) {
        perror("setresuid");
        _exit(EXIT_FAILURE);
    }
    prctl(PR_SET_DUMPABLE, 0);
    prctl(PR_SET_NO_NEW_PRIVS, 1);
    if (proc_name) {
        set_proc_name(proc_name);
    }
}

void sigchld_handler(int sig)
{
    (void)sig;
    int saved = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
    errno = saved;
}

void parent_shutdown_handler_ixxc(int sig)
{
    (void)sig;
    shutdown_requested = 1;
}

void worker_loop_ixxc(int i, int sfd
#if USE_SSL
, SSL_CTX *ctx
#endif
) {
    printf("[ixxc_worker %d] loop start (pid=%d)\n", i, (int)getpid());
    for (;;) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept4(sfd, (struct sockaddr *)&caddr, &clen, SOCK_CLOEXEC);
        if (cfd < 0) {
            if (errno == EINTR) {
                printf("[ixxc_worker %d] EINTR  -  beende loop\n", i);
                break;
            }
            sleep(1);
            continue;
        }
        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        handle_connection_fd(i, cfd, &caddr
        #if USE_SSL
        , ctx
        #endif
        );
    }
    printf("[ixxc_worker %d] sauber beendet.\n", i);
}

pid_t fork_worker_ixxc(int i, int sfd
#if USE_SSL
, SSL_CTX *ctx
#endif
) {
    pid_t p = fork();
    if (p < 0) {
        perror("fork worker");
        exit(EXIT_FAILURE);
    }
    if (p == 0) {
        drop_privileges_and_set_name(DROP_USER, "ixxc_worker");
        signal(SIGTERM, SIG_DFL);
        signal(SIGINT,  SIG_DFL);
        signal(SIGPIPE, SIG_IGN);
        worker_loop_ixxc(i, sfd
            #if USE_SSL
            , ctx
            #endif
        );
        _exit(0);
    }
    return p;
}

void kill_all_children_ixxc(pid_t logger_pid, pid_t *workers, int sig)
{
    if (logger_pid > 0 && kill(logger_pid, sig) == -1 && errno != ESRCH) {
        perror("kill logger");
    }
    for (int i = 0; i < WORKERS; ++i) {
        if (workers[i] > 0 && kill(workers[i], sig) == -1 && errno != ESRCH) {
            perror("kill worker");
        }
    }
}

void reap_all_children_ixxc(pid_t logger_pid, pid_t *workers)
{
    int status;
    if (logger_pid > 0) {
        if (waitpid(logger_pid, &status, 0) == -1) {
            if (errno != ECHILD) {
                perror("waitpid logger");
            }
        } else if (WIFEXITED(status)) {
            printf("[ixxc_server] logger exit %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[ixxc_server] logger signal %d\n", WTERMSIG(status));
        }
    }
    for (int i = 0; i < WORKERS; ++i) {
        if (workers[i] <= 0) {
            continue;
        }
        if (waitpid(workers[i], &status, 0) == -1) {
            if (errno != ECHILD) {
                perror("waitpid worker");
            }
            continue;
        }
        if (WIFEXITED(status)) {
            printf("[ixxc_server] worker %d exit %d\n", i, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[ixxc_server] worker %d signal %d\n", i, WTERMSIG(status));
        }
    }
}

pid_t bootstrap_children_ixxc(int sfd
#if USE_SSL
, SSL_CTX *ctx
#endif
) {
    pid_t logger_pid = fork_logger_ixxc();
    for (int i = 0; i < WORKERS; ++i) {
        worker_pids[i] = fork_worker_ixxc(i, sfd
            #if USE_SSL
            , ctx
            #endif
        );
    }
    return logger_pid;
}

int parent_supervise_and_wait_ixxc(int sfd, pid_t logger_pid
#if USE_SSL
, SSL_CTX *ctx
#endif
) {
    #if USE_SSL
    (void)ctx;
    #endif
    drop_privileges_and_set_name(DROP_USER, "ixxc_server");
    signal(SIGINT,  parent_shutdown_handler_ixxc);
    signal(SIGTERM, parent_shutdown_handler_ixxc);
    signal(SIGCHLD, sigchld_handler);
    signal(SIGPIPE, SIG_IGN);
    printf("[ixxc_server] children: logger=%d workers=", (int)logger_pid);
    for (int i = 0; i < WORKERS; ++i) {
        printf("%d%s", (int)worker_pids[i], (i + 1 < WORKERS) ? "," : "\n");
    }
    while (!shutdown_requested) {
        pause();
        if (!shutdown_requested) {
            int alive = 0;
            if (logger_pid > 0 && kill(logger_pid, 0) == 0) {
                alive++;
            }
            for (int i = 0; i < WORKERS; ++i) {
                if (worker_pids[i] > 0 && kill(worker_pids[i], 0) == 0) {
                    alive++;
                }
            }
            if (alive == 0) {
                shutdown_requested = 1;
            }
        }
    }
    printf("[ixxc_server] shutdown sende SIGTERM an Kinder\n");
    kill_all_children_ixxc(logger_pid, worker_pids, SIGTERM);
    reap_all_children_ixxc(logger_pid, worker_pids);
    close(sfd);
    cleanup_shared_log();
    ixxc_cleanup_endpoints();
    printf("[ixxc_server] alle Ressourcen frei. Ende.\n");
    return 0;
}


