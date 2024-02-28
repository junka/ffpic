#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "queue.h"

typedef struct {
    int index;
    int tag;
} tagged_index_t;

struct ring_queue *ring_alloc(int size) {
    struct ring_queue *queue;
    queue = (struct ring_queue *)malloc(sizeof(struct ring_queue));
    if (!queue) {
        return NULL;
    }

    tagged_index_t head = {0, 0};
    tagged_index_t tail = {0, 0};

    atomic_init(&queue->head, *(uintptr_t *)&head);
    atomic_init(&queue->tail, *(uintptr_t *)&tail);
    queue->size = size;
    queue->queue = (void **)malloc(size * sizeof(void *));
    if (!queue->queue) {
        free(queue);
        return NULL;
    }

    return queue;
}

void ring_free(struct ring_queue *queue) {
    if (queue) {
        free(queue->queue);
        free(queue);
    }
}

bool ring_enqueue(struct ring_queue *queue, void *data) {
    uintptr_t head, tail;
    tagged_index_t *head_ptr, *tail_ptr;
    tagged_index_t new_tail;
    int pos;

    do {
        tail = atomic_load(&queue->tail);
        tail_ptr = (tagged_index_t *)&tail;

        head = atomic_load(&queue->head);
        head_ptr = (tagged_index_t *)&head;
        int next = (tail_ptr->index + 1) % queue->size;
        if (next == head_ptr->index)
            return false;

        pos = tail_ptr->index;
        new_tail.index = next;
        new_tail.tag = tail_ptr->tag + 1;
    } while (!atomic_compare_exchange_weak(&queue->tail, &tail,
                                            *(uintptr_t *)&new_tail));

    queue->queue[pos] = data;
    return true;
}

bool ring_enqueue_bulk(struct ring_queue *queue, void **data, int n) {
    uintptr_t head, tail;
    tagged_index_t *head_ptr, *tail_ptr;
    tagged_index_t new_tail;
    int pos;
    int avail;
    do {
        tail = atomic_load(&queue->tail);
        tail_ptr = (tagged_index_t *)&tail;

        head = atomic_load(&queue->head);
        head_ptr = (tagged_index_t *)&head;

        avail = queue->size -
                (queue->size + tail_ptr->index - head_ptr->index) % queue->size;

        if (avail < n)
            return false;

        pos = tail_ptr->index;
        new_tail.index = tail_ptr->index + n;
        new_tail.tag = tail_ptr->tag + 1;
    } while (!atomic_compare_exchange_weak(&queue->tail, &tail,
                                            *(uintptr_t *)&new_tail));

    for (int i = 0; i < n; i++) {
        queue->queue[(pos + i) % queue->size] = data[i];
    }

    return true;
}

void *ring_dequeue(struct ring_queue *queue) {
    uintptr_t head, tail;
    tagged_index_t *head_ptr, *tail_ptr;
    tagged_index_t new_head;
    void *data = NULL;
    int pos;

    do {
        tail = atomic_load(&queue->tail);
        tail_ptr = (tagged_index_t *)&tail;
        head = atomic_load(&queue->head);
        head_ptr = (tagged_index_t *)&head;

        if (head_ptr->index == tail_ptr->index) {
            return NULL;
        }

        pos = head_ptr->index;
        new_head.index = (head_ptr->index + 1) % queue->size;
        new_head.tag = head_ptr->tag + 1;

    } while (!atomic_compare_exchange_weak(&queue->head, &head,
                                            *(uintptr_t *)&new_head));

    data = queue->queue[pos];
    return data;
}

int ring_dequeue_bulk(struct ring_queue *queue, void **data, int n) {
    uintptr_t head, tail;
    tagged_index_t *head_ptr, *tail_ptr;
    tagged_index_t new_head;
    int inuse = 0;
    int pos;

    do {
        tail = atomic_load(&queue->tail);
        tail_ptr = (tagged_index_t *)&tail;
        head = atomic_load(&queue->head);
        head_ptr = (tagged_index_t *)&head;

        if (head_ptr->index == tail_ptr->index) {
            return 0;
        }

        inuse = (queue->size + tail_ptr->index - head_ptr->index) % queue->size;
        if (inuse < n) {
        return 0;
        }
        pos = head_ptr->index;
        new_head.index = (head_ptr->index + n) % queue->size;
        new_head.tag = head_ptr->tag + 1;

    } while (!atomic_compare_exchange_weak(&queue->head, &head,
                                            *(uintptr_t *)&new_head));

    for (int i = 0; i < n; i++) {
        data[i] = queue->queue[(pos + i) % queue->size];
    }

    return n;
}

int ring_count(struct ring_queue *queue) {
    uintptr_t tail, head;
    tagged_index_t *head_ptr, *tail_ptr;
    int size = 0;

    do {
        tail = atomic_load(&queue->tail);
        tail_ptr = (tagged_index_t *)&tail;
        head = atomic_load(&queue->head);
        head_ptr = (tagged_index_t *)&head;
        size = (queue->size + tail_ptr->index - head_ptr->index) % queue->size;
    } while (atomic_load(&queue->tail) != tail);

    return size;
}
