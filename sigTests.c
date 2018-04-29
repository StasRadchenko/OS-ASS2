#include "types.h"
#include "param.h"
#include "user.h"
#include "stat.h"




void count_proc(){
	int i;
	volatile int count = 0;
	for (i=0; i<10000000; i++){
		if (i%3==0)
			count+=3;
		else if (i%3==1)
			count+=2;
		else
			count++;
	}
}

void first_proc_print(int n){
	printf(1, "This is FIRST_PROC_PRINT with n = %d\n", n);
}

void second_proc_print(int n){
	printf(1, "This is SECOND_PROC_PRINT with n = %d\n", n);
}

void pre_test(sighandler_t old_handler){
	printf(2, "---------------PROC_PRINT_ADDRESSES start-------------\n");
	printf(2, "FIRST_PROC_PRINT address: %p\n", first_proc_print);
	printf(2, "FIRST_PROC_PRINT address: %p\n", second_proc_print);
	printf(2, "---------------PROC_PRINT_ADDRESSES end---------------\n\n");

	printf(2, "---------------HANDLERS_CHECK start------------------\n");
	if ((sighandler_t)SIG_DFL != old_handler ){
		printf(2,"	old_handler handler is: %d\n	should be SIG_DFL\n", (int)old_handler);
	}
	old_handler = signal(0, first_proc_print);
	if (first_proc_print == old_handler){		
		printf(2, "Handler Successfully matched!\n");
	}
	else{
		printf(2, "old_handler is: %p\n	should be: %p\n", old_handler, first_proc_print);
	}

	printf(2, "Check if old_handler uses first_proc_print:\n");
	old_handler(10);
	old_handler = signal(0, second_proc_print);
	if (old_handler != first_proc_print){
		printf(2, "old_handler and first_proc_print should be the same\n");
	}
	printf(2, "Checking if the old handler receives second_proc_print:\n");
	old_handler = signal(0, first_proc_print);
	old_handler(1);
	printf(2, "---------------HANDLERS_CHECK end------------------\n\n");
}

void looping_sigprocmask_test(int parent){
	int i;
	for (i=0; i < 32; i++)
		signal(i, second_proc_print);

	sigprocmask(0);

	for (i=0; i < 32; i++)
		kill(parent, i);
}


int main(int argc, char **argv){
	int child, parent;
	//kind of pre testing data
	sighandler_t old_handler = signal(0, first_proc_print);
	pre_test(old_handler);
	//-------------------------
	if ((child = fork()) == 0){
		count_proc();
		printf(2, "I should not print here\n");
		exit();
	}else{
		kill(child, SIG_KILL);
		wait();
	}

	parent = getpid();
//////////////////////one if down/////////////
	if ((child = fork()) == 0){
		sleep(5);
		kill(parent, SIG_KILL);
		count_proc();
		kill(parent, SIG_KILL);
		exit();
	} else {
		signal(SIG_KILL, (sighandler_t)SIG_IGN);
		sleep(10);
		kill(child, SIGSTOP);
		printf(2, "Parent should stay alive as we used SIG_IGN to ignore SIG_KILL\n");
		signal(SIG_KILL, first_proc_print);
		kill(child, SIGCONT);
		count_proc();
		wait();
		printf(2, "Parent prints this after activating first_proc_print\n");
		signal(SIG_KILL, (sighandler_t)SIG_DFL);
	}
	//////////////////////////////////////
	if ((child = fork()) == 0){
		kill(parent, SIGSTOP);
		sleep(10);
		printf(2, "Parent receives SIGCONT\n");
		kill(parent, SIGCONT);
		exit();
	} else {
		count_proc();
		printf(2, "parent prints this note after the child\n");
		wait();
	}

	looping_sigprocmask_test(parent);

	exit();
}
