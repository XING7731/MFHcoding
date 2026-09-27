#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <mqueue.h>
#include <signal.h>
#include "common.h"

#define BUFFER_SIZE 8

typedef struct {
    sensor_msg_t buf[BUFFER_SIZE];
    int in;
    int out;
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
} ring_buffer_t;

static ring_buffer_t rb;
static mqd_t mq;
static volatile sig_atomic_t running = 1;

/* 信号处理 */
void handle_sigint(int sig) {
    running = 0;
}

/* 传感器线程 */
void *sensor_thread(void *arg) {
    char *type = (char *)arg;
    unsigned int seed = time(NULL) ^ pthread_self();

    while (running) {
        sensor_msg_t msg;
        strncpy(msg.type, type, sizeof(msg.type));

        if (strcmp(type, "temperature") == 0)
            msg.value = 15 + rand_r(&seed) % 21;
        else if (strcmp(type, "humidity") == 0)
            msg.value = 30 + rand_r(&seed) % 61;
        else
            msg.value = rand_r(&seed) % 101;

        msg.timestamp = time(NULL);

        pthread_mutex_lock(&rb.mutex);
        while ((rb.in + 1) % BUFFER_SIZE == rb.out && running)
            pthread_cond_wait(&rb.not_full, &rb.mutex);

        if (!running) break;

        rb.buf[rb.in] = msg;
        rb.in = (rb.in + 1) % BUFFER_SIZE;
        pthread_cond_signal(&rb.not_empty);
        pthread_mutex_unlock(&rb.mutex);

        sleep(1);
    }
    return NULL;
}

/* 发送线程 */
void *sender_thread(void *arg) {
    sensor_msg_t msg;

    while (running) {
        pthread_mutex_lock(&rb.mutex);
        while (rb.in == rb.out && running)
            pthread_cond_wait(&rb.not_empty, &rb.mutex);

        if (!running) break;

        msg = rb.buf[rb.out];
        rb.out = (rb.out + 1) % BUFFER_SIZE;
        pthread_cond_signal(&rb.not_full);
        pthread_mutex_unlock(&rb.mutex);

        mq_send(mq, (char *)&msg, sizeof(msg), 0);
    }
    return NULL;
}

int main() {
    signal(SIGINT, handle_sigint);

    pthread_t sensors[SENSOR_NUM], sender;
    char *types[SENSOR_NUM] = {"temperature", "humidity", "smoke"};

    rb.in = rb.out = 0;
    pthread_mutex_init(&rb.mutex, NULL);
    pthread_cond_init(&rb.not_full, NULL);
    pthread_cond_init(&rb.not_empty, NULL);

    mq = mq_open(MQ_NAME, O_CREAT | O_WRONLY, 0644, NULL);

    for (int i = 0; i < SENSOR_NUM; i++)
        pthread_create(&sensors[i], NULL, sensor_thread, types[i]);

    pthread_create(&sender, NULL, sender_thread, NULL);

    while (running) pause();

    pthread_mutex_destroy(&rb.mutex);
    pthread_cond_destroy(&rb.not_full);
    pthread_cond_destroy(&rb.not_empty);
    mq_close(mq);
    mq_unlink(MQ_NAME);

    return 0;
}