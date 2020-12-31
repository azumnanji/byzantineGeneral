#include <cmsis_os2.h>
#include "general.h"

// add any #includes here
#include <stdlib.h>
#include <string.h>

// add any #defines here
#define MIN_QUEUE_SIZE 2 // min size of msg without any traitors
#define MAX_TRAITORS 2

// add global variables here
// gives q[m][i] where m is the recursion level and i is associated with the general id 
osMessageQueueId_t *q_id[MAX_TRAITORS + 1];
osSemaphoreId_t done;					// signal broadcast function to end
osSemaphoreId_t *barrier;			// semaphores used for barrier sync
uint8_t nGenerals;
uint8_t commander_id;
uint8_t reporter_id;
bool *traitors;								// traitors[i] is true if general i is a traitor
uint8_t n_traitors;
uint8_t msg_size;							// max size of the message
char buffer[200];							// buffer to print the required output

/** Record parameters and set up any OS and other resources
  * needed by your general() and broadcast() functions.
  * nGeneral: number of generals
  * loyal: array representing loyalty of corresponding generals
  * reporter: general that will generate output
  * return true if setup successful and n > 3*m, false otherwise
  */
bool setup(uint8_t nGeneral, bool loyal[], uint8_t reporter) {	
	nGenerals = nGeneral;
	traitors = (bool *) malloc(nGenerals * sizeof(bool));
	
	if (traitors == NULL) {
		printf("malloc failed for traitors bool array");
		return false;
	}
	
	reporter_id = reporter;
	n_traitors = 0;
	
	// set the buffer to empty for strcat later
	memset(buffer, '\0', sizeof(buffer));
	
	// determine traitors
	for (int i = 0; i < nGenerals; ++i) {
		if (!loyal[i])
			n_traitors++;
		traitors[i] = !loyal[i];
	}
	
	// determine if n > 3m for algorithm to work
	if (!c_assert(nGenerals > 3 * n_traitors)) {
		free(traitors);
		return false;
	}
	
	done = osSemaphoreNew(1, 0, NULL);

	barrier = (osSemaphoreId_t *) malloc(nGenerals * sizeof(osSemaphoreId_t));
	
	if (barrier == NULL) {
		printf("malloc failed for barrier sempahore array");
		return false;
	}
	
	// create the semaphores for barrier synchronization
	for (uint8_t i = 0; i < nGenerals; ++i) {
		barrier[i] = osSemaphoreNew(nGenerals - 1, 0, NULL);
	}
	
	// queue msg size equal to max possible chars in a msg
	msg_size = ((n_traitors + 1)*MIN_QUEUE_SIZE + 1) * sizeof(char);
	
	
	uint8_t n_queues = 1;
	// initialize all the queues in the q_id array dynamically
	for (int i = n_traitors, j = 0; i >= 0 && j <= n_traitors + 1; i--, j++) {
		q_id[i] = (osMessageQueueId_t *) malloc(n_queues * sizeof(osMessageQueueId_t));
		if (q_id[i] == NULL) {
			printf("malloc failed for q_id[%d] sempahore array", i);
			return false;
		}
		
		// for each recursion level initialize n_queues msg queues with a buffer of nGenerals - j msgs
		for (int k = 0; k < n_queues; k++) {
			q_id[i][k] = osMessageQueueNew(nGenerals - j, msg_size, NULL);
		} 
		if (i == n_traitors)
			j++;
		n_queues = n_queues * nGenerals;
	}

	return true;
}


/** Delete any OS resources created by setup() and free any memory
  * dynamically allocated by setup().
  */
void cleanup(void) {
	osSemaphoreDelete(done);
	for (int i = 0; i < nGenerals; ++i) {
		osSemaphoreDelete(barrier[i]);
	}
	free(traitors);
	free(barrier);
	
	uint8_t n_queues = 1;
	// delete all the queues used during the OM algorithm
	for (int i = n_traitors; i >= 0; i--) {
		for (int k = 0; k < n_queues; k++) {
			osMessageQueueDelete(q_id[i][k]);
		}
		n_queues = n_queues * nGenerals;
		free(q_id[i]);
	}
}


/** This function performs the initial broadcast to n-1 generals.
  * It should wait for the generals to finish before returning.
  * Note that the general sending the command does not participate
  * in the OM algorithm.
  * command: either 'A' or 'R'
  * sender: general sending the command to other n-1 generals
  */
void broadcast(char command, uint8_t commander) {
	// msg is command and q_id is commander
	// determine if commander is traitor or not
	// commander sends messages over queue 0
	char msg[msg_size];
	commander_id = commander;
	// append the commander id before the command 'id:A'
	msg[n_traitors*MIN_QUEUE_SIZE] = '0' + commander;
	msg[n_traitors*MIN_QUEUE_SIZE + 1] = ':';
	if (traitors[commander]) {
			for (int i = 0; i < nGenerals; ++i) {
				if (i % 2 == 0) 
					msg[msg_size - 1] = 'R';
				else
					msg[msg_size - 1] = 'A';
				// q_id[n_traitors][0] is the queue of the commander
				if(osMessageQueuePut(q_id[n_traitors][0], &msg , 0, 0) != osOK) {
					printf("Unable to put message in commander queue");
					return;
				}
			}
	}
	else{
		msg[msg_size - 1] = command;
		for (int i = 0; i < nGenerals; ++i) {
			if(osMessageQueuePut(q_id[n_traitors][0], &msg , 0, 0) != osOK) {
					printf("Unable to put message in commander queue");
					return;
				}
		}
	}
	// wait until all generals are finished processing
	osSemaphoreAcquire(done, osWaitForever);
}

/*
caller: id of general which sent the message
id: id of general receiving the message
m: iteration of recursion in OM algorithm
*/
void oral_message(uint8_t m, uint8_t id, uint8_t queue_index) {

	if (m == 0) {
		// add 2 to msg size to add ' ' character and '\0'
		char msg[msg_size + 2];
		if (id == reporter_id) {
			if (osMessageQueueGet(q_id[m][queue_index], &msg, NULL, osWaitForever) != osOK){
				printf("Could not get message for OM(%d), queue id: %d", m, id);
				return;
			}
			
			msg[msg_size] = ' ';
			msg[msg_size + 1] = '\0';
			// append the message to the buffer
			strcat(buffer, msg);
		}
	}
	else {
		char msg[msg_size];
		
		if (osMessageQueueGet(q_id[m][queue_index], &msg, NULL, osWaitForever) != osOK){
			printf("Could not get message for OM(%d), queue id: %d", m, id);
			return;
		}
		
		// check commander id for first recursion call since it calls this function 
		// when its general thread is created
		if (id != commander_id) {	
			// append ':id' at the correct location in the message
			msg[(m - 1)*MIN_QUEUE_SIZE] = '0' + id;
			msg[(m - 1)*MIN_QUEUE_SIZE + 1] = ':';
			
			
			if (traitors[id]) {
				msg[msg_size - 1] = id % 2 == 0 ? 'R':'A';
			}
			
			// determine number of generals to send the message to
			uint8_t generals = nGenerals - 2 - n_traitors + m;
			
			// determine the new queue index in the 2D queue array
			uint8_t new_q_index = nGenerals * queue_index + id;
			
			// send the message to the remaining generals
			for (int i = 0; i < generals; i++) {
					if (osMessageQueuePut(q_id[m - 1][new_q_index], &msg, 0, 0) != osOK) {
						printf("Failure: Unable to put message for OM(%d), queue id: %d", m, id);
						return;
					}
			}
			
			bool called[nGenerals];
			// initialize the called array to false
			for (uint8_t i = 0; i < nGenerals; i ++) {
				called[i] = false;
			}
			
			// determine which generals the message has come from
			for (uint8_t i = (m - 1); i < msg_size/2; i++) {
				uint8_t index = msg[i*2] - '0';
				called[index] = true;
			}
		
			// call OM algorithm on generals who received the message
			for (int i = 0; i < nGenerals; i++) {
				if (!called[i])
					oral_message(m - 1, i, new_q_index);
			}		
		}
		
	}
}

// used to sync all the general threads
void barrier_sync(uint8_t id){
	for (uint8_t i = 0; i < nGenerals - 1; i++) {
		osSemaphoreRelease(barrier[id]);
	}
	for (uint8_t i = 0; i < nGenerals; i++) {
		if (i != id)
			osSemaphoreAcquire(barrier[i], osWaitForever);
	}
}

/** Generals are created before each test and deleted after each
  * test.  The function should wait for a value from broadcast()
  * and then use the OM algorithm to solve the Byzantine General's
  * Problem.  The general designated as reporter in setup()
  * should output the messages received in OM(0).
  * idPtr: pointer to general's id number which is in [0,n-1]
  */
void general(void *idPtr) {
	uint8_t id = *(uint8_t *)idPtr; 
	
	// call oral message for OM(n_traitors) with commander queue id 0
	oral_message(n_traitors, id, 0);
	
	// semaphore barrier to sync all the general threads
	barrier_sync(id);
	
	// assume generals will always have id 0 
	if (id == 0) {
		puts(buffer);
		if(osSemaphoreRelease(done) != osOK){
			printf("Could not release done semamphore");
			return;
		}
	}
		
}
