// Simulation of a Uber dispatch mechanism
// This is the only file where you need to write your own code!
// Your solution should not rely on modification to any other files!
// See dispatch.h for descriptions of the following functions
#include "dispatch.h"

/**
 ** Describe your synchronization strategy here:
 **
 ** The synchronization pattern I used for the dispatcher and driver
    threads is actually fairly simple and straightforward.  My shared
    state is the world_t object state which is initialized by the
    init_world function below.  The state is initialized to have a mutex
    for locking the state and three condition variables which are used
    to cause the dispatcher to wait to add more rides to the queue when 
    the queue is full, the drivers to wait when the queue is empty (or
    if the drivers have not been told to exit yet) and the dispatcher to
    wait while the dispatcher is done adding requests to the queue, but the
    drivers are not done completing rides yet.
    
    The dispatcher operates in a very simple way.  While the dispatcher is still
    receiving requests from stdin (ie it is still in the while(getline) loop in 
    dispatcher_thread) it takes in each request from stdin and dispatches the
    request with the dispatch function. In the dispatch function, the dispatch
    thread locks the mutex, increments total_rides, pushes the request onto the
    back of the queue and finally signals at least one driver waiting on the cond_empty
    CV to wake up because the queue is not empty.  If the queue is full, then our 
    dispatcher thread waits on the cond_full CV until a driver takes a request from 
    the queue and signals the dispatcher to wake up.  When requests are no longer being
    sent to stdin and the dispatcher breaks out of the while(getline) loop our dispatcher
    locks the mutex, sets the dispatcher_done variable to 1 and waits for a signal from
    the drivers on the cond_done CV.
    
    The driver thread locks the mutex and waits on the cond_empty CV if the queue is
    empty and the exit_drivers variable has not been set.  When the driver is woken up
    by the dispatcher thread, it checks if the exit_driver's variable is set and breaks
    the loop if it is (more about this later!).  Otherwise this means that people are 
    waiting for rides so we pop the next request off the queue and signal the dispatcher
    possibly waiting that the queue is no longer full. The mutex is unlocked and the thread
    completes the ride. After the ride is completed we re-lock the mutex and increment the
    rides_done variable.  The driver then checks if the dispatch_done variable is 1 and if
    the rides_done == total_rides.  If this is true then all rides are completed and the
    driver signals the dispatcher on the cond_done CV.
    
    When the dispatcher is woken up on the cond_done CV it sets the exit_drivers variable
    to 1 and then broadcasts to ALL drivers waiting on cond_empty to wake up.  The dispatcher's
    job is now officially done and the thread exits. Since all drivers are waiting on 
    cond_empty while the queue is empty and the exit_drivers variable is NOT 1, when all 
    of the drivers are woken up, they check and see that exit_drivers is 1 and break out of 
    the while loop. Because we broadcasted to all of the driver threads this allows all of
    our driver threads to eventually break out of the loop and exit.
 **/

int init_world(world_t* state) {
    // Your code here!
    state->request_queue = (queue_t*) malloc(sizeof(queue_t));
    if (!state || !state->request_queue) {
        return -1;
    }
    queue_init(state->request_queue);
    pthread_mutex_init(&state->lock, NULL);
    pthread_cond_init(&state->cond_full, NULL);
    pthread_cond_init(&state->cond_empty, NULL);
    pthread_cond_init(&state->cond_done, NULL);
    state->total_rides = 0;
    state->rides_done = 0;
    state->dispatch_done = 0;
    state->exit_drivers = 0;
    return 0;
}

void* dispatcher_thread(void* arg) {
    world_t* state = (world_t*)arg;
    char *line = NULL;
    size_t nbytes = 0;
    request_t* req;
    int scanned;
    // DO NOT change the following loop
    while(getline(&line, &nbytes, stdin) != -1) {
        req = (request_t*)malloc(sizeof(request_t));
        // Parse stdin inputs
        scanned = sscanf(line, "%lu %lu %f %f %f %f",
            &req->customer_id, &req->timestamp,
            &req->origin.longitude, &req->origin.latitude,
            &req->destination.longitude, &req->destination.latitude);
        assert(scanned == 6);
        dispatch(state, (void*)req);
        free(line);
        line = NULL;
        nbytes = 0;
    }
    
    pthread_mutex_lock(&state->lock);
    state->dispatch_done = 1;
    
    while (state->total_rides != state->rides_done)
        pthread_cond_wait(&state->cond_done, &state->lock);
        
    state->exit_drivers = 1;
    pthread_cond_broadcast(&state->cond_empty);
    pthread_mutex_unlock(&state->lock);
        
    return NULL;
}

// Implement the actual dispatch() and driver_thread() methods
void dispatch(world_t* state, void* req) {
    pthread_mutex_lock(&state->lock);
    state->total_rides++;
    while(size(state->request_queue) == MAX_QUEUE_SIZE)
        pthread_cond_wait(&state->cond_full, &state->lock);
        
    push_back(state->request_queue, req);
    
    pthread_cond_signal(&state->cond_empty);
    pthread_mutex_unlock(&state->lock);
    
}

void* driver_thread(void* arg) {
    world_t* state = (world_t*)arg;
    request_t* curr_req;
    
    //printf("driver %lu thread started\n", pthread_self());
    
    while (1) {
        pthread_mutex_lock(&state->lock);        
        //printf("driver %lu thread waiting on cond_empty\n", pthread_self());
        while (empty(state->request_queue) && !state->exit_drivers) {
            //printf("is empty: %d, exit_drivers: %d\n", empty(state->request_queue), state->exit_drivers);
            pthread_cond_wait(&state->cond_empty, &state->lock); 
        }
        //printf("driver %lu thread came out of wait\n", pthread_self());
        
        if (state->exit_drivers) {
            pthread_mutex_unlock(&state->lock);
            break;
        } else {            
            curr_req = (request_t*) pop_front(state->request_queue);
            
            pthread_cond_signal(&state->cond_full);
            pthread_mutex_unlock(&state->lock);
            
            drive(curr_req);
            
            pthread_mutex_lock(&state->lock);
            state->rides_done++;
            free(curr_req);
            if (state->dispatch_done && (state->rides_done == state->total_rides)) {
                free(state->request_queue);
                pthread_cond_signal(&state->cond_done);
                pthread_mutex_unlock(&state->lock);
                break;
            } else            
                pthread_mutex_unlock(&state->lock);
        }
    }
    
    //printf("driver thread %lu exiting\n", pthread_self());
    
    return NULL;
}
