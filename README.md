# Overview #

## Introduction ##

This is thread pool with task dependencies and sequences support. I didn't find one pool with those features supported, so I created my one.

### Features ###

a) Multi-thread of course.

b) Task dependencies and sequences, let's take main.c as an example:

```
   group1 is made up with work 0,1,2,3
   group2 is made up with work 4,5,6,7
   group3 is made up with work 8,9,10,11
   group4 is made up with work 12,13,14,15
   group5 is made up with work 16,17,18,19
```
   works within a group are independent from each other, so it doesn't matter if work 0 starts or finishes before work 1. However group 2 is depend on group 1, by add_dependents_to_group(group2, cond, 1, group1). So group 2 must be started after group 1 finished when cond returns true. It is the same as:

```
   run_group1;
   if (cond)
	run_group2;
```
   A specific use case is:
```
   group1 (
	resolve_symbol(a);
	resolve_symbol(b);
   )
   if (symbol_exist(a) && symbol_exist(b)) {
	group2 (
		read_symbol_value(a);
		read_symbol_value(b);
	)
   }
```
   If group 2 depend by group 1, and group 5 depend by group 2,3,4. group 1,3,4 will be appended to execute queue. After the cond_false of group 1, group 2 will never be run, and group 5 will never be run. So group 2,3,4, the dependencies of group 5, don't need to run. However group 3,4 had already been appended to execute queue. So group 3,4 may or may not be executed, and group 1 must be execute, and group 2,5 will never be executed.

```
   $ ./a.out 
   fun: running the work of 11  ---> group3
   fun: running the work of 0       ---> group1
   fun: running the work of 3       ---> group1
   fun: running the work of 14    ---> group4
   fun: running the work of 9   ---> group3
   fun: running the work of 15    ---> group4
   fun: running the work of 10  ---> group3
   fun: running the work of 8   ---> group3
   fun: running the work of 12    ---> group4
   fun: running the work of 2       ---> group1
   fun: running the work of 1       ---> group1
   fun: running the work of 13    ---> group4
```
   Note: The performance of the thread pool is not fully tested.