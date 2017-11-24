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
		printf("Sleep for %d ms\n", f_ms);
		/* produce the resource */
		tonks_sleep(f_ms*1000);
		/* end critical section */
		sem_post(sem_ptr);
		/* small sleep to reduce chance that semaphore is re-taken */
		usleep(5000);
		/* find the target soldier - which is the next non-dead one */
		for(i=index; i<index+Y; i++) {
			target_sol = i%Y;
			pthread_mutex_lock(mutex_lives_arr+target_sol);
			if(lives_arr[target_sol] > 0) {
				pthread_mutex_unlock(mutex_lives_arr+target_sol);
				break;
			}
			pthread_mutex_unlock(mutex_lives_arr+target_sol);
			if(i == index+Y-1)
				pthread_exit(NULL);
		}
		/* increment that resource */
		pthread_mutex_lock(mutex_res_arr+target_sol);
		res_arr[target_sol]++;
		pthread_mutex_unlock(mutex_res_arr+target_sol);
		printf("Farmer %d produced a resource for soldier %d\n",
				index, target_sol);
		printf("Soldier %d has %d lives\n",
				target_sol, lives_arr[target_sol]);
	}
	return(NULL);
}

void *sold (void *in_arg) {
	int index = ((S_arg *) in_arg)->index;
	int *res_arr = ((S_arg *) in_arg)->res_arr;
	pthread_mutex_t *mutex_res_arr = ((S_arg *) in_arg)->mutex_res_arr;
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
				pthread_mutex_unlock(mutex_eat_arr+index);
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
	int i, j, X, Y, pros_ind, target;
	pthread_mutexattr_t attr_mutex;
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

	/* not setting following attribute results in undefined behavior
	 * for mutexes that are in shared memory */
	pthread_mutexattr_init(&attr_mutex);
	pthread_mutexattr_setpshared(&attr_mutex, PTHREAD_PROCESS_SHARED);

	/* setup the shared memory for soldier counts */
	char *name_shm_sold = "/tmp/shm_sold";
	int fd_shm_sold = shm_open(name_shm_sold, O_CREAT | O_RDWR, 0666);
	ftruncate(fd_shm_sold, X*16);
	int *shm_sold = mmap(
			NULL, X*16, PROT_WRITE | PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, fd_shm_sold, 0);
	for(i=0; i<X; i++)
		shm_sold[i] = Y;

	/* setup the shared memory for mutex for soldier counts */
	char *name_mutex_shm_sold = "/tmp/mutex_shm_sold";
	int fd_mutex_shm_sold =
			shm_open(name_mutex_shm_sold, O_CREAT | O_RDWR, 0666);
	ftruncate(fd_mutex_shm_sold, 256);
	pthread_mutex_t *mutex_shm_sold = mmap(
			NULL, 256, PROT_WRITE | PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, fd_shm_sold, 0);
	pthread_mutex_init(mutex_shm_sold, &attr_mutex);

	/* setup the shared memory for attack data */
	char *name_shm_atk = "/tmp/shm_atk";
	int fd_shm_atk = shm_open(name_shm_atk, O_CREAT | O_RDWR, 0666);
	ftruncate(fd_shm_atk, 3*X*16);
	int (*shm_atk)[3] = mmap(
			NULL, 3*X*16, PROT_WRITE | PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, fd_shm_atk, 0);
	/* i: child sending attack */
	/* j=0: attack points; j=1: attack target; j=2: received damage */
	for(i=0; i<X; i++)
		for(j=0; j<3; j++)
			shm_atk[i][j] = 0;

	/* setup the shared memory for mutex for attack data */
	char *name_mutex_shm_atk = "/tmp/mutex_shm_atk";
	int fd_mutex_shm_atk =
			shm_open(name_mutex_shm_atk, O_CREAT | O_RDWR, 0666);
	ftruncate(fd_mutex_shm_atk, 256);
	pthread_mutex_t *mutex_shm_atk = mmap(
			NULL, 256, PROT_WRITE | PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, fd_shm_atk, 0);
	pthread_mutex_init(mutex_shm_atk, &attr_mutex);

	/* destroy the mutex attribute object */
	pthread_mutexattr_destroy(&attr_mutex);

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
		int game_on = 1;
		int draw = 1;
		printf("Parent finished spawning\n");
		printf("#### 1 ####\n");
		while(game_on) {
			usleep(100000);
			/* check if child has won */
			game_on = 0;
			for(i=0; i<X; i++) {
				if(shm_atk[i][1] == -1) {
					draw = 0;
					break;
				} else if(shm_sold[i] > 0) {
					game_on = 1;
					break;
				}
			}
			printf("#### 2 ####\n");
			if(!game_on)
				break;
			printf("#### 3 ####\n");
			usleep(800000);
			pthread_mutex_lock(mutex_shm_sold);
			pthread_mutex_lock(mutex_shm_atk);
			printf("#### 4 ####\n");
			for(i=0; i<X; i++) {
				printf("Child %d has %d soldiers\n", i, shm_sold[i]);
				printf("Child %d tries to attack %d with %d points\n",
						i, shm_atk[i][2], shm_atk[i][1]);
			}
			/* calculate the damages */
			for(i=0; i<X; i++) {
				/* check if there is attack to process */
				if(shm_atk[i][1] > 0) {
					target = shm_atk[i][2];
					if(shm_atk[target][2] == i) {
						printf("Processes attack each other");
						/* children attack each other */
						if(shm_atk[i][1] > shm_atk[target][1]) {
							shm_atk[target][3] +=
									shm_atk[i][1] - shm_atk[target][1];
							/* reset attack points */
							shm_atk[target][1] = 0;
						} else {
							/* points equal or opponent higher */
							shm_atk[i][3] +=
									shm_atk[target][1] - shm_atk[i][1];
							/* reset attack points */
							shm_atk[target][1] = 0;
						}
					} else {
						shm_atk[target][3] += shm_atk[i][1];
					}
					/* reset attack points */
					shm_atk[i][1] = 0;
				}
			}
			printf("#### 5 ####\n");
			pthread_mutex_unlock(mutex_shm_sold);
			pthread_mutex_unlock(mutex_shm_atk);
			printf("#### 6 ####\n");
			usleep(100000);
		}
		printf("#### 7 ####\n");
		if(draw) {
			printf("Game ended in a draw\n");
		} else {
			printf("Child %d with PID %d has won\n", i, children[i]);
		}

		/* small delay to prevent orphan processes */
		usleep(100000);
		/* unlink file descriptors */
		unlink(name_shm_sold);
		unlink(name_mutex_shm_sold);
		unlink(name_shm_atk);
		unlink(name_mutex_shm_atk);
		/* destroy mutexes */
		pthread_mutex_destroy(mutex_shm_sold);
		pthread_mutex_destroy(mutex_shm_atk);
	} else { /* child code */
		/* seed must be different for each process */
		srand(time(NULL) + pros_ind);
		printf("Child with PID %d and index %d created\n",
						getpid(), pros_ind);
		int target, target_sold, my_sold, last_child, in_damage, offset;
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
			/* choose the victim */
			/* read from shared memory using mutex */
			pthread_mutex_lock(mutex_shm_sold);
			offset = rand() % X;
			last_child = 1;
			for(i=0; i<X; i++) {
				target = (i + offset) % X;
				/* find out number of remaining soldiers */
				target_sold = shm_sold[target];
				/* no suicide or mutilation permitted */
				if((target != pros_ind) && (target_sold > 0)) {
					last_child = 0;
					break;
				}
			}
			pthread_mutex_unlock(mutex_shm_sold);
			if(last_child) {
				printf("Winner detected\n");
				/* send "attack" of -1 to tell parent that it's won */
				pthread_mutex_lock(mutex_shm_atk);
				shm_atk[pros_ind][1] = -1;
				pthread_mutex_unlock(mutex_shm_atk);
				break;
			}
			/* send the attack */
			pthread_mutex_lock(mutex_shm_atk);
			shm_atk[pros_ind][1] += atk_pts;
			shm_atk[pros_ind][2] = target;
			pthread_mutex_unlock(mutex_shm_atk);
			printf("Child %d sends attack of %d to %d\n",
					pros_ind, atk_pts, target);

			/* wait for parent to process the attacks */
			usleep(200000);

			/* receive the attack */
			pthread_mutex_lock(mutex_shm_atk);
			in_damage = shm_atk[pros_ind][3];
			shm_atk[pros_ind][3] = 0;
			pthread_mutex_unlock(mutex_shm_atk);
			printf("Child %d receives %d damage\n", pros_ind, in_damage);
			/* assign damage sequentially */
			for(i=0; (i<Y) && (0<in_damage); i++) {
				pthread_mutex_lock(mutex_lives_arr+i);
				/* damage the soldier if possible */
				while((0<lives_arr[i]) && (0<in_damage)) {
					lives_arr[i]--;
					in_damage--;
				}
				pthread_mutex_unlock(mutex_lives_arr+i);
			}

			/* update the soldier count in shared array */
			my_sold = 0;
			for(i=0; i<Y; i++) {
				pthread_mutex_lock(mutex_lives_arr+i);
				/* count the lives */
				if(lives_arr[i] > 0) {
					my_sold++;
					pthread_mutex_unlock(mutex_lives_arr+i);
				}
				pthread_mutex_unlock(mutex_lives_arr+i);
			}
			pthread_mutex_lock(mutex_shm_sold);
			shm_sold[pros_ind] = my_sold;
			pthread_mutex_unlock(mutex_shm_sold);
			/* end process if all soldiers dead or this is last child */
			if(!(0<my_sold)) {
				break;
			} else {
				printf("Checking other children\n");
				last_child = 1;
				/* check number of alive children */
				pthread_mutex_lock(mutex_shm_sold);
				for(i=0; i<X; i++) {
					/* find out number of remaining soldiers */
					if((i != pros_ind) && (0 < shm_sold[i])) {
						last_child = 0;
						break;
					}
				}
				pthread_mutex_unlock(mutex_shm_sold);
				if(last_child) {
					printf("Winner detected\n");
					/* send "attack" of -1 to tell parent that it's won */
					pthread_mutex_lock(mutex_shm_atk);
					shm_atk[pros_ind][1] = -1;
					pthread_mutex_unlock(mutex_shm_atk);
					break;
				}
			}
		}
		printf("Child %d is ending\n", pros_ind);
		/* wait for threads to complete */
		for(i=0; i<Y; i++) {
			pthread_cancel(farm_t[i]);
			pthread_cancel(sold_t[i]);
		}
		for(i=0; i<Y; i++) {
			pthread_join(farm_t[i], NULL);
			pthread_join(sold_t[i], NULL);
		}
		/* free everything */
		sem_destroy(&sem);
		for(i=0; i<Y; i++) {
			pthread_mutex_destroy(mutex_res_arr+i);
			pthread_mutex_destroy(mutex_lives_arr+i);
			pthread_mutex_destroy(mutex_eat_arr+i);
		}
		for(i=0; i<Y; i++) {
			free(f_arg_arr[i]);
			free(s_arg_arr[i]);
		}


	}
	return(0);
}
