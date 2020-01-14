#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <map>
#include <bitset>
#include <string>
#include <chrono>
#include <random>
#include <functional>
#include "sandboxed-process.h"
#include "compile-options.h"

template<class Choice>
struct Result {
	/*
	using Choices = std::conditional<
		std::is_same<Choice, bool>::value,
		// TODO: Replace vector<bool> with dynamic_bitset
		std::vector<bool>,
		std::conditional<
			std::is_same<Choice, char>::value,
			std::string,
			std::vector<Choice>
		>
	>;
	*/

	using Choices = std::vector<Choice>;

	Choices first_choices;
	Choices second_choices;
	int first_score;
	int second_score;
};

template<class Choice>
struct Strategy {
	using StrategyKey = std::string;

	std::string name;
	std::map<StrategyKey, Result<Choice>> results;
	double score;
};

std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());

template<class Choice>
class Judge {
	const int compile_message_size = 4096;
	const int time_limit = 1;
	const int memory_limit = 128;
	const int __iter_min = 1;
	const int __iter_max = (1 << 24);
	const Choice __end_of_iter = -1;

	const std::filesystem::path strategies_directory;
	const std::filesystem::path sandbox_directory;

public:
	Judge(
		const std::filesystem::path& _strategies_directory = "strategies",
		const std::filesystem::path& _sandbox_directory = "sandbox"
	):
		strategies_directory(std::filesystem::absolute(_strategies_directory)),
		sandbox_directory(std::filesystem::absolute(_sandbox_directory))
	{
		std::filesystem::create_directories(strategies_directory);
		std::filesystem::create_directories(sandbox_directory);
	}

	void write_strategy(
		const std::string& strategy_name,
		const std::string& file_name,
		const std::string& content
	) const {
		const auto strategy_directory = strategies_directory / strategy_name;
		std::filesystem::create_directory(strategy_directory);

		const auto strategy_path = strategy_directory / file_name;
		std::ofstream strategy_file(strategy_path);
		strategy_file << content;
		if (strategy_file.fail()) {
			throw std::runtime_error("Failed to write to strategy file: " + strategy_path.string());
		}
	}

	std::optional<std::string> compile(
		const std::string& strategy_name,
		const CompileOptions& options
	) const {
		const auto strategy_directory = strategies_directory / strategy_name;
		const auto input_path = strategy_directory / options.input_file_name;
		const auto output_path = strategy_directory / options.output_file_name;

		std::filesystem::remove(output_path);

		// TODO: Find a better way to catch compilation error.
		const auto compilation_command = options.get_compilation_command(input_path, output_path) + " 2>&1";
		const auto compile_fp = popen(compilation_command.data(), "r");
		if (!compile_fp) {
			throw std::runtime_error("Failed to popen!");
		}

		char compile_message[compile_message_size];
		const auto read_bytes = fread(compile_message, sizeof(*compile_message), sizeof(compile_message) - 1, compile_fp);
		compile_message[read_bytes] = 0;
		pclose(compile_fp);

		if (!std::filesystem::exists(output_path)) {
			return std::nullopt;
		}
		return options.get_execution_command(output_path);
	}

	auto compile(
		const std::string& strategy_name,
		const std::string& content,
		const CompileOptions& options
	) const {
		write_strategy(strategy_name, options.input_file_name, content);
		return compile(strategy_name, options);
	}

	std::optional<Result<Choice>> compare(
		const std::string& first_command,
		const std::string& second_command,
		const std::pair<int, int> iter_range = {200, 500}
	) const {
		const auto[iter_min, iter_max] = iter_range;
		if (!(iter_min <= iter_max && __iter_min <= iter_min && iter_max <= __iter_max)) {
			throw std::range_error("Invalid range!");
		}

		const auto is_invalid = [](const Choice& choice) {
			return !(choice == 0 || choice == 1);
		};

		const auto calculate = [](
			Result<Choice>& result,
			const int iter,
			const Choice& first_choice,
			const Choice& second_choice
		) {
			constexpr int score_table[2][2][2] = {
				{{0, 0}, {3, -1}},
				{{-1, 3}, {2, 2}}
			};

			result.first_choices[iter] = first_choice;
			result.second_choices[iter] = second_choice;
			result.first_score += score_table[first_choice][second_choice][0];
			result.second_score += score_table[first_choice][second_choice][1];
		};

		std::uniform_int_distribution<> random_iter_count(iter_min, iter_max);
		const auto iter_limit = random_iter_count(rng);

		SandboxedProcess first_process(first_command);
		SandboxedProcess second_process(second_command);
		Result<Choice> result = {};

		result.first_choices.resize(iter_limit);
		result.second_choices.resize(iter_limit);

		for (int iter = 0; iter < iter_limit; iter++) {
			const Choice first_choice = first_process.recv_int();
			if (is_invalid(first_choice)) {
				return std::nullopt;
			}

			const Choice second_choice = second_process.recv_int();
			if (is_invalid(second_choice)) {
				return std::nullopt;
			}

			if (iter < iter_limit - 1) {
				first_process.send_int(second_choice);
				second_process.send_int(first_choice);
			}
			else {
				first_process.send_int(__end_of_iter);
				second_process.send_int(__end_of_iter);
			}

			calculate(result, iter, first_choice, second_choice);
		}

		first_process.close();
		second_process.close();

		return result;
	}

	void benchmark_compare(
		const std::string& strategy_name,
		const std::string& lang,
		const std::string& content,
		const int compare_count
	) const {
		const int period = compare_count / 10;
		const auto& options = compile_options.at(lang);
		if (const auto command = compile(strategy_name, content, options)) {
			std::cout << "DEBUG: Start benchmark_compare";
			std::cout << '(' << strategy_name << ", " << compare_count << ')' << std::endl;

			const auto time_start = std::chrono::steady_clock::now();

			for (int count = 1; count <= compare_count; count++) {
				if (!compare(*command, *command)) {
					throw std::runtime_error("Comparison error!");
				}
				if (count % period == 0) {
					std::cout << "DEBUG: " << strategy_name << " - " << count << std::endl;
				}
			}

			const auto time_end = std::chrono::steady_clock::now();
			const std::chrono::duration<double> diff = time_end - time_start;
			std::cout << "Time to compare " << compare_count << " times: " << diff.count() << 's' << std::endl;
		}
		else {
			throw std::runtime_error("Compilation error!");
		}
	}
};

int main(const int argc, const char* argv[]) {
	std::ios_base::sync_with_stdio(false);

	try {
		std::ifstream tit_for_tat_file("strategy_examples/tit_for_tat.c");
		std::string tit_for_tat(
			(std::istreambuf_iterator<char>(tit_for_tat_file)),
			(std::istreambuf_iterator<char>())
		);

		Judge<int> judge;
		judge.benchmark_compare("tit_for_tat", "c", tit_for_tat, 500);
	} catch(const std::runtime_error& err) {
		std::cout << "Runtime error: " << err.what() << std::endl;
	}
}
