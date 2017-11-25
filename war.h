/*
 * war.h
 *
 *  Created on: 25 Nov 2017
 *      Author: adtonks
 */

#ifndef WAR_H_
#define WAR_H_

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
	pthread_cond_t *cond_eat_arr;
} S_arg;

void tonks_sleep(int f_mus);
void *farm (void *in_arg);
void *sold (void *in_arg);
/* IMPORTANT: this function was originally written for scheduler.c */
int check_no(const char *input);

#endif /* WAR_H_ */
