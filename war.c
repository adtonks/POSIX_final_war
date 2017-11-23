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
#include <sys/mman.h>
#include <fcntl.h>

/* use a struct to pass arguments; dispenses with global variables */
typedef struct f_arg {
	int Y;
	int index;
	sem_t *sem_ptr;
	int *res_arr;
	pthread_mutex_t *mutex_res_arr;
	int *lives_arr;
	pthread_mutex_t *mutex_lives_arr;
} F_arg;

typedef struct s_arg {
	int index;
	int *res_arr;
	pthread_mutex_t *mutex_res_arr;
	int *lives_arr;
	pthread_mutex_t *mutex_lives_arr;
	int *eat_arr;
	pthread_mutex_t *mutex_eat_arr;
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
	int Y = ((F_arg *) in_arg)->Y;
	int index = ((F_arg *) in_arg)->index;
	int *res_arr = ((F_arg *) in_arg)->res_arr;
	pthread_mutex_t *mutex_res_arr = ((F_arg *) in_arg)->mutex_res_arr;
	int *lives_arr = ((F_arg *) in_arg)->lives_arr;
	pthread_mutex_t *mutex_lives_arr = ((F_arg *) in_arg)->mutex_lives_arr;
	sem_t *sem_ptr = ((F_arg *) in_arg)->sem_ptr;
	int target_sol, i;

	int f_ms, f_mus;
	while(1) {
		sem_wait(sem_ptr);
		/* begin critical section */
		/* the farmer has been woken up, to farm for f_ms milliseconds */
		f_ms = 400 + (rand()%1800);
		/* produce the resource */
		tonks_sleep(f_ms*1000);
		/* end critical section */
		sem_post(sem_ptr);
		/* small sleep to reduce chance that semaphore is re-taken */
		usleep(5000);
		/* update the resource array */
		pthread_mutex_lock(mutex_res_arr+index);
		res_arr[index]++;
		pthread_mutex_unlock(mutex_res_arr+index);
		/* find the target soldier - which is the next non-dead one */
		for(i=index; i<index+Y; i++) {
			target_sol = i%Y;
			pthread_mutex_lock(mutex_lives_arr+target_sol);
			if(lives_arr[target_sol] > 0)
				break;
			pthread_mutex_unlock(mutex_lives_arr+target_sol);
		}
		/* increment that resource */
		printf("Thread %d has produced %d resources total for soldier %d\n",
				index, res_arr[index], target_sol);
	}
	return(NULL);
}

void *sold (void *in_arg) {
	int index = ((S_arg *) in_arg)->index;
	int *res_arr = ((S_arg *) in_arg)->res_arr;
	pthread_mutex_t *mutex_res_arr = ((S_arg *) in_arg)->mutex_res_arr;
	int *lives_arr = ((S_arg *) in_arg)->lives_arr;
	pthread_mutex_t *mutex_lives_arr = ((F_arg *) in_arg)->mutex_lives_arr;
	int *eat_arr = ((S_arg *) in_arg)->eat_arr;
	pthread_mutex_t *mutex_eat_arr = ((S_arg *) in_arg)->mutex_eat_arr;
	/* loop through the sleep-eat-attack loop */
	while(1) {
		/* block until eating time (soldier is "sleeping" */
		while(1) {
			/* reading int may not be atomic if memory unaligned */
			pthread_mutex_lock(mutex_eat_arr+index);
			if(eat_arr[index] == 1) {
				eat_arr[index] = 0;
				break;
			}
			pthread_mutex_unlock(mutex_eat_arr+index);
			/* small sleep to prevent mutex re-take if write needed */
			usleep(5000);
		}
		/* try to eat the resource */
		pthread_mutex_lock(mutex_res_arr+index);
		if(res_arr[index] > 0) {
			res_arr[index]--;
			/* write attack to eat array */
			eat_arr[index] = -1;
			printf("Soldier %d attacks, has %d resources left\n",
					index, res_arr[index]);
		}
		pthread_mutex_unlock(mutex_res_arr+index);
		/* unlock the eat array after attack result written */
		pthread_mutex_unlock(mutex_eat_arr+index);
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
	int i, X, Y, pros_ind;
	pthread_mutexattr_t attr_mutex;
	char *name = "/tmp/war_mem";
	int fd_shm_sold = shm_open(name, O_CREAT | O_RDWR, 0666);
	int *shm_sold;
	int fd_mutex_shm_sold = shm_open(name, O_CREAT | O_RDWR, 0666);
	pthread_mutex_t *mutex_shm_sold;
	srand(time(NULL));
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
	/* setup the shared memory for soldier counts */
	ftruncate(fd_shm_sold, X*16);
	shm_sold = mmap(NULL, X*16, PROT_WRITE | PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, fd_shm_sold, 0);
	for(i=0; i<X; i++)
		shm_sold[i] = Y;
	/* setup the shared memory for mutex for soldier counts */
	ftruncate(fd_mutex_shm_sold, 256);
	mutex_shm_sold = mmap(NULL, 256, PROT_WRITE | PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, fd_shm_sold, 0);
	/* not setting following attribute results in undefined behavior */
	pthread_mutexattr_init(&attr_mutex);
	pthread_mutexattr_setpshared(&attr_mutex, PTHREAD_PROCESS_SHARED);
	pthread_mutexattr_destroy(&attr_mutex);
	pthread_mutex_init(mutex_shm_sold, &attr_mutex);
	/* initialize array to hold PID of children */
	int children[X];
	for(i=0; i<X; i++)
		children[i] = 0;
	for(pros_ind=0; pros_ind<X; pros_ind++) {
		children[pros_ind] = fork();
		if(children[pros_ind] == 0)
			break;
	}
	/* parent code */
	if(children[X-1] != 0) {
		printf("Parent finished spawning\n");
		sleep(5);
	} else { /* child code */
		printf("Child with PID %d and index %d created\n",
						getpid(), pros_ind);
		int target, target_sold;
		int atk_pts = 0;
		/* semaphore for 6 farmers */
		sem_t sem;
		sem_init(&sem, 0, 6);
		/* resource array */
		int res_arr[Y];
		for(i=0; i<Y; i++)
			res_arr[i] = 0;
		pthread_mutex_t mutex_res_arr[Y];
		for(i=0; i<Y; i++)
			pthread_mutex_init(mutex_res_arr+i, NULL);
		/* soldier lives array */
		int lives_arr[Y];
		for(i=0; i<Y; i++)
			lives_arr[i] = 3;
		pthread_mutex_t mutex_lives_arr[Y];
		for(i=0; i<Y; i++)
			pthread_mutex_init(mutex_lives_arr+i, NULL);
		/* soldier eating time array */
		int eat_arr[Y];
		for(i=0; i<Y; i++)
			eat_arr[i] = 0;
		pthread_mutex_t mutex_eat_arr[Y];
		for(i=0; i<Y; i++)
			pthread_mutex_init(mutex_eat_arr+i, NULL);
		/* prepare farmer threads */
		pthread_t farm_t[Y];
		F_arg *f_arg_arr[Y];
		for(i=0; i<Y; i++) {
			f_arg_arr[i] = malloc(sizeof(F_arg));
			f_arg_arr[i]->Y = Y;
			f_arg_arr[i]->index = i;
			f_arg_arr[i]->sem_ptr = &sem;
			f_arg_arr[i]->res_arr = res_arr;
			f_arg_arr[i]->mutex_res_arr = mutex_res_arr;
			f_arg_arr[i]->lives_arr = lives_arr;
			f_arg_arr[i]->mutex_lives_arr = mutex_lives_arr;
		}
		/* prepare soldier threads */
		pthread_t sold_t[Y];
		S_arg *s_arg_arr[Y];
		for(i=0; i<Y; i++) {
			s_arg_arr[i] = malloc(sizeof(S_arg));
			s_arg_arr[i]->index = i;
			s_arg_arr[i]->res_arr = res_arr;
			s_arg_arr[i]->mutex_res_arr = mutex_res_arr;
			s_arg_arr[i]->lives_arr = lives_arr;
			s_arg_arr[i]->mutex_lives_arr = mutex_lives_arr;
			s_arg_arr[i]->eat_arr = eat_arr;
			s_arg_arr[i]->mutex_eat_arr = mutex_eat_arr;
		}
		/* create the farmer threads */
		for(i=0; i<Y; i++) {
			assert(pthread_create(farm_t + i, NULL, farm, (void *) f_arg_arr[i]) == 0);
		}
		/* create the soldier threads */
		for(i=0; i<Y; i++) {
			assert(pthread_create(sold_t + i, NULL, sold, (void *) s_arg_arr[i]) == 0);
		}
		while(1) {
			usleep(700000);
			/* wake the soldiers */
			for(i=0; i<Y; i++) {
				pthread_mutex_lock(mutex_eat_arr+i);
				eat_arr[i] = 1;
				pthread_mutex_unlock(mutex_eat_arr+i);
			}
			/* count the points after waiting for attack results */
			usleep(100000);
			atk_pts = 0;
			for(i=0; i<Y; i++) {
				pthread_mutex_lock(mutex_eat_arr+i);
				if(eat_arr[i] == -1) {
					eat_arr[i] = 0;
					atk_pts++;
				}
				pthread_mutex_unlock(mutex_eat_arr+i);
			}
			/* send the attack */
			while(1) {
				target = rand() % X;
				/* read from shared memory using mutex */
				pthread_mutex_lock(mutex_shm_sold);
				target_sold = shm_sold[target];
				pthread_mutex_unlock(mutex_shm_sold);
				if((target != pros_ind) && (target_sold > 0))
					break;
			}
			printf("%d soldiers in process %d attacked %d\n",
					atk_pts, getpid(), target);
			/* wait for parent to process the attacks */
			usleep(200000);
			/* receive the attack */

			/* check that at least one soldier still alive */
		}

		/* wait for threads to complete */
		for(i=0; i<Y; i++) {
			pthread_join(farm_t[i], NULL);
		}

		/* free everything */
		sem_destroy(&sem);
		for(i=0; i<Y; i++) {
			pthread_mutex_destroy(mutex_eat_arr+i);
			pthread_mutex_destroy(mutex_res_arr+i);
		}

	}
	shm_unlink(name);
	return(0);
}
