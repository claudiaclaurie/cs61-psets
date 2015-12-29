#pragma once

#include <stdlib.h>
#include "ubler_helpers.h"

/**
 * The definitions of meal and customer. You will probably have to change
 * these structs.
 */
struct meal
{
    struct private_tracking *stats;

    struct customer *customer;
    struct restaurant *restaurant;

};

struct customer
{
    struct private_tracking *stats;

    struct meal *meal;
    struct location *location;

    pthread_mutex_t lock;

};

// init/cleanup meal
void init_meal(struct meal *meal);
void cleanup_meal(struct meal *meal);

// init/cleanup customer
void init_customer(struct customer *customer);
void cleanup_customer(struct customer *customer);

void *driver_thread(void *driver_arg);
