#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "queue.h"

typedef struct {
    int index;
    int tag;
} tagged_index_t;

_Static_assert(sizeof(tagged_index_t) == sizeof(uintptr_t),
               "a tagged index must fit in one atomic pointer");

// head/tail are held in an atomic_uintptr_t, so the pair is copied byte for
// byte rather than read through a cast, which would break strict aliasing.
static inline uintptr_t tag_pack(tagged_index_t tagged) {
    uintptr_t raw;
    memcpy(&raw, &tagged, sizeof(raw));
    return raw;
}

static inline tagged_index_t tag_unpack(uintptr_t raw) {
    tagged_index_t tagged;
    memcpy(&tagged, &raw, sizeof(tagged));
    return tagged;
}

struct ring_queue *ring_alloc(int size) {
    struct ring_queue *queue;
    queue = (struct ring_queue *)malloc(sizeof(struct ring_queue));
    if (!queue) {
        return NULL;
    }

    atomic_init(&queue->head, tag_pack((tagged_index_t){0, 0}));
    atomic_init(&queue->tail, tag_pack((tagged_index_t){0, 0}));
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
    uintptr_t head, tail, next;
    int pos;

    do {
        tagged_index_t tail_tagged, head_tagged;
        tail = atomic_load(&queue->tail);
        head = atomic_load(&queue->head);
        tail_tagged = tag_unpack(tail);
        head_tagged = tag_unpack(head);

        int index = (tail_tagged.index + 1) % queue->size;
        if (index == head_tagged.index)
            return false;

        pos = tail_tagged.index;
        next = tag_pack((tagged_index_t){index, tail_tagged.tag + 1});
    } while (!atomic_compare_exchange_weak(&queue->tail, &tail, next));

    queue->queue[pos] = data;
    return true;
}

bool ring_enqueue_bulk(struct ring_queue *queue, void **data, int n) {
    uintptr_t head, tail, next;
    int pos;

    do {
        tagged_index_t tail_tagged, head_tagged;
        tail = atomic_load(&queue->tail);
        head = atomic_load(&queue->head);
        tail_tagged = tag_unpack(tail);
        head_tagged = tag_unpack(head);

        int avail = queue->size -
                    (queue->size + tail_tagged.index - head_tagged.index) %
                        queue->size;
        if (avail < n)
            return false;

        pos = tail_tagged.index;
        next = tag_pack(
            (tagged_index_t){tail_tagged.index + n, tail_tagged.tag + 1});
    } while (!atomic_compare_exchange_weak(&queue->tail, &tail, next));

    for (int i = 0; i < n; i++) {
        queue->queue[(pos + i) % queue->size] = data[i];
    }

    return true;
}

void *ring_dequeue(struct ring_queue *queue) {
    uintptr_t head, tail, next;
    int pos;

    do {
        tagged_index_t tail_tagged, head_tagged;
        tail = atomic_load(&queue->tail);
        head = atomic_load(&queue->head);
        tail_tagged = tag_unpack(tail);
        head_tagged = tag_unpack(head);

        if (head_tagged.index == tail_tagged.index) {
            return NULL;
        }

        pos = head_tagged.index;
        next = tag_pack((tagged_index_t){(head_tagged.index + 1) % queue->size,
                                         head_tagged.tag + 1});
    } while (!atomic_compare_exchange_weak(&queue->head, &head, next));

    return queue->queue[pos];
}

int ring_dequeue_bulk(struct ring_queue *queue, void **data, int n) {
    uintptr_t head, tail, next;
    int pos;

    do {
        tagged_index_t tail_tagged, head_tagged;
        tail = atomic_load(&queue->tail);
        head = atomic_load(&queue->head);
        tail_tagged = tag_unpack(tail);
        head_tagged = tag_unpack(head);

        if (head_tagged.index == tail_tagged.index) {
            return 0;
        }

        int inuse =
            (queue->size + tail_tagged.index - head_tagged.index) % queue->size;
        if (inuse < n) {
            return 0;
        }

        pos = head_tagged.index;
        next = tag_pack((tagged_index_t){(head_tagged.index + n) % queue->size,
                                         head_tagged.tag + 1});
    } while (!atomic_compare_exchange_weak(&queue->head, &head, next));

    for (int i = 0; i < n; i++) {
        data[i] = queue->queue[(pos + i) % queue->size];
    }

    return n;
}

int ring_count(struct ring_queue *queue) {
    uintptr_t head, tail;
    int size = 0;

    do {
        tagged_index_t tail_tagged, head_tagged;
        tail = atomic_load(&queue->tail);
        head = atomic_load(&queue->head);
        tail_tagged = tag_unpack(tail);
        head_tagged = tag_unpack(head);

        size =
            (queue->size + tail_tagged.index - head_tagged.index) % queue->size;
    } while (atomic_load(&queue->tail) != tail);

    return size;
}
