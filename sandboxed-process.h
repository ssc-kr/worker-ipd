#include <stdexcept>
#include <string>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#define __WAIT__(cond) { do { __asm__ __volatile__(""); } while(!(cond)); }

constexpr int __initial_key = 998244353;
constexpr int __counter_limit = 16;

struct SharedData {
	int output_remain;
	int output_value;
	int input_remain;
	int input_value;
};

class SandboxedProcess {
public:
	int pid;
	int shmid;
	SharedData* addr;

	SandboxedProcess(const std::string& command) {
		const auto execution_command = command;

		static int counter = 0;
		const key_t key = __initial_key + counter++;
		if (counter >= __counter_limit) {
			counter -= __counter_limit;
		}

		shmid = shmget(key, sizeof(SharedData), IPC_CREAT | 0666);
		if (shmid == -1) {
			throw std::runtime_error("Failed to shmget!");
		}

		addr = (SharedData*)shmat(shmid, nullptr, 0);
		if (addr == (void*)-1) {
			throw std::runtime_error("Failed to shmat!");
		}
		memset(addr, 0, sizeof(SharedData));

		pid = fork();
		if (pid == 0) {
			execl(execution_command.data(), execution_command.data(), std::to_string(shmid).data(), nullptr);
			exit(-1); // execl failed
		}
		else if (pid < 0) {
			throw std::runtime_error("Failed to fork!");
		}
	}

	bool close() {
		// int status;
		// waitpid(pid, &status, 0);
		kill(pid, SIGKILL);
		shmdt(addr);
		shmctl(shmid, IPC_RMID, 0);
		return false;
	}

	int recv_int() {
		__WAIT__(addr->input_remain);
		addr->input_remain = 0;
		return addr->input_value;
	}

	void send_int(const int value) {
		addr->output_value = value;
		addr->output_remain = 1;
	}
};

/*
const std::string sandbox_command = (
	"nsjail -Mo" // or --really_quiet
	" --user 12345 --group 12345 --hostname playground"
	// " --stderr_to_null --disable_proc --quiet"
	" --seccomp_string 'ALLOW { write { fd == 1 } } DEFAULT ALLOW'"
	" --max_cpus 1"
	" --time_limit " + std::to_string(time_limit) +
	" --rlimit_as " + std::to_string(memory_limit) +
	" --bindmount_ro " + strategies_directory.string() +
	" --chroot " + sandbox_directory.string() +
	" -- "
);
*/
