#pragma once
#include "common.h"

void init_shared_log(void);
void cleanup_shared_log(void);
int  prepare_log_file(const char *path, const char *owner_name, const char *group_name, mode_t mode);
void add_log_buffer_async(int worker_index, const char *ip, const char *buf, int n);
void logger_loop_ixxc(void);
pid_t fork_logger_ixxc(void);
int  prepare_logging_subsystem_ixxc(void);

