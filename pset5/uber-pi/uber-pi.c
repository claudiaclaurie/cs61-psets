#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// timestamp()
//    Return the current time as a double.

static inline double timestamp(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

#define NUM_THREADS 8
#define NUM_PASSENGERS NUM_THREADS
#define NUM_UBERS 4
#define CALCS_PER_UBER 1500000
#define RIDES_PER_PASSENGER 3
#define PI 3.14159265358979323846264338327

// created another mutex called status_lock to lock my array of statuses for the ubers
pthread_mutex_t uber_locks[NUM_UBERS];
pthread_mutex_t status_lock;

// created an array of uber_status to aid in my passenger_better_init
typedef enum _uber_status {not_in_use, in_use} uber_status;
volatile uber_status status[NUM_UBERS];

// variable used in my better_init function to cause a thread to sleep instead of
// attempting to acquire the status lock when it will most likely be blocked.
volatile int free_ubers;

double uber_times[NUM_UBERS];
volatile double inside[NUM_UBERS];
volatile double outside[NUM_UBERS];

static inline double rand_unif() {
	return (double)rand() / (double)RAND_MAX;
}

void drive(int thread_id, int uber_id) {
	(void) thread_id;
	
	//printf("me: %d is being driven by uber: %d.\n", thread_id, uber_id);

    double start_time = timestamp();
	double sample_x;
	double sample_y;
	double res;
	for (int k = 0; k < CALCS_PER_UBER; ++k) {
		sample_x = rand_unif();
		sample_y = rand_unif();

		res = pow(sample_x, 2) + pow(sample_y, 2);
		if (res < 1.0) {
			inside[uber_id]++;
		}
		else {
			outside[uber_id]++;
		}
	}

    uber_times[uber_id] += (timestamp() - start_time);

}

// Question to be answered in your README: Why is this function so slow?
void* passenger(void* params) {
	int me = (int)params;
	for (int k = 0; k < RIDES_PER_PASSENGER; ++k) {
		for (int i = 0; i < NUM_UBERS; ++i) {
			pthread_mutex_lock(&uber_locks[i]);
			drive(me, i);
			pthread_mutex_unlock(&uber_locks[i]);
			break;
		}
	}
	return NULL;
}

/*  So for my solution using passenger_better_init I use a global array of
    statuses for each uber and a global variable of how many ubers are 
    currently free along with a mutex associated withthese variables.  The way 
    it works is that each thread checks if there are any free ubers. If there 
    are not then it sleeps.  If there are free ubers then the thread will 
    attempt to acquire the status_lock to find which uber is free. This 
    implentation is not perfect though, as it is possible that after a
    thread passes the free_ubers condition, other threads could come in and
    ride in the uber that was just free which would cause the original thread to
    block on status_lock.  So there is the possibility of threads getting stuck
    but at least it is not possible for any two threads to occupy the same uber
    at the same time.  I originally was using a CV for the free_ubers condition
    until I saw a piazza post stating that we could only use mutexes. Now it seems 
    to be performing correctly in the sense that each passenger rides three times 
    total and the rides are evenly distributed among the different uber drivers 
    and the fraction of time the Uber drivers were driving is high, but when I 
    use the debugging printf statement in the drive function I noticed some
    strange scheduling.  So in the default case where there are 8 passengers and 
    4 ubers, it seems as if the first 4 passengers ride their ubers 3 times, 
    and then the next 4 passengers ride their ubers 3 times.  I'm not sure 
    if this is a scheduling quirk or if my implementation has some sort of fault.
*/

void* passenger_better_init(void* params) {
	int me = (int)params;
	int uber_id;
	for (int k = 0; k < RIDES_PER_PASSENGER; ++k) {
		while (free_ubers == 0) 
		    usleep(1);
		
		pthread_mutex_lock(&status_lock);
		for (int i = 0; i < NUM_UBERS; ++i) { 		        
		        if (status[i] == in_use)
		        continue;
		    else {
                free_ubers--;
                status[i] = in_use;
                uber_id = i;
			    break;
			}
	    }
	    pthread_mutex_unlock(&status_lock);
	    //printf("me: %d STARTING RIDE with uber: %d\n", me, uber_id);
	    drive(me, uber_id);
	    //printf("me: %d ENDING RIDE with uber: %d\n", me, uber_id);	
		pthread_mutex_lock(&status_lock);
		free_ubers++;
		status[uber_id] = not_in_use;
		pthread_mutex_unlock(&status_lock);		
	}
	return NULL;
}

/*  For my trylock implementation I took a simple approach using the uber_locks array instead
    of my status array. Basically how it works is each thread loops through the array of uber
    locks and attempts to lock it with a call to try_lock.  If the call is successful then we
    run the drive function with the index of our uber_lock and then we unlock the lock and break
    out of the loop. If the call to trylock is not successful then we simply increment our idx
    and mod the idx by NUM_UBERS to loop back to 0.  Now it seems to be performing correctly
    but I am having the same scheduling quirk that is occuring in my better_init implementation.

*/
void* passenger_trylock(void* params) {
	int me = (int)params;
	for (int k = 0; k < RIDES_PER_PASSENGER; ++k) {	
		int idx = 0;
		int needs_ride = 1;
		while (needs_ride) {
		    if (pthread_mutex_trylock(&uber_locks[idx]) == 0) {
	            //printf("me: %d STARTING RIDE with uber: %d\n", me, idx);
	            drive(me, idx);
	            //printf("me: %d ENDING RIDE with uber: %d\n", me, idx);
		        pthread_mutex_unlock(&uber_locks[idx]);
		        needs_ride = 0;
		    } else {
		        idx++;
		        idx = idx % NUM_UBERS;
		    }		    
		}
	}
	return NULL;
}

static void print_usage() {
	printf("Usage: ./uber-pi [PASSENGER_TYPE]\n");
	exit(1);
}

int main (int argc, char** argv) {
	srand((unsigned)time(NULL));
	pthread_t threads[NUM_THREADS];

	if (argc < 2) {
		print_usage();
	}
    
	for (int j = 0; j < NUM_UBERS; ++j) {
		pthread_mutex_init(&uber_locks[j], NULL);
		status[j] = not_in_use;
	}
	
	// Here we initialize our free_ubers variable, our status_lock and status_cv
	// for use in our passenger_better_init
	free_ubers = NUM_UBERS;
	pthread_mutex_init(&status_lock, NULL);
    
    double timevar = timestamp();

	for (long long i = 0; i < NUM_PASSENGERS; ++i) {
		if (strcmp(argv[1], "2") == 0) {
			if (pthread_create(&threads[i], NULL, passenger_trylock, (void *)i)) {
				printf("pthread_create failed\n");
				exit(1);
			}
		}
		else if (strcmp(argv[1], "1") == 0) {
			if (pthread_create(&threads[i], NULL, passenger_better_init, (void *)i)) {
				printf("pthread_create failed\n");
				exit(1);
			}
		}
		else if (strcmp(argv[1], "0") == 0) {
			if (pthread_create(&threads[i], NULL, passenger, (void *)i)) {
				printf("pthread_create failed\n");
				exit(1);
			}
		}
		else {
			print_usage();
		}
	}

	for (int i = 0; i < NUM_PASSENGERS; ++i) {
		pthread_join(threads[i], NULL);
	}
	
	// added lines to destroy mutexes and cvs after joining
	for (int k = 0; k < NUM_UBERS; ++k) {
	    pthread_mutex_destroy(&uber_locks[k]);
    }
    // Here is where we destroy our status_lock
    pthread_mutex_destroy(&status_lock);

    
    timevar = (timestamp() - timevar);

	double inside_sum = 0;
	double outside_sum = 0;
    double total_uber_time = 0.0;
	for (int u = 0; u < NUM_UBERS; ++u) {
		inside_sum += inside[u];
		outside_sum += outside[u];
        total_uber_time += uber_times[u];
	}

	double mc_pi = 4.0 * inside_sum/(inside_sum + outside_sum);

    printf("Average fraction of time Uber drivers were driving: %5.3f\n",
            (total_uber_time / NUM_UBERS) / timevar);
	printf("Value of pi computed was: %f\n", mc_pi);
	if (!(fabs(mc_pi - PI) < 0.02)) {
		printf("Your computation of pi was not very accurate, something is probably wrong!\n");
		exit(1);
	}
	return 0;
}
