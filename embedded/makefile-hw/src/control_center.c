#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <mqueue.h>
#include <signal.h>
#include "common.h"

static mqd_t mq;
static volatile sig_atomic_t running = 1;
static pthread_mutex_t log_mutex;

void handle_sigint(int sig) {
    running = 0;
}

/* 日志线程安全 */
void write_log(const char *msg) {
    pthread_mutex_lock(&log_mutex);
    FILE *fp = fopen("smart_home.log", "a");
    fprintf(fp, "%s\n", msg);
    fclose(fp);
    pthread_mutex_unlock(&log_mutex);
}

/* 接收线程 */
void *receiver_thread(void *arg) {
    sensor_msg_t msg;
    char log[256];

    while (running) {
        if (mq_receive(mq, (char *)&msg, sizeof(msg), NULL) < 0)
            continue;

        snprintf(log, sizeof(log),
                 "[%.19s] %s = %.2f",
                 ctime(&msg.timestamp), msg.type, msg.value);
        write_log(log);

        if (strcmp(msg.type, "temperature") == 0 && msg.value > 30)
            write_log("[CONTROL] Air Conditioner ON");

        if (strcmp(msg.type, "humidity") == 0 && msg.value < 40)
            write_log("[CONTROL] Humidifier ON");

        if (strcmp(msg.type, "smoke") == 0 && msg.value > 60)
            write_log("[ALARM] Fire Alert!");
    }
    return NULL;
}

int main() {
    signal(SIGINT, handle_sigint);
    pthread_t recv_thread;

    pthread_mutex_init(&log_mutex, NULL);
    mq = mq_open(MQ_NAME, O_CREAT | O_RDONLY, 0644, NULL);

    pthread_create(&recv_thread, NULL, receiver_thread, NULL);
    pthread_join(recv_thread, NULL);

    pthread_mutex_destroy(&log_mutex);
    mq_close(mq);
    mq_unlink(MQ_NAME);
    return 0;
}