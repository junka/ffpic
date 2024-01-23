#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "queue.h"

void *test_enqueue(void *arg) {
    printf("new thread started\n");
    struct ring_queue *rq = (struct ring_queue *)arg;
    for (int i = 0; i < 64; i++) {
        char *k = calloc(1, 4);
        sprintf(k, "A%d", i);
        if (!ring_enqueue(rq, k)) {
            printf("enenque fail %d, total %d\n", i, ring_count(rq));
        }
    }
    return NULL;
}

int main() {
    struct ring_queue *rq = ring_alloc(128);
    if (!rq) {
        return -1;
    }

    pthread_t tid;
    pthread_create(&tid, NULL, test_enqueue, (void *)rq);
    for (int i = 0; i < 4; i++) {
        char *k = calloc(1, 4);
        sprintf(k, "a%d", i);
        if (!ring_enqueue(rq, k)) {
            printf("enenque fail %d\n", i);
        }
    }
    void *blk[28];
    for (int i = 4; i < 32; i++) {
        blk[i - 4] = calloc(1, 4);
        sprintf(blk[i - 4], "a%d", i);
    }
    if (!ring_enqueue_bulk(rq, blk, 28)) {
        printf("enqueu bulk fail\n");
    }
    usleep(1);
    for (int i = 32; i < 64; i++) {
        char *k = calloc(1, 4);
        sprintf(k, "a%d", i);
        if (!ring_enqueue(rq, k)) {
        printf("enenque fail %d\n", i);
        }
    }
    pthread_join(tid, NULL);

    char *c = ring_dequeue(rq);
    printf("%s\n", c);
    if (!c || strcmp(c, "a0")) {
        return -1;
    }
    free(c);
    c = ring_dequeue(rq);
    printf("%s\n", c);
    if (!c || strcmp(c, "a1")) {
        return -1;
    }
    free(c);
    c = ring_dequeue(rq);
    printf("%s\n", c);
    if (!c || strcmp(c, "a2")) {
        return -1;
    }
    free(c);
    if (ring_count(rq) != 124) {
        printf("ring count %d\n", ring_count(rq));
        return -1;
    }
    while (ring_count(rq)) {
        c = ring_dequeue(rq);
        if (c) {
            free(c);
        }
    }
    ring_free(rq);
    return 0;
}