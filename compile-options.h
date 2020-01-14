#include <filesystem>
#include <map>
#include <string>
#include <functional>

struct CompileOptions {
	std::string input_file_name;
	std::string output_file_name;
	std::function<std::string(
		const std::filesystem::path& input_path,
		const std::filesystem::path& output_path
	)> get_compilation_command;
	std::function<std::string(const std::filesystem::path& output_path)> get_execution_command;
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
