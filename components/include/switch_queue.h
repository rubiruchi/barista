/*
 * Copyright 2015-2018 NSSLab, KAIST
 */

/**
 * \file
 * \author Jaehyun Nam <namjh@kaist.ac.kr>
 */

// inside of 'switch_mgmt.c'

/////////////////////////////////////////////////////////////////////

/** \brief The number of pre-allocated switches */
#define SW_PRE_ALLOC 1024

/** \brief The structure of a switch pool */
typedef struct _sw_queue_t {
    int size; /**< the number of entries */

    switch_t *head; /**< The head pointer */
    switch_t *tail; /**< The tail pointer */

    pthread_spinlock_t lock; /**< The lock for management */
} sw_queue_t;

/** \brief Switch pool */
sw_queue_t sw_q;

/////////////////////////////////////////////////////////////////////

/**
 * \brief Function to pop a switch from a switch pool
 * \return Empty switch
 */
static switch_t *sw_dequeue(void)
{
    switch_t *new = NULL;

    pthread_spin_lock(&sw_q.lock);

    if (sw_q.head == NULL) {
        pthread_spin_unlock(&sw_q.lock);

        new = (switch_t *)CALLOC(1, sizeof(switch_t));
        if (new == NULL) {
            PERROR("calloc");
            return NULL;
        }

        pthread_spin_lock(&sw_q.lock);
        sw_q.size--;
        pthread_spin_unlock(&sw_q.lock);

        return new;
    } else if (sw_q.head == sw_q.tail) {
        new = sw_q.head;
        sw_q.head = sw_q.tail = NULL;
    } else {
        new = sw_q.head;
        sw_q.head = sw_q.head->next;
        sw_q.head->prev = NULL;
    }

    sw_q.size--;

    pthread_spin_unlock(&sw_q.lock);

    memset(new, 0, sizeof(switch_t));

    return new;
}

/**
 * \brief Function to push a used switch into a switch pool
 * \param old Used switch
 */
static int sw_enqueue(switch_t *old)
{
    if (old == NULL)
        return -1;

    memset(old, 0, sizeof(switch_t));

    pthread_spin_lock(&sw_q.lock);

    if (sw_q.size < 0) {
        sw_q.size++;

        pthread_spin_unlock(&sw_q.lock);

        FREE(old);

        return 0;
    }

    if (sw_q.tail == NULL) {
        sw_q.head = sw_q.tail = old;
    } else {
        sw_q.tail->next = old;
        old->prev = sw_q.tail;
        sw_q.tail = old;
    }

    sw_q.size++;

    pthread_spin_unlock(&sw_q.lock);

    return 0;
}

/**
 * \brief Function to initialize a switch pool
 * \return None
 */
static int sw_q_init(void)
{
    memset(&sw_q, 0, sizeof(sw_queue_t));
    pthread_spin_init(&sw_q.lock, PTHREAD_PROCESS_PRIVATE);

    int i;
    for (i=0; i<SW_PRE_ALLOC; i++) {
        switch_t *new = (switch_t *)MALLOC(sizeof(switch_t));
        if (new == NULL) {
            PERROR("malloc");
            return -1;
        }

        sw_enqueue(new);
    }

    return 0;
}

/**
 * \brief Function to destroy a switch pool
 * \return None
 */
static int sw_q_destroy(void)
{
    pthread_spin_lock(&sw_q.lock);

    switch_t *curr = sw_q.head;
    while (curr != NULL) {
        switch_t *tmp = curr;
        curr = curr->next;
        FREE(tmp);
    }

    pthread_spin_unlock(&sw_q.lock);
    pthread_spin_destroy(&sw_q.lock);

    return 0;
}

