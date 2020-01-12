#include <iostream>
#include <stdexcept>
#include <fstream>
#include <filesystem>
#include <map>
#include <string>
#include <functional>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#define DONTOPT __asm__ __volatile__("")

struct CompileOptions {
	std::string input_file_name;
	std::string output_file_name;
	std::function<std::string(
		const std::filesystem::path& input_path,
		const std::filesystem::path& output_path
	)> get_compilation_command;
	std::function<std::string(const std::filesystem::path& output_path)> get_execution_command;
};

struct Process {
	int pid;
	int shmid;
	void *addr;
	bool close() {
		kill(pid, SIGKILL);
		shmdt(addr);
		// shmctl(shmid, IPC_RMID, 0);
		return false;
	}
};

struct Result {
	// TODO:
	int first_score, second_score;
};

// TODO: replace std::function with C++20 std::format
const std::map<std::string, CompileOptions> compile_options = {
	{
		"c",
		{
			"a.c",
			"a.out",
			[](
				const std::filesystem::path& input_path,
				const std::filesystem::path& output_path
			) {
				return (
					"clang -std=gnu11 -Wall"
					" -O2 -static -s -lm -DONLINE_JUDGE"
					" -o " + output_path.string() +
					" " + input_path.string()
				);
			},
			[](const std::filesystem::path& output_path) {
				return output_path.string();
			}
		}
	},
	{
		"c++",
		{
			"a.cpp",
			"a.out",
			[](
				const std::filesystem::path& input_path,
				const std::filesystem::path& output_path
			) {
				return (
					"clang++ -std=gnu++2a -Wall"
					" -O2 -static -s -lm -DONLINE_JUDGE"
					" -o " + output_path.string() +
					" " + input_path.string()
				);
			},
			[](const std::filesystem::path& output_path) {
				return output_path.string();
			}
		}
	},
	{
		"python",
		{
			"a.py",
			"a.pyc",
			[](
				const std::filesystem::path& input_path,
				const std::filesystem::path& output_path
			) {
				return (
					"python3 -c \""
					"import py_compile;"
					"py_compile.compile("
					"r'" + input_path.string() + "'" +
					", optimize=2"
					", cfile=" + output_path.string() + 
					")\""
				);
			},
			[](const std::filesystem::path& output_path) {
				return "python3 " + output_path.string();
			}
		}
	},
	{
		"pypy",
		{
			"a.py",
			"a.pyc",
			[](
				const std::filesystem::path& input_path,
				const std::filesystem::path& output_path
			) {
				return (
					"python3 -c \""
					"import py_compile;"
					"py_compile.compile("
					"r'" + input_path.string() + "'" +
					", optimize=2"
					", cfile=" + output_path.string() + 
					")\""
				);
			},
			[](const std::filesystem::path& output_path) {
				return "pypy3 " + output_path.string();
			}
		}
	},
	{
		"java",
		{
			"a.java",
			"a",
			[](
				const std::filesystem::path& input_path,
				const std::filesystem::path& output_path
			) {
				return "javac -J-Xms1024m -J-Xmx1024m -J-Xss512m -encoding UTF-8 " + input_path.string();
			},
			[](const std::filesystem::path& output_path) {
				return "java -Xms1024m -Xmx1024m -Xss512m -Dfile.encoding=UTF-8 " + output_path.string();
			}
		}
	}
};

constexpr int compile_message_size = 4096;
constexpr int time_limit = 1;
constexpr int memory_limit = 128;
const std::filesystem::path source_directory = std::filesystem::absolute("source");
const std::filesystem::path sandbox_directory = std::filesystem::absolute("sandbox");
const std::string sandbox_command = (
	"nsjail -Mo" // or --really_quiet
	" --user 12345 --group 12345 --hostname playground"
	// " --stderr_to_null --disable_proc --quiet"
	" --seccomp_string 'ALLOW { write { fd == 1 } } DEFAULT ALLOW'"
	" --max_cpus 1"
	" --time_limit " + std::to_string(time_limit) +
	" --rlimit_as " + std::to_string(memory_limit) +
	" --bindmount_ro " + source_directory.string() +
	" --chroot " + sandbox_directory.string() +
	" -- "
);

void create(
	const std::string& strategy_name,
	const std::string& file_name,
	const std::string& content
) {
	const auto path = source_directory / strategy_name;
	if(!std::filesystem::exists(path)) {
		if(!std::filesystem::create_directories(path)) {
			throw std::runtime_error("Failed to create directories!");
		}
	}
	std::ofstream(path / file_name) << content;
}

std::optional<std::string> compile(
	const std::string& strategy_name,
	const CompileOptions& options
) {
	const auto path = source_directory / strategy_name;
	const auto input_path = path / options.input_file_name;
	const auto output_path = path / options.output_file_name;

	std::filesystem::remove(output_path);

	const auto compilation_command = options.get_compilation_command(input_path, output_path) + " 2>&1";
	const auto compile_fp = popen(compilation_command.data(), "r");
	if(!compile_fp) {
		throw std::runtime_error("Failed to popen!");
	}

	char compile_message[compile_message_size];
	const auto read_bytes = fread(compile_message, sizeof(*compile_message), sizeof(compile_message) - 1, compile_fp);
	compile_message[read_bytes] = 0;
	pclose(compile_fp);

	if(!std::filesystem::exists(output_path)) {
		return std::nullopt;
	}
	return options.get_execution_command(output_path);
}

Process execute(const std::string& command) {
	const auto execution_command = command;

	constexpr size_t mem_size = 64;
	static int counter = 0;
	const key_t key = 998244353 + (counter++ % 16);
	int shmid = shmget(key, mem_size, IPC_CREAT | 0666);
	if(shmid == -1) {
		throw std::runtime_error("Failed to shmget!");
	}

	const auto addr = shmat(shmid, nullptr, 0);
	if(addr == (void*)-1) {
		throw std::runtime_error("Failed to shmat!");
	}
	std::memset(addr, 0, mem_size);

	const auto pid = fork();
	if(pid > 0) {
		return { pid, shmid, addr };
	}
	else if(pid == 0) {
		execl(execution_command.data(), execution_command.data(), std::to_string(shmid).data(), nullptr);
		exit(-1);
	}
	else {
		throw std::runtime_error("Failed to fork!");
	}
}

std::optional<Result> compare(
	const std::string& first_command,
	const std::string& second_command
) {
	auto first_process = execute(first_command);
	auto second_process = execute(second_command);

	int first_score = 0;
	int second_score = 0;
	constexpr int score_table[2][2][2] = {
		{
			{0, 0},
			{3, -1}
		},
		{
			{-1, 3},
			{2, 2}
		}
	};

	const auto receive_int = [&](const Process& process) {
		int& input_remain = *((int*)process.addr + 2);
		const int& input_value = *((int*)process.addr + 3);

		while(!input_remain) DONTOPT;
		input_remain = 0;
		return input_value;
	};

	const auto send_int = [&](
		const Process& process,
		const int value
	) {
		int& output_remain = *((int*)process.addr + 0);
		int& output_value = *((int*)process.addr + 1);
		output_value = value;
		output_remain = 1;
	};

	// write(first_process.in, "400\n", 4);
	// write(second_process.in, "400\n", 4);
	for(int iter = 0; iter < 400; iter++) {
		int first_choice = receive_int(first_process);
		int second_choice = receive_int(second_process);

		if(iter < 400 - 1) {
			send_int(first_process, second_choice);
			send_int(second_process, first_choice);
		}

		const auto check = [](const int& choice) {
			return choice < 0 || choice > 1;
		};

		if(check(first_choice) || check(second_choice)) {
			first_score = second_score = 0;
			break;
		}

		first_score += score_table[first_choice][second_choice][0];
		second_score += score_table[first_choice][second_choice][1];
	}

	first_process.close();
	second_process.close();

	return {{ first_score, second_score }};
}

bool edit(
	const std::string& strategy_name,
	const std::string& lang,
	const std::string& content
) {
	// TODO: validate strategy_name and lang

	const auto& options = compile_options.at(lang);
	create(strategy_name, options.input_file_name, content);
	if(const auto command = compile(strategy_name, options)) {
		for(int count = 0; count < 500; count++) {
			if(count % 50 == 0) std::cout << count << std::endl;
			if(!compare(*command, *command)) {
				std::cout << "Comparison error!" << std::endl;
				exit(-1);
			}
		}
		return false;

		/*
		if(compare(*command, *command)) {
			// success
			return false;
		}
		else {
			// comparison error
			std::cout << "Comparison error!" << std::endl;
			return true;
		}
		*/
	}
	else {
		// compilation error
		std::cout << "Compilation error!" << std::endl;
		return true;
	}
}

int main() {
	std::ios_base::sync_with_stdio(false);

	const std::string tit_for_tat = R"tft(
		int input();
		void output(int x);
		void __main__(int N) {
			output(1);
			while(--N) output(input());
		}

		#include <stdio.h>
		#include <sys/ipc.h>
		#include <sys/shm.h>
		#define DONTOPT __asm__ __volatile__("")
		void *addr;

		int input() {
			int* input_remain = (int*)addr + 0;
			const int* input_value = (int*)addr + 1;
			while(!*input_remain) DONTOPT;
			*input_remain = 0;
			return *input_value;
		}

		void output(const int value) {
			int* output_remain = (int*)addr + 2;
			int* output_value = (int*)addr + 3;

			*output_value = value;
			*output_remain = 1;
		}

		int main(int argc, char* argv[]) {
			const int shmid = atoi(argv[1]);
			// const int key = 998244353;
			// const int shmid = shmget(key, 64, 0);
			// printf("! %d\n", shmid);
			addr = shmat(shmid, (void*)0, 0);
			__main__(400);
			shmdt(addr);
		})tft";

	if(!std::filesystem::exists(sandbox_directory)) {
		if(!std::filesystem::create_directories(sandbox_directory)) {
			throw std::runtime_error("Failed to create directories!");
		}
	}

	edit("tit_for_tat", "c", tit_for_tat);
	std::cout << "Good!" << std::endl;

	/*
	if(!edit("c_hello", "c", "main(){puts(\"Hello\");}")) {
		std::cout << "OK hello" << std::endl;
	}

	if(edit("c_error", "c", "error")) {
		std::cout << "OK error" << std::endl;
	}

	if(edit("c_timelimit", "c", "main(){while(1);}")) {
		std::cout << "OK" << std::endl;
	}
	*/
}
