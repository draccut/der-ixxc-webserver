#include "../../header/prod/common.h"
#include "../../header/prod/log.h"
#include "../../header/prod/process.h"

shared_log_t *shared_log = NULL;
volatile sig_atomic_t shutdown_requested_logger = 0;

void init_shared_log(void)
{
    shared_log = mmap(NULL, sizeof(shared_log_t),
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared_log == MAP_FAILED) {
        perror("mmap shared log");
        exit(1);
    }
    atomic_init(&shared_log->write_index, 0);
    atomic_init(&shared_log->read_index, 0);
}

void cleanup_shared_log(void)
{
    if (shared_log != NULL && shared_log != MAP_FAILED) {
        if (munmap(shared_log, sizeof(shared_log_t)) == -1) {
            perror("munmap shared log");
        } else {
            printf("[Parent] Shared Log Speicher erfolgreich freigegeben.\n");
            shared_log = NULL;
        }
    }
}

int prepare_log_file(const char *path, const char *owner_name, const char *group_name, mode_t mode)
{
    if (!path || !owner_name || !group_name) {
        errno = EINVAL;
        return -1;
    }
    int fd = open(path, O_CREAT | O_NOFOLLOW | O_WRONLY, mode);
    if (fd < 0) {
        if (errno == EEXIST) {
            fd = open(path, O_WRONLY);
            if (fd < 0) {
                perror("open existing logfile");
                return -1;
            }
        } else {
            perror("create logfile");
            return -1;
        }
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat logfile");
        close(fd);
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "logfile is not a regular file\n");
        close(fd);
        errno = EINVAL;
        return -1;
    }
    struct passwd *pw = getpwnam(owner_name);
    if (!pw) {
        fprintf(stderr, "unknown user: %s\n", owner_name);
        close(fd);
        errno = EINVAL;
        return -1;
    }
    struct group *gr = getgrnam(group_name);
    if (!gr) {
        fprintf(stderr, "unknown group: %s\n", group_name);
        close(fd);
        errno = EINVAL;
        return -1;
    }
    if (fchown(fd, pw->pw_uid, gr->gr_gid) != 0) perror("fchown logfile");
    if (fchmod(fd, mode) != 0) {
        perror("fchmod logfile");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int open_log_fd(const char *path)
{
    int fd = open(path, O_CREAT | O_APPEND | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0640);
    if (fd < 0) {
        perror("open log");
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    return fd;
}

void add_log_buffer_async(int worker_index, const char *ip, const char *buf, int n)
{
    if (!shared_log) return;
    unsigned int r_idx = atomic_load(&shared_log->read_index);
    unsigned int w_idx = atomic_load(&shared_log->write_index);
    if (w_idx - r_idx >= RING_BUFFER_SIZE) return;
    unsigned int current_index;
    do {
        current_index = atomic_load(&shared_log->write_index);
        if (current_index - atomic_load(&shared_log->read_index) >= RING_BUFFER_SIZE) {
            return;
        }
    } while (!atomic_compare_exchange_weak(&shared_log->write_index, &current_index, current_index + 1));
    int slot = current_index & (RING_BUFFER_SIZE - 1);
    time_t t = time(NULL);
    char ts[32];
    struct tm *tm_info = localtime(&t);
    if (tm_info) {
        strftime(ts, sizeof(ts), "%F %T", tm_info);
    } else {
        strncpy(ts, "0000-00-00 00:00:00", sizeof(ts));
    }
    int current_len = snprintf(shared_log->log_buffer[slot], MAX_LOG_LINE, "[%s] [%s] [%d] ", ts, ip, worker_index);
    if (current_len < 0) {
        current_len = 0;
    }
    if (current_len >= MAX_LOG_LINE) {
        current_len = MAX_LOG_LINE - 1;
    }
    for (int i = 0; i < n && current_len < (MAX_LOG_LINE - 1); ++i) {
        unsigned char c = (unsigned char)buf[i];
        if (c >= 32 && c <= 126) {
            shared_log->log_buffer[slot][current_len++] = (char)c;
        } else {
            if (current_len + 5 < MAX_LOG_LINE) {
                current_len += snprintf(&shared_log->log_buffer[slot][current_len], 6, " 0x%02X", c);
            } else {
                break;
            }
        }
    }
    shared_log->log_buffer[slot][current_len] = '\0';
}

static void logger_signal_handler_ixxc(int sig)
{
    (void)sig;
    shutdown_requested_logger = 1;
}

void logger_loop_ixxc(void)
{
    signal(SIGTERM, logger_signal_handler_ixxc);
    signal(SIGINT,  logger_signal_handler_ixxc);
    signal(SIGPIPE, SIG_IGN);
    int log_fd = open_log_fd(LOG_FILE);
    if (log_fd < 0) {
        fprintf(stderr, "[ixxc_logger] open_log_fd failed\n");
        _exit(1);
    }
    off_t approx_size = 0;
    struct stat st;
    if (fstat(log_fd, &st) == 0) {
        approx_size = st.st_size;
    }
    while (!shutdown_requested_logger) {
        if (!shared_log) {
            sleep(1);
            continue;
        }
        unsigned int r_idx = atomic_load(&shared_log->read_index);
        unsigned int w_idx = atomic_load(&shared_log->write_index);
        if (r_idx == w_idx) {
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 50L * 1000L * 1000L };
            nanosleep(&ts, NULL);
            continue;
        }
        unsigned int slot = r_idx & (RING_BUFFER_SIZE - 1);
        const char *line = shared_log->log_buffer[slot];
        size_t len = strnlen(line, MAX_LOG_LINE);
        if (len > 0) {
            char out[MAX_LOG_LINE + 2];
            size_t copy = len < MAX_LOG_LINE ? len : MAX_LOG_LINE;
            memcpy(out, line, copy);
            out[copy] = '\n';
            out[copy + 1] = '\0';
            const char *p = out;
            size_t left = copy + 1;
            while (left > 0) {
                ssize_t w = write(log_fd, p, left);
                if (w < 0) {
                    if (errno == EINTR) continue;
                    perror("[ixxc_logger] write");
                    break;
                }
                left -= (size_t)w;
                p += w;
            }
            approx_size += (off_t)(copy + 1);
            if ((unsigned long long)approx_size >= MAX_LOG_SIZE) {
                fprintf(stderr, "[ixxc_logger] WARN: log size >= MAX_LOG_SIZE\n");
            }
        }
        atomic_store(&shared_log->read_index, r_idx + 1);
    }
    if (shared_log) {
        for (;;) {
            unsigned int r_idx = atomic_load(&shared_log->read_index);
            unsigned int w_idx = atomic_load(&shared_log->write_index);
            if (r_idx == w_idx) break;
            unsigned int slot = r_idx & (RING_BUFFER_SIZE - 1);
            const char *line = shared_log->log_buffer[slot];
            size_t len = strnlen(line, MAX_LOG_LINE);
            if (len > 0) {
                const char *p = line;
                size_t left = len;
                while (left > 0) {
                    ssize_t w = write(log_fd, p, left);
                    if (w < 0) {
                        if (errno == EINTR) {
                            continue;
                        } else {
                            break;
                        }
                    }
                    left -= (size_t)w;
                    p += w;
                }
                if (write(log_fd, "\n", 1) != 1) {
                    /* absichtlich ignoriert wir sind im Shutdown */
                }
            }
            atomic_store(&shared_log->read_index, r_idx + 1);
        }
    }
    if (log_fd >= 0) {
        fsync(log_fd);
        close(log_fd);
    }
    printf("[ixxc_logger] sauber beendet.\n");
    _exit(0);
}

pid_t fork_logger_ixxc(void)
{
    pid_t p = fork();
    if (p < 0) {
        perror("fork logger");
        exit(EXIT_FAILURE);
    }
    if (p == 0) {
        drop_privileges_and_set_name(DROP_USER, "ixxc_logger");
        logger_loop_ixxc();
        _exit(0);
    }
    return p;
}

int prepare_logging_subsystem_ixxc(void)
{
    if (prepare_log_file(LOG_FILE, DROP_USER, DROP_USER, 0640) != 0) {
        fprintf(stderr, "prepare_log_file failed for %s\n", LOG_FILE);
    }
    init_shared_log();
    if (!shared_log || shared_log == MAP_FAILED) {
        return -1;
    } else {
        return 0;
    }
}
