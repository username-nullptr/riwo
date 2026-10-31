// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

namespace
{

using byte = uint8_t;
using input = std::vector<byte>;

struct options
{
	std::filesystem::path corpus;
	std::filesystem::path artifact;
	size_t runs {0};
	size_t max_length {0};
	uint64_t seed {0};
};

[[noreturn]] void usage(const char *program)
{
	throw std::invalid_argument(
		"usage: " + std::string(program) +
		" --corpus=DIR --runs=N --seed=N --max-len=N [--artifact=FILE]"
	);
}

[[nodiscard]] bool option_value(std::string_view argument,
	std::string_view name, std::string_view &value)
{
	if(argument.size() <= name.size() or
		argument.substr(0, name.size()) != name or
		argument[name.size()] != '=')
	{
		return false;
	}
	value = argument.substr(name.size() + 1);
	return true;
}

template <typename Integer>
[[nodiscard]] bool parse_integer(std::string_view text, Integer &value)
{
	if(text.empty())
		return false;

	const auto result = std::from_chars(
		text.data(), text.data() + text.size(), value);
	return result.ec == std::errc {} and
		result.ptr == text.data() + text.size();
}

[[nodiscard]] options parse_options(int argc, char **argv)
{
	options result;
	bool has_corpus = false;
	bool has_runs = false;
	bool has_max_length = false;

	for(int index = 1; index < argc; ++index)
	{
		const std::string_view argument(argv[index]);
		std::string_view value;
		if(argument == "--help" or argument == "-h")
		{
			std::cerr <<
				"usage: " << argv[0] <<
				" --corpus=DIR --runs=N --seed=N --max-len=N"
				" [--artifact=FILE]\n";
			std::exit(0);
		}
		if(option_value(argument, "--corpus", value))
		{
			if(value.empty())
				usage(argv[0]);
			result.corpus = value;
			has_corpus = true;
			continue;
		}
		if(option_value(argument, "--artifact", value))
		{
			if(value.empty())
				usage(argv[0]);
			result.artifact = value;
			continue;
		}
		if(option_value(argument, "--runs", value))
		{
			if(not parse_integer(value, result.runs) or result.runs == 0)
				usage(argv[0]);
			has_runs = true;
			continue;
		}
		if(option_value(argument, "--seed", value))
		{
			if(not parse_integer(value, result.seed))
				usage(argv[0]);
			continue;
		}
		if(option_value(argument, "--max-len", value))
		{
			if(not parse_integer(value, result.max_length) or
				result.max_length == 0)
			{
				usage(argv[0]);
			}
			has_max_length = true;
			continue;
		}
		usage(argv[0]);
	}

	if(not has_corpus or not has_runs or not has_max_length)
		usage(argv[0]);
	return result;
}

[[nodiscard]] input read_input(const std::filesystem::path &path,
	size_t max_length)
{
	const auto file_size = std::filesystem::file_size(path);
	const auto size = std::min(
		file_size, static_cast<uintmax_t>(max_length));
	input result(static_cast<size_t>(size));
	if(result.empty())
		return result;

	std::ifstream stream(path, std::ios::binary);
	if(not stream)
		throw std::runtime_error("cannot open corpus input: " + path.string());

	stream.read(reinterpret_cast<char*>(result.data()),
		static_cast<std::streamsize>(result.size()));
	if(not stream and not stream.eof())
		throw std::runtime_error("cannot read corpus input: " + path.string());
	result.resize(static_cast<size_t>(stream.gcount()));
	return result;
}

[[nodiscard]] std::vector<std::filesystem::path> corpus_paths(
	const std::filesystem::path &corpus)
{
	if(not std::filesystem::is_directory(corpus))
		throw std::runtime_error("corpus is not a directory: " + corpus.string());

	std::vector<std::filesystem::path> result;
	for(const auto &entry :
		std::filesystem::recursive_directory_iterator(corpus))
	{
		if(entry.is_regular_file())
			result.push_back(entry.path());
	}
	std::sort(result.begin(), result.end());
	if(result.empty())
		throw std::runtime_error("corpus is empty: " + corpus.string());
	return result;
}

[[nodiscard]] uint64_t hash_text(std::string_view text)
{
	uint64_t result = UINT64_C(14695981039346656037);
	for(const char character : text)
	{
		result ^= static_cast<uint64_t>(
			static_cast<unsigned char>(character));
		result *= UINT64_C(1099511628211);
	}
	return result;
}

class random_generator
{
public:
	explicit random_generator(uint64_t seed) noexcept
		: state_(seed)
	{
	}

	[[nodiscard]] uint64_t next() noexcept
	{
		state_ += UINT64_C(0x9e3779b97f4a7c15);
		uint64_t value = state_;
		value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
		value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
		return value ^ (value >> 31U);
	}

	[[nodiscard]] size_t index(size_t upper_bound) noexcept
	{
		return static_cast<size_t>(next() % upper_bound);
	}

	[[nodiscard]] byte value() noexcept
	{
		return static_cast<byte>(next() & UINT64_C(0xff));
	}

private:
	uint64_t state_;
};

[[nodiscard]] input mutate(const input &base, size_t max_length,
	random_generator &random)
{
	input result = base;
	const size_t mutation_count = random.index(4) + 1;
	for(size_t index = 0; index < mutation_count; ++index)
	{
		switch(random.index(7))
		{
		case 0:
			if(not result.empty())
			{
				const size_t offset = random.index(result.size());
				result[offset] ^= static_cast<byte>(
					1U << random.index(std::numeric_limits<byte>::digits));
			}
			break;
		case 1:
			if(not result.empty())
				result[random.index(result.size())] = random.value();
			break;
		case 2:
			if(result.size() < max_length)
			{
				const auto offset = static_cast<std::ptrdiff_t>(
					random.index(result.size() + 1));
				result.insert(result.begin() + offset, random.value());
			}
			break;
		case 3:
			if(not result.empty())
			{
				const auto offset = static_cast<std::ptrdiff_t>(
					random.index(result.size()));
				result.erase(result.begin() + offset);
			}
			break;
		case 4:
			result.resize(random.index(result.size() + 1));
			break;
		case 5:
			if(not result.empty())
			{
				const size_t begin = random.index(result.size());
				const size_t count = std::min(
					random.index(result.size() - begin) + 1,
					max_length);
				std::fill_n(result.begin() + static_cast<std::ptrdiff_t>(begin),
					static_cast<std::ptrdiff_t>(count), random.value());
			}
			break;
		default:
			if(result.size() < max_length)
				result.push_back(random.value());
			break;
		}
	}
	return result;
}

void write_artifact(const std::filesystem::path &path, const input &value)
{
	if(path.empty())
		return;

	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if(not stream)
		throw std::runtime_error(
			"cannot create fuzz artifact: " + path.string());
	if(not value.empty())
	{
		stream.write(reinterpret_cast<const char*>(value.data()),
			static_cast<std::streamsize>(value.size()));
	}
	if(not stream)
		throw std::runtime_error(
			"cannot write fuzz artifact: " + path.string());
}

void run_case(const input &value, std::string_view label,
	const std::filesystem::path &artifact)
{
	write_artifact(artifact, value);
	std::cerr << "GNU fuzz regression: " << label << '\n';
	if(LLVMFuzzerTestOneInput(value.data(), value.size()) != 0)
		throw std::runtime_error("fuzz harness rejected input: " +
			std::string(label));
}

[[nodiscard]] std::vector<input> edge_inputs(size_t max_length)
{
	std::vector<input> result {
		{},
		{0x00},
		{0x00, 0x00},
		{0x00, 0x00, 0x00},
		{0xff},
		{0xff, 0xff, 0xff},
	};

	const size_t patterned_size = std::min<size_t>(max_length, 64);
	result.emplace_back(patterned_size, 0x00);
	result.emplace_back(patterned_size, 0xff);
	result.emplace_back(patterned_size, static_cast<byte>('A'));
	return result;
}

} //namespace

int main(int argc, char **argv)
{
	try {
		const auto settings = parse_options(argc, argv);
		const auto paths = corpus_paths(settings.corpus);

		std::vector<input> corpus;
		corpus.reserve(paths.size());
		for(const auto &path : paths)
			corpus.push_back(read_input(path, settings.max_length));

		size_t run_count = 0;
		const auto boundaries = edge_inputs(settings.max_length);
		for(const auto &value : boundaries)
		{
			run_case(value, "edge-" + std::to_string(run_count++),
				settings.artifact);
		}
		for(size_t index = 0; index < corpus.size(); ++index)
		{
			const auto &path = paths[index];
			const auto label = std::filesystem::relative(
				path, settings.corpus).generic_string();
			run_case(corpus[index], "seed-" + label, settings.artifact);
		}

		uint64_t seed = settings.seed;
		for(const auto &path : paths)
		{
			seed ^= hash_text(std::filesystem::relative(
				path, settings.corpus).generic_string());
			seed = (seed << 1U) | (seed >> 63U);
		}
		random_generator random(seed);

		for(size_t index = 0; index < settings.runs; ++index)
		{
			const size_t source = index % corpus.size();
			const auto value = mutate(corpus[source], settings.max_length, random);
			run_case(value, "mutate-" + std::to_string(index) + "-" +
				paths[source].filename().string(), settings.artifact);
		}

		std::cerr << "GNU fuzz regression completed: " <<
			(settings.runs + corpus.size() + boundaries.size()) <<
			" edge, corpus, and mutation cases\n";
		return 0;
	}
	catch(const std::exception &error) {
		std::cerr << "GNU fuzz regression failed: " << error.what() << '\n';
		return 1;
	}
}
