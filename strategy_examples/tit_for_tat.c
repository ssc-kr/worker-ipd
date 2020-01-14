int input();
void output(int x);

void __main__() {
	output(1);
	for (;;) {
		const int value = input();
		if (!(value == 0 || value == 1)) return;
		output(value);
	}
}

#include <sys/ipc.h>
#include <sys/shm.h>
#define __WAIT__(cond) { do { __asm__ __volatile__(""); } while(!(cond)); }

struct SharedData {
	int input_remain;
	int input_value;
	int output_remain;
	int output_value;
}* addr;

int input() {
	__WAIT__(addr->input_remain);
	addr->input_remain = 0;
	return addr->input_value;
}

void output(const int value) {
	addr->output_value = value;
	addr->output_remain = 1;
}

int main(int argc, char* argv[]) {
	const int shmid = atoi(argv[1]);
	addr = shmat(shmid, (void*)0, 0);
	__main__();
	shmdt(addr);
}
