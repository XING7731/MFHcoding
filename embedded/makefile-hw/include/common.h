#ifndef COMMON_H
#define COMMON_H

#include <time.h>

#define MQ_NAME "/smart_home_queue"
#define MQ_MSG_SIZE sizeof(sensor_msg_t)
#define SENSOR_NUM 3

typedef struct {
    char type[16];       // temperature / humidity / smoke
    float value;
    time_t timestamp;
} sensor_msg_t;

#endif