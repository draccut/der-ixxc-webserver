#pragma once
#include "common.h"

void set_proc_name(const char *name);
void drop_privileges_and_set_name(const char *user, const char *proc_name);
void sigchld_handler(int sig);
void parent_shutdown_handler_ixxc(int sig);

void worker_loop_ixxc(int i, int sfd
#if USE_SSL
, SSL_CTX *ctx
#endif
);

pid_t fork_worker_ixxc(int i, int sfd
#if USE_SSL
, SSL_CTX *ctx
#endif
);

void kill_all_children_ixxc(pid_t logger_pid, pid_t *workers, int sig);
void reap_all_children_ixxc(pid_t logger_pid, pid_t *workers);

pid_t bootstrap_children_ixxc(int sfd
#if USE_SSL
, SSL_CTX *ctx
#endif
);

int parent_supervise_and_wait_ixxc(int sfd, pid_t logger_pid
#if USE_SSL
, SSL_CTX *ctx
#endif
);

