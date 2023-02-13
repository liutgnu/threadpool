#include "thpool.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#define DEFAULT_ARRAY_LEN 16
 
typedef struct pp {
	thread_pool_t *pool;
	int index;
} pp;

void add_group_to_pool_cleanup(thread_pool_t *, work_group_t *);

void *group_routine(void *args)
{
	pp *p = (pp *)args;

	thread_pool_t *pool = (thread_pool_t *)p->pool;
	work_t *work_tmp;
	work_group_t *group, *group_tmp;
 
	while (1) {
		pthread_mutex_lock(&pool->queue_lock);
		while (!pool->work_group_head && !pool->shutdown) {
			pthread_cond_wait(&pool->queue_ready, &pool->queue_lock);
		}

		if (!pool->work_group_head && pool->shutdown) {
			pthread_mutex_unlock(&pool->queue_lock);
			pthread_exit(NULL);
		}

		// /**** debug ****/
		// printf("%d ", p->index);
		// group = pool->work_group_head;
		// do {
		// 	printf("%p ", group);
		// 	group = group->next_group;
		// } while (group != pool->work_group_head);
		// printf("\n");
		// /**** debug ****/

		group = pool->work_group_head;
		do {
			if (!pthread_mutex_trylock(&group->group_lock)) {
				pool->work_group_head = group->next_group;
				pthread_mutex_unlock(&pool->queue_lock);
				goto success;
			}
			group = group->next_group;
		} while (group != pool->work_group_head);

		pthread_mutex_unlock(&pool->queue_lock);
		continue;

success:
		// /**** debug ****/
		// int i;
		// for (work_tmp = group->work_head, i = 0;
		//      work_tmp;
		//      work_tmp = work_tmp->next_work, i++);
		// assert(group->work_num == i);
		// /**** debug ****/

		if (group->dep_group_num > 0) {
			pthread_mutex_unlock(&group->group_lock);
			continue;
		} else if (!group->dep_group_num &&
			   group->group_status == GROUP_UNHANDLED &&
			   group->cond &&
			   !group->cond(NULL)) {
			delete_dependents_of_group(pool, group);
			pthread_mutex_unlock(&group->group_lock);
			continue;
		}
		// Now the workable groups
		if (group->work_num == 1) {
			// Executing the work
			group->work_head->work_routine(group->work_head->args);
			if (group->wake_group) {
				pthread_mutex_lock(&group->wake_group->group_lock);
				group->wake_group->dep_group_num--;
				if (!group->wake_group->dep_group_num) {
					group->wake_group->group_status = GROUP_HANDLED;
				}
				pthread_mutex_unlock(&group->wake_group->group_lock);
			}
			remove_group_from_pool(pool, group);
			pthread_mutex_unlock(&group->group_lock);
			free(group->work_head);
			free(group);
		} else if (group->work_num > 1 && group->group_status != GROUP_HANDLED) {
			// We are groups with multiple works, so for each work we create a group, so the later one 
			group->dep_group_num += group->work_num;
			for (work_tmp = group->work_head;
			     work_tmp; 
			     work_tmp = work_tmp->next_work) {
				func_args_t f[] = {{work_tmp->work_routine, work_tmp->args}};
				group_tmp = add_works_to_group(1, f);
				group_tmp->wake_group = group;
				add_group_to_pool(pool, group_tmp);
			}
			pthread_mutex_unlock(&group->group_lock);
			// 让出cpu
		} else if (group->work_num > 1 && group->group_status == GROUP_HANDLED) {
			if (group->wake_group) {
				pthread_mutex_lock(&group->wake_group->group_lock);
				group->wake_group->dep_group_num--;
				pthread_mutex_unlock(&group->wake_group->group_lock);
			}
			remove_group_from_pool(pool, group);
			add_group_to_pool_cleanup(pool, group);
			pthread_mutex_unlock(&group->group_lock);
		} else {
			pthread_mutex_unlock(&group->group_lock);
		}
	}
	return NULL;
}
 
void create_threadpool(thread_pool_t **pool, size_t max_thread_num)
{
	*pool = (thread_pool_t *)malloc(sizeof(thread_pool_t));
	assert((*pool) != NULL);
	memset(*pool, 0, sizeof(thread_pool_t));

	(*pool)->maxnum_thread = max_thread_num;
	(*pool)->thread = (pthread_t *)malloc(sizeof(pthread_t) * max_thread_num);
	assert((*pool)->thread != NULL);
	assert(pthread_mutex_init(&((*pool)->queue_lock), NULL) == 0);
	assert(pthread_mutex_init(&((*pool)->clean_queue_lock), NULL) == 0);
	assert(pthread_cond_init(&((*pool)->queue_ready), NULL) == 0);
 
	for (int i = 0; i < max_thread_num; i++) {
		pp *p = malloc(sizeof(pp));
		p->pool = *pool;
		p->index = i;
		assert(pthread_create((*pool)->thread + i, NULL, group_routine, p) == 0);
	}
}

void destroy_threadpool(thread_pool_t* pool)
{
	work_group_t *group_tmp, *group_save;
	int i = 0;

	if (pool->shutdown) {
		return;
	}
 
	pthread_mutex_lock(&pool->queue_lock);
	pool->shutdown = 1;
	pthread_cond_broadcast(&pool->queue_ready);
	pthread_mutex_unlock(&pool->queue_lock);
 
	for(int i = 0; i < pool->maxnum_thread; i++){
		pthread_join(pool->thread[i], NULL);
	}

	pthread_mutex_lock(&pool->clean_queue_lock);
	if (pool->clean_group_head) {
		group_tmp = pool->clean_group_head;
		do {
			group_save = group_tmp;
			group_tmp = group_tmp->next_group;
			free(group_save->work_head);
			free(group_save);
		} while (group_tmp != pool->clean_group_head);
	}
	pthread_mutex_unlock(&pool->clean_queue_lock);

	free(pool->thread);
 
	pthread_mutex_destroy(&pool->queue_lock);
	pthread_mutex_destroy(&pool->clean_queue_lock);
	pthread_cond_destroy(&pool->queue_ready);
	free(pool);
}

work_group_t *add_works_to_group(int num, func_args_t funcs[])
{
	work_group_t *group = malloc(sizeof(work_group_t));
	work_t *works = malloc(sizeof(work_t) * num);
	assert(group != NULL);
	assert(works != NULL);
	memset(group, 0, sizeof(work_group_t));
	memset(works, 0, sizeof(work_t) * num);

	for (int i = 0; i < num; i++) {
		works[i].work_routine = funcs[i].work_routine;
		works[i].args = funcs[i].args;
		if (i > 0)
			works[i - 1].next_work = &works[i];
	}

	group->work_head = works;
	group->work_num = num;
	assert(pthread_mutex_init(&group->group_lock, NULL) == 0);
	return group;
}

void _add_group_to_queue(work_group_t **queue_head, work_group_t *group)
{
	work_group_t *group_tmp;

	if (!(group_tmp = *queue_head)) {
		*queue_head = group;
	} else {
		while (group_tmp->next_group != *queue_head) {
			group_tmp = group_tmp->next_group;
		}
		group_tmp->next_group = group;
	}
	group->next_group = *queue_head;
}


void add_group_to_queue(work_group_t **queue_head, pthread_mutex_t *lock, work_group_t *group)
{
	pthread_mutex_lock(lock);
	_add_group_to_queue(queue_head, group);
	pthread_mutex_unlock(lock);
}

void add_group_to_pool(thread_pool_t *pool, work_group_t *group)
{
	if (!group)
		return;

	add_group_to_queue(&pool->work_group_head, &pool->queue_lock, group);
	pthread_cond_signal(&pool->queue_ready);	
}

void add_groups_to_pool(thread_pool_t *pool, int num, ...)
{
	int i;
	va_list ap;
	work_group_t *group_tmp;

	pthread_mutex_lock(&pool->queue_lock);
	va_start(ap, num);
	for (i = 0; i < num; i++) {
		group_tmp = va_arg(ap, work_group_t *);
		_add_group_to_queue(&pool->work_group_head, group_tmp);
	}
	va_end(ap);
	pthread_mutex_unlock(&pool->queue_lock);
	pthread_cond_signal(&pool->queue_ready);	
}

void add_group_to_pool_cleanup(thread_pool_t *pool, work_group_t *group)
{
	if (!group)
		return;

	add_group_to_queue(&pool->clean_group_head, &pool->clean_queue_lock, group);	
}

void _remove_group_from_pool(thread_pool_t *pool, work_group_t *group)
{
	work_group_t *group_tmp;

	// printf("IN ");
	// group_tmp = pool->work_group_head;
	// do {
	// 	printf("%p|%d ", group_tmp, group_tmp->work_num);
	// 	group_tmp = group_tmp->next_group;
	// } while (group_tmp != pool->work_group_head);	
	// printf("\n");

	if (pool->work_group_head == group) {
		if (group->next_group == group) {
			pool->work_group_head = NULL;
		} else {
			for (group_tmp = pool->work_group_head;
			     group_tmp->next_group != pool->work_group_head;
			     group_tmp = group_tmp->next_group);
			group_tmp->next_group = group->next_group;
			pool->work_group_head = group->next_group;
		}
	} else {
		group_tmp = pool->work_group_head;
		do {
			if (group_tmp->next_group == group) {
				group_tmp->next_group = group->next_group;
				break;
			}
			group_tmp = group_tmp->next_group;
		} while (group_tmp != pool->work_group_head);
	}

	// printf("OUT ");
	// if (pool->work_group_head) {
	// 	group_tmp = pool->work_group_head;
	// 	do {
	// 		printf("%p|%d ", group_tmp, group_tmp->work_num);
	// 		group_tmp = group_tmp->next_group;
	// 	} while (group_tmp != pool->work_group_head);
	// 	printf("\n");
	// }else {
	// 	printf("(nil)\n");
	// }	
}

void remove_group_from_pool(thread_pool_t *pool, work_group_t *group)
{
	work_group_t *group_tmp;

	if (!group)
		return;

	pthread_mutex_lock(&pool->queue_lock);
	if (pool->work_group_head == NULL) {
		pthread_mutex_unlock(&pool->queue_lock);
		return;
	}
	_remove_group_from_pool(pool, group);
	pthread_mutex_unlock(&pool->queue_lock);
}

void add_dependents_to_group(work_group_t *group_base, bool (*cond)(void*), int num, ...)
{
	int i;
	va_list ap;
	work_group_t *group_tmp;

	va_start(ap, num);
	for (i = 0; i < num; i++) {
		group_tmp = va_arg(ap, work_group_t *);
		group_tmp->wake_group = group_base;
	}
	va_end(ap);
	group_base->dep_group_num += num;
	group_base->cond = cond;
}

void delete_dependents_of_group(thread_pool_t *pool, work_group_t *group)
{
	work_group_t *group_iter, *group_dep, **group_array, **group_tmp;
	int cap = DEFAULT_ARRAY_LEN;
	int i = 0, j = 0;

	if (!group)
		return;

	group_array = malloc(sizeof(work_group_t *) * DEFAULT_ARRAY_LEN);
	assert(pool->work_group_head != NULL);
	assert(group_array != NULL);

	pthread_mutex_lock(&pool->queue_lock);
	group_dep = group->wake_group;
loop:
	group_iter = pool->work_group_head;
	do {
		if (group_iter->wake_group == group_dep) {
			group_array[i++] = group_iter;
			if (i > (cap >> 1)) {
				group_tmp = malloc(sizeof(work_group_t *) * (cap << 1));
				assert(group_tmp != NULL);
				memcpy(group_tmp, group_array, sizeof(work_group_t *) * cap);
				free(group_array);
				group_array = group_tmp;
				cap <<= 1;
			}
		}
		group_iter = group_iter->next_group;
	} while (group_iter != pool->work_group_head);

	if (group_dep) {
		if (!group_dep->wake_group) {
			group_array[i++] = group_dep;
		} else {
			group_dep = group_dep->wake_group;
			goto loop;
		}
	}

	for (j = 0; j < i; j++) {
		_remove_group_from_pool(pool, group_array[j]);
		// There might be works running when group removed from pool. So we need
		// to wait and clean up the group after the works been done. So we append
		// the groups to a clean up queue.
		add_group_to_pool_cleanup(pool, group_array[j]);
	}
	pthread_mutex_unlock(&pool->queue_lock);
	free(group_array);
}