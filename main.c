#include "thpool.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>
 
void *fun(void *args)
{
	int work = (int)args;
	printf("fun: running the work of %d\n", work);
	return NULL;
}

bool cond_true(void *args)
{
	return true;
}

bool cond_false(void *args)
{
	return false;
}

int main(int argc, char* args[])
{
	thread_pool_t* pool = NULL;
	work_group_t *group1, *group2, *group3, *group4, *group5;
	create_threadpool(&pool, 20);
	int i = 0;

	func_args_t funcs_a[4] = {{fun, (void *)(i+0)}, {fun, (void *)(i+1)}, {fun, (void *)(i+2)}, {fun, (void *)(i+3)}};
	func_args_t funcs_b[4] = {{fun, (void *)(i+4)}, {fun, (void *)(i+5)}, {fun, (void *)(i+6)}, {fun, (void *)(i+7)}};
	func_args_t funcs_c[4] = {{fun, (void *)(i+8)}, {fun, (void *)(i+9)}, {fun, (void *)(i+10)}, {({void *late(void *){sleep(2);printf("late2 func...\n");}late;}), NULL}};
	func_args_t funcs_d[4] = {{fun, (void *)(i+12)}, {fun, (void *)(i+13)}, {fun, (void *)(i+14)}, {({void *late(void *){sleep(1);printf("late1 func...\n");}late;}), NULL}};
	func_args_t funcs_e[4] = {{fun, (void *)(i+16)}, {fun, (void *)(i+17)}, {fun, (void *)(i+18)}, {fun, (void *)(i+19)}};

	group1 = add_works_to_group(4, funcs_a);
	group2 = add_works_to_group(4, funcs_b);
	group3 = add_works_to_group(4, funcs_c);
	group4 = add_works_to_group(4, funcs_d);
	group5 = add_works_to_group(4, funcs_e);

	add_dependents_to_group(group2, cond_true, 1, group1);
	add_dependents_to_group(group5, cond_true, 3, group2, group3, group4);

	add_groups_to_pool(pool, 5, group1, group2, group3, group4, group5);

	destroy_threadpool(pool);
	return 0;
}