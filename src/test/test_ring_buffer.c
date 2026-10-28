#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "../../header/test/test_requests.h"

#define TARGET_IP   "127.0.0.1"
#define TARGET_PORT 8080
#define NUM_THREADS 4
#define REQ_PER_ITER 2

static volatile int running = 1;
static volatile int smmm = 300;

static int send_request(const char *req)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(TARGET_PORT);
    if (inet_pton(AF_INET, TARGET_IP, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    char buf[2048];
    if (strncmp(req, "GET ", 4) == 0 ||
        strncmp(req, "POST ", 5) == 0 ||
        strncmp(req, "PUT ", 4) == 0 ||
        strncmp(req, "HEAD ", 5) == 0) {
        snprintf(buf, sizeof(buf), "%s\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
                 req, TARGET_IP, TARGET_PORT);
        } else {
            snprintf(buf, sizeof(buf),
                     "GET /%s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
                     req, TARGET_IP, TARGET_PORT);
        }
        ssize_t sent = send(sock, buf, strlen(buf), 0);
        if (sent < 0) {
            perror("send");
        }
        /* Drain any response (optional) */
        char tmp[512];
        while (recv(sock, tmp, sizeof(tmp), 0) > 0) { /* discard */ }
        close(sock);
        return (sent > 0) ? 0 : -1;
}

static void *worker(void *arg)
{
    int tid = *(int *)arg;
    unsigned long long idx = tid;   /* each thread starts at a different offset */
    printf("[thread %d] started\n", tid);
    while (smmm >= 0) {
        for (int i = 0; i < REQ_PER_ITER; i++) {
            const char *req = x[idx % x_count];
            send_request(req);
            idx++;
            smmm--;
        }
        sleep(1);
    }
    printf("[thread %d] exiting\n", tid);
    return NULL;
}

int main(void)
{
    pthread_t threads[NUM_THREADS];
    int tids[NUM_THREADS];

    printf("x table has %d requests\n", x_count);
    printf("starting %d threads, each sends %d requests per iteration (infinite loop)\n",
           NUM_THREADS, REQ_PER_ITER);
    printf("target: %s:%d\n", TARGET_IP, TARGET_PORT);

    for (int i = 0; i < NUM_THREADS; i++) {
        tids[i] = i;
        if (pthread_create(&threads[i], NULL, worker, &tids[i]) != 0) {
            perror("pthread_create");
            running = 0;
            break;
        }
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
