/*
 * war.c
 *
 *  Created on: 23 Nov 2017
 *      Author: adtonks
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <semaphore.h>

/* use a struct to pass arguments; dispenses with global variables */
typedef struct f_arg {
	int index;
	int *res_arr;
	int *res_to_sol_arr;
	sem_t *sem_ptr;
	pthread_mutex_t *mutex_ptr;
} F_arg;

typedef struct s_arg {
	int index;
	int *res_arr;
	int *res_to_sol_arr;
	pthread_mutex_t *mutex_ptr;
} S_arg;

void tonks_sleep(int f_mus) {
	/* sleep function for fractional seconds */
	int secs = 0;
	if(f_mus<1)
		return;
	while(f_mus>1000000) {
		secs++;
		f_mus -= 1000000;
	}
	sleep(secs);
	usleep(f_mus);
	return;
}

void *farm (void *in_arg) {
	int index = ((F_arg *) in_arg)->index;
	int *res_arr = ((F_arg *) in_arg)->res_arr;
	int *res_to_sol_arr = ((F_arg *) in_arg)->res_to_sol_arr;
	sem_t *sem_ptr = ((F_arg *) in_arg)->sem_ptr;
	pthread_mutex_t *mutex_ptr = ((F_arg *) in_arg)->mutex_ptr;

	int f_ms, f_mus;
	while(1) {
		sem_wait(sem_ptr);
		/* begin critical section */
		/* the farmer has been woken up, to farm for f_ms milliseconds */
		srand(time(NULL));
		f_ms = 400 + (rand()%1800) + 2000;
		/* produce the resource */
		tonks_sleep(f_ms*1000);
		/* end critical section */
		sem_post(sem_ptr);
		/* small sleep to reduce chance that semaphore is re-taken */
		usleep(5000);
		/* update the resource array */
		pthread_mutex_lock(mutex_ptr+index);
		res_arr[index]++;
		pthread_mutex_unlock(mutex_ptr+index);
		printf("Thread %d has produced %d resources total for soldier %d\n",
				index, res_arr[index], res_to_sol_arr[index]);
	}
	return(NULL);
}

/* IMPORTANT: this function was originally written for scheduler.c */
int check_no(const char *input) {
	int i;
	int len = strlen(input);
	for(i=0; i<len; i++) {
		if(((int) input[i] < 48) || (57 < (int) input[i]))
			return(0);
	}
	return(1);
}

int main(int argc, char const *argv[]) {
	int i, X, Y;
	if(argc != 5) {
		printf("Command line arguments incorrect\n");
		return(1);
	}
	for(i=1; i<5; i+=2) {
		if(strcmp(argv[i], "-children") == 0) {
			if(!check_no(argv[i+1])) {
				printf("Command line arguments incorrect\n");
				return(1);
			}
			X = atoi(argv[i+1]);
		} else if(strcmp(argv[i], "-fighters") == 0) {
			if(!check_no(argv[i+1])) {
				printf("Command line arguments incorrect\n");
				return(1);
			}
			Y = atoi(argv[i+1]);
		} else {
			printf("Command line arguments incorrect\n");
			return(1);
		}
	}
	if(X<2) {
		printf("Must have minimum 2 children\n");
		return(1);
	} else if(Y<1) {
		printf("Must have minimum 1 fighter per child\n");
		return(1);
	}
	printf("%d children and %d fighters\n", X, Y);
	int children[X];
	for(i=0; i<X; i++)
		children[X] = 0;
	for(i=0; i<X; i++) {
		children[i] = fork();
		if(children[i] == 0)
			break;
	}
	/* parent code */
	if(children[X-1] != 0) {
		printf("Parent finished spawning\n");
	} else { /* child code */
		int res_arr[Y];
		for(i=0; i<Y; i++)
			res_arr[i] = 0;
		int res_to_sol_arr[Y];
		for(i=0; i<Y; i++)
			res_to_sol_arr[i] = i;
		sem_t sem;
		sem_init(&sem, 0, 6);
		pthread_mutex_t mutex[Y];
		for(i=0; i<Y; i++)
			pthread_mutex_init(mutex+i, NULL);
		printf("Child with PID %d created\n", getpid());
		pthread_t farm_t[Y];
		F_arg *f_arg_arr[Y];
		pthread_t sold_t[Y];
		S_arg *s_arg_arr[Y];
		/* initialize the farmer thread arguments */
		for(i=0; i<Y; i++) {
			f_arg_arr[i] = malloc(sizeof(F_arg));
			f_arg_arr[i]->index = i;
			f_arg_arr[i]->res_arr = res_arr;
			f_arg_arr[i]->res_to_sol_arr = res_to_sol_arr;
			f_arg_arr[i]->sem_ptr = &sem;
			f_arg_arr[i]->mutex_ptr = mutex;
		}
		/* initialize the soldier thread arguments */
		for(i=0; i<Y; i++) {
			s_arg_arr[i] = malloc(sizeof(S_arg));
			s_arg_arr[i]->index = i;
			s_arg_arr[i]->res_arr = res_arr;
			s_arg_arr[i]->res_to_sol_arr = res_to_sol_arr;
			s_arg_arr[i]->mutex_ptr = mutex;
		}
		/* create the farmer threads */
		for(i=0; i<Y; i++) {
			assert(pthread_create(farm_t + i, NULL, farm, (void *) f_arg_arr[i]) == 0);
		}
		/* create the soldier threads */
		/*
		for(i=0; i<Y; i++) {
			assert(pthread_create(sold_t + i, NULL, sold, (void *) s_arg_arr[i]) == 0);
		}
		*/
		/* wait for threads to complete */
		for(i=0; i<Y; i++) {
			pthread_join(farm_t[i], NULL);
		}

		/* free everything */
		sem_destroy(&sem);

	}

	return(0);
}
