#ifndef _THPOOL_H
#define _THPOOL_H

#include <pthread.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>

enum group_status {
	GROUP_UNHANDLED = 0,
	GROUP_HANDLED,
};

typedef struct work {
	void 			*(*work_routine)(void*);
	void 			*args;
	struct work 		*next_work;
} work_t;

typedef struct work_group {
	pthread_mutex_t		group_lock;
	struct work		*work_head;
	int 			work_num;
	struct work_group 	*next_group;	// next task group

	struct work_group 	*wake_group;	// Which group depend by me
	int			dep_group_num;	// How many groups I depend on
	enum group_status	group_status;
	bool 			(*cond)(void*);
} work_group_t;

typedef struct thread_pool {
	size_t		shutdown;
	size_t		maxnum_thread;
	pthread_t	*thread;		// a array of threads
	pthread_cond_t	queue_ready;		// condition varaible
	pthread_cond_t	loop_ready;		// condition varaible
	pthread_mutex_t	queue_lock;		// queue lock
	work_group_t	*work_group_head;

	pthread_mutex_t	clean_queue_lock;
	work_group_t	*clean_group_head;
	pthread_cond_t	clean_queue_ready;
	size_t		clean_shutdown;
} thread_pool_t;

typedef struct func_args {
	void *(*work_routine)(void *);
	void *args;
} func_args_t;

void *group_routine(void *);
void create_threadpool(thread_pool_t **, size_t);
void destroy_threadpool(thread_pool_t*);
work_group_t *add_works_to_group(int, func_args_t []);
void add_group_to_pool(thread_pool_t *, work_group_t *);
void add_groups_to_pool(thread_pool_t *, int, ...);
void remove_group_from_pool(thread_pool_t *, work_group_t *);
void add_dependents_to_group(work_group_t *, bool (*)(void *),int, ...);
void delete_dependents_of_group(thread_pool_t *, work_group_t *);

#endif