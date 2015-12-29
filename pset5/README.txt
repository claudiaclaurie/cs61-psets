GRADE ME

README for CS 61 Problem Set 5
------------------------------
OTHER COLLABORATORS AND CITATIONS (if any):



NOTES FOR THE GRADER (if any):


WP-1: For this first situation I believe that a lock with a condition variable
      would be the best choice.  This is because of the fact that FIFA demands
      the primitives to be high performance and if we simply used a lock without
      a condition variable, each player would be doing a lot of busy waiting, 
      constantly checking to see if the ball's "lock" was held by another player.
      With the CV we could simply broadcast to all players that the ball is free
      to headbutt once the player is done headbutting.  Also a situation where
      several players are trying to headbutt a soccer ball would not require the
      players to queue up in a certain order to headbutt the ball, so it would make
      sense to broadcast the signal to all players. It's worth noting that this could
      also probably be accomplished with a binary semaphore.

WP-2: For the pancake situation I think it would be best to use a counting semaphore
      to distribute the pancakes amongst consumers effectively.  This can be thought of
      as the popular "producer-consumer problem" where Professor Seltzer is the producer
      and the hungry teenagers are the consumers. Whenever the pancake buffer is empty,
      Professor Seltzer will wake up and fill it back up, which wakes up our teenagers.  
      The teenagers will continuously take pancakes from the buffer and then sleep when 
      the pancake buffer is empty, notifiying the Professor to wake up and make more 
      pancakes.

WP-3: For the distributed relay situation I believe that the easiest solution to this
      problem would be be to simply use a lock with a condition variable.  Instead of
      passing a baton we could be passing a lock from one runner to the other.  That is,
      while the first runner is completing their lap the other runners will be sleeping 
      waiting to be signaled about the runner's completion.  The reason we need a condition
      variable instead of just using a lock is that we need to make sure we signal the
      correct runner to start next.  If we just used a lock, then it is possible that the
      Brown or Columbia track runner would obtain the lock after the Harvard runner completes
      the lap instead of the Yale runner.  We could use this by a single signal instead of
      broadcasting the signal to all runners.
      
WP-4: For this parking situation the best solution in my mind would be to implement a lock
      for each separate space in each neighborhood. In addition to this each neighborhood
      could have a counting semaphore initialized to the number of spaces in the neighborhood
      and an array of status information for each space in the neighborhood with an associated
      lock. When a car drives up to the check-in point, they do a P on the counting semaphore. 
      If there are spots left (the P was not blocked) then the status mutex is locked, a free
      space is locked by the car and the status mutex is unlocked.  When a car leaves they do a
      V on the semaphore, the status is locked and updated, the space's lock is released and then
      the status lock is released.  This will have to be implemented in the correct order to avoid
      deadlock but I believe something like this would be a solution to our predicament.

WP-5: I think the best wait to fix this race condition would be to implement a condition variable
      and mutex with their Bayesian filter.  The condition variable and mutex will be associated with
      some global variable "food_email_detected" so while the graduate student is doing research,
      they will be waiting on the food_email_detected variable (while(food_email_detected == 0)
      pthread_cond_wait(&email_cv, &email_lock)).  The Bayesian filter will be configured to 
      signal the graduate student (our thread) to check their email when an email comes in.  The
      student will then lock the email mutex and check all of their emails before resetting the
      food_email_detected email back to 0 and unlocking the email_lock.      

WP-6: The reason that the "passenger" implementation is so limited in its performance is because
      we are only using one of our Ubers to drive each passenger around.  I added a printf line
      to the drive function and realized that only uber 0 was actually doing any work.  This is 
      because in the passenger function each passenger loops through the list of Ubers 3 times for
      three different rides.  But instead of grabbing the next available uber, all of the passengers
      are attempting to acquire the lock of Uber #0 and then breaking out of the loop.  We need
      to instead keep track of which Uber's are available so all of the passengers do not end up
      waiting on the same car.  If an Uber is already occupied we can just go to the next available
      car and if no ubers are available THEN  we can wait on the next car.by

WP-7: I believe that trylock will outperform my earlier implementation when the number of passengers
      is close to the number of ubers.  This is because the closer these two numbers are together,
      the less amount of time threads end up just looping around trying to grab the locks of ubers
      that are already occupied. In my better_init implementation, although it uses busy waiting when
      the number of free ubers is 0, it seems to be perform better.  For both of the implementations
      the percentage of the time the ubers are driving are very high (95-99%) but the better_init 
      version's program completes and exits much faster than the trylock under stressful conditions.


