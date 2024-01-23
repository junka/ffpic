#ifndef __QUEUE_H__
#define __QUEUE_H__

#include <stdatomic.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ring_queue {
    atomic_uintptr_t head;
    atomic_uintptr_t tail;
    int size;
    void **queue;
};

/**
 * It allocates a ring buffer of size `size` and returns a pointer to it.
 *
 * @param size The size of the ring buffer.
 *
 * @return A pointer to a ring_queue struct.
 */
struct ring_queue *ring_alloc(int size);

/**
 * "If the queue is not empty, then remove the first element from the queue and
 * return it."
 *
 * The first thing we do is check to see if the queue is empty. If it is, then
 * we return NULL
 *
 * @param queue The queue to free.
 */
void ring_free(struct ring_queue *queue);

/**
 * "Try to increment the tail pointer, and if successful, write the data to the
 * queue."
 *
 * The first thing we do is load the tail pointer. We then load the head
 * pointer. We check if the queue is full by comparing the next index of the
 * tail pointer to the head pointer. If the queue is full, we return false
 *
 * @param queue The ring queue to enqueue to.
 * @param data The data to be enqueued.
 *
 * @return A pointer to the data that was dequeued.
 */
bool ring_enqueue(struct ring_queue *queue, void *data);

/**
 * It tries to enqueue a bunch of items, and if it can't, it returns false
 *
 * @param queue the ring queue
 * @param data the array of data to be enqueued
 * @param n the number of items to enqueue
 *
 * @return A boolean value.
 */
bool ring_enqueue_bulk(struct ring_queue *queue, void **data, int n);

/**
 * "If the queue is not empty, remove the first element from the queue and
 * return it."
 *
 * The first thing we do is load the head and tail pointers. We then check if
 * the queue is empty. If it is, we return NULL. If it isn't, we increment the
 * head pointer and store it back in the queue. We then return the data at the
 * old head position
 *
 * @param queue The queue to dequeue from.
 *
 * @return A pointer to the data at the head of the queue.
 */
void *ring_dequeue(struct ring_queue *queue);

/**
 * It tries to dequeue n items from the queue, and returns the number of items
 * actually dequeued
 *
 * @param queue the ring queue
 * @param data the array of data to be enqueued
 * @param n the number of elements to dequeue
 *
 * @return The number of items dequeued, return n or 0.
 */
int ring_dequeue_bulk(struct ring_queue *queue, void **data, int n);

/**
 * > If the tail and head pointers are the same, then the queue is empty.
 * Otherwise, the queue size is the difference between the tail and head
 * pointers, modulo the queue size
 *
 * @param queue The queue to count the number of elements in.
 *
 * @return The number of elements in the queue.
 */
int ring_count(struct ring_queue *queue);

#ifdef __cplusplus
}
#endif

#endif
