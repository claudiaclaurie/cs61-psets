/**
 * The main thread for Ubler drivers.
 *
 * Assignment:
 *   Synchronize multiple driver threads as they pick up meals or customers.
 *   You'll need some sort of locking mechanisms as drivers try to access the same
 *     meals or customers, but you'll also need to be careful about the interaction
 *     between meals and customers.  Be careful that your solution doesn't create
 *     deadlock.
 *
 * Extra challenge:
 *   Deal with restaurants who mix their customers orders up.  You'll need to call
 *     fix_mismatch to fix the mixup.  Of course, fix_mismatch
 *     isn't synchronized, so you'll have to synchronize it!  Be careful about which
 *     meals and which customers you've already synchronized when you try to do this.
 *   If you'd like to try this part out, you can pass the --mix-up-meals option
 *     to ubler_test to have 50% of the restaurants mix their meals up.  You can
 *     also run make extra to run tests that automatically test this functionality.
 *   
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include "ubler.h"
#include "ubler_helpers.h"

/**
 * Describe your synchronization strategy here:
 *   The synchronizatin strategy I used for my ubler implementation is actually
     quite simple. I simply added a mutex to the customer struct, initialized
     the mutex in the customer init function and destroyed the mutex in the 
     customer cleanup function.  Originally I had a mutex for both the customers
     and the meal structs but realized that we lookup the customer and meal in both 
     the pick_up_customer and pick_up_meal cases, so having mutexes in both cases might
     cause unneeded blocking by other threads with the same customer/meal. Since we do
     not want one driver picking up a customer and one driver picking up the meal, it
     made sense to have a mutex associated with each customer/meal pair rather than a 
     mutex for each struct but it does work in either case.
     
     How it all works is basically the same thing in both the customer and meal cases.
     The driver receives a request from the helper function and either casts the request
     to a customer or meal struct.  The driver then looks up the associated meal/customer
     for the request so customer and meal are stored in local variables. My driver then
     checks if either has been picked up, continuing to the next iteration of the loop if
     that's the case, otherwise the driver looks up the source and destination locations.
     Now since I am not holding a mutex we are not guarenteed that this will still be true
     by the time we drive to the source location, but this is just a quick check to avoid
     looking up the source and destination locations unnecessarily. After all of this is 
     where we lock our request mutex, located in the customer struct and check the conditions
     again. If another driver has picked up the meal or customer then we don't bother and 
     simply unlock the mutex, otherwise we drive to the source destination and pick up our 
     meal or customer before unlocking the mutex.  Because we hold the customer mutex until 
     after we pick up the customer/meal, which changes the private_stats that our if condition 
     checks for whichever struct we pick up, any other thread that is waiting on that mutex 
     will wake up and check the condition again, realize it is too late, unlock the mutex and 
     go through the loop again, getting a new request.  This prevents two drivers from 
     attempting to complete the same request at the same time, whether both drivers are going 
     for the same meal/customer or if one driver is going for the meal and one is going for 
     the customer.  The most important thing for my synchronization period is that no two
     drivers can change the private_statistics of a request's meal or customer without holding
     the customer's lock, ie our driver_pick_meal_up/customer_up is the critical section.
     
     I also included driving to the source location in my critical section, although it is not
     completely neccesary for my strategy to work.  My reasoning behind this was that in a 
     real-life business situation you wouldn't send two ubler's to the same customer/meal, which
     makes them race to the location.  But I did let this case occur while testing out different
     methods and saw similar results when running the make check functions and running it using
     the Unix "time" command.  Also as I stated before my implementation lets multiple drivers
     have the same request, whether it is the meal or customer side of it, but once a driver 
     starts driving to the location the other drivers are too late!
 */

// Ignore this function unless you are doing the extra challenge
int fix_mismatch(struct meal *mealA);


/**
 * The main driver thread.  This is where most of your changes should go.
 * You should also change the meal/customer init/cleanup functions, and the
 *   meal and customer structs
 */
void *driver_thread(void *driver_arg)
{
    struct driver *driver = (struct driver *)driver_arg;

    while (1)
    {
        void *request;
        bool isCustomer;
        int result = receive_request(&request, &isCustomer);
        if (result != 0)
        {
            // no more destinations left!
            break;
        }
        if (isCustomer)
        {
            struct customer *customer = (struct customer *)request;
            
            // you might want some synchronization here for part two...

            struct meal *meal = NULL;
            customer_get_meal(customer, &meal);    
            
            if (customer_picked_up(customer) != 0 &&
                meal_picked_up(meal) != 0)
            {
                continue;
            }

            // you might want some synchronization here for parts one and two...
            // you might want some checks here for part two...
            
            struct restaurant *restaurant = NULL;
            meal_get_restaurant(meal, &restaurant);

            struct location *srcLocation = NULL;
            customer_get_location(customer, &srcLocation);

            struct location *destLocation = NULL;
            restaurant_get_location(restaurant, &destLocation);

            // you might want some synchronization here for part one...             
            pthread_mutex_lock(&customer->lock);
            
            if (customer_picked_up(customer) == 0 &&
                meal_picked_up(meal) == 0)
            {
                driver_drive_to_location(driver, srcLocation);
                driver_pick_customer_up(driver, customer);
                //printf("Driver %lu picked up customer %p\n", pthread_self(), customer);  
                pthread_mutex_unlock(&customer->lock);
                driver_drive_to_location(driver, destLocation);
                driver_drop_off_customer(driver, customer, restaurant);
                //printf("Driver %lu dropped off customer %p\n", pthread_self(), customer);
            } else {
                pthread_mutex_unlock(&customer->lock);                  
            }
   
        }
        else
        {
            struct meal *meal = (struct meal *)request;

            // you might want some synchronization here for part two...

            struct customer *customer = NULL;
            meal_get_customer(meal, &customer);

            if (customer_picked_up(customer) != 0 &&
                meal_picked_up(meal) != 0)
            {
                continue;
            }
            
            struct restaurant *restaurant = NULL;
            meal_get_restaurant(meal, &restaurant);

            struct location *srcLocation = NULL;
            restaurant_get_location(restaurant, &srcLocation);

            struct location *destLocation = NULL;
            customer_get_location(customer, &destLocation);

            // you might want some synchronization here for part one...            
            pthread_mutex_lock(&customer->lock);
            
            if (meal_picked_up(meal) == 0 &&
                customer_picked_up(customer) == 0)
            {
                driver_drive_to_location(driver, srcLocation);                             
                driver_pick_meal_up(driver, meal);
                //printf("Driver %lu picked up meal: %p\n", pthread_self(), meal);   
                pthread_mutex_unlock(&customer->lock);    
                driver_drive_to_location(driver, destLocation);
                driver_drop_off_meal(driver, meal, customer);
                //printf("Driver %lu dropped of meal: %p to cust: %p\n", pthread_self(), meal, customer);   
            } else {
                pthread_mutex_unlock(&customer->lock);                  
            }      
        }
    }

    return NULL;
}

/**
 * Some meal/customer code you may want to change
 */
// init/cleanup meal
void init_meal(struct meal *meal)
{
    meal->stats = private_tracking_create();

    meal->customer = NULL;
    meal->restaurant = NULL;
    
}
void cleanup_meal(struct meal *meal)
{
    private_tracking_destroy(meal->stats);
    
}

// init/cleanup customer
void init_customer(struct customer *customer)
{
    customer->stats = private_tracking_create();

    customer->meal = NULL;
    customer->location = NULL;
    
    pthread_mutex_init(&customer->lock, NULL);

}
void cleanup_customer(struct customer *customer)
{
    private_tracking_destroy(customer->stats);
    pthread_mutex_destroy(&customer->lock);
}

// Ignore this function unless you are doing the extra challenge
// All the mismatches are of the following form:
//   customer A wants meal A, and customer A has a pointer to meal A
//   customer B wants meal B, and customer B has a pointer to meal B
//   BUT meal A has a pointer to customer B, and meal B has a pointer to customer A
//   
// you will probably have to change this function for part two, maybe even use a trylock...
// 
// This function returns 0 on success and -1 on failure
int fix_mismatch(struct meal *mealA)
{
    struct customer *customerB = NULL;
    meal_get_customer(mealA, &customerB);
    

    struct meal *mealB= NULL;
    customer_get_meal(customerB, &mealB);

    struct customer *customerA = NULL;
    meal_get_customer(mealB, &customerA);

    meal_set_customer(mealA, customerA);
    meal_set_customer(mealB, customerB);

    return 0;
}
