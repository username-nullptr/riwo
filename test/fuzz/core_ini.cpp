// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/core/ini.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{

class fuzz_workspace
{
public:
	fuzz_workspace()
	{
		std::error_code error;
		const auto base = std::filesystem::temp_directory_path(error);
		if( error )
			std::abort();

		const auto stamp = std::chrono::steady_clock::now()
			.time_since_epoch().count();
		for(size_t suffix = 0; suffix < 100; ++suffix)
		{
			m_path = base / ("riwo-fuzz-ini-" + std::to_string(stamp) +
				"-" + std::to_string(suffix));
			if( std::filesystem::create_directory(m_path, error) )
				return ;
			if( error )
				std::abort();
		}
		std::abort();
	}

	~fuzz_workspace()
	{
		std::error_code error;
		std::filesystem::remove_all(m_path, error);
	}

	[[nodiscard]] std::filesystem::path file(const char *name) const {
		return m_path / name;
	}

private:
	std::filesystem::path m_path;
};

[[nodiscard]] fuzz_workspace &workspace()
{
	static fuzz_workspace result;
	return result;
}

void write_file(const std::filesystem::path &path, const std::string &contents)
{
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if( not stream )
		std::abort();
	stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	if( not stream )
		std::abort();
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path)
{
	std::ifstream stream(path, std::ios::binary);
	if( not stream )
		std::abort();
	return std::string(std::istreambuf_iterator<char>(stream), {});
}

void verify_roundtrip(const riwo::ini &source,
	const std::filesystem::path &first_file,
	const std::filesystem::path &second_file)
{
	const auto expected = source.data();
	std::error_code error;

	riwo::ini copy(expected);
	copy.sync(first_file, error);
	if( error )
		std::abort();

	riwo::ini restored(first_file);
	restored.load(error);
	if( error or restored.data() != expected )
		std::abort();

	restored.sync(second_file, error);
	if( error or read_file(first_file) != read_file(second_file) )
		std::abort();
}

} //namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if( size > RIWO_FUZZ_MAX_LENGTH )
		return 0;

	std::string input;
	if( size != 0 )
		input.assign(reinterpret_cast<const char*>(data), size);

	auto &files = workspace();
	const auto input_file = files.file("input.ini");
	const auto parsed_file = files.file("parsed.ini");
	const auto parsed_again_file = files.file("parsed-again.ini");
	write_file(input_file, input);

	std::error_code error;
	riwo::ini parsed(input_file);
	parsed.load(error);
	if( not error )
		verify_roundtrip(parsed, parsed_file, parsed_again_file);

	std::string group = "group:";
	group += input;
	std::string key = "key:";
	key += input;

	riwo::ini generated;
	generated.write({group, key}, input);
	generated.write({"fixed", "empty"}, "");
	verify_roundtrip(generated, files.file("generated.ini"),
		files.file("generated-again.ini"));
	return 0;
}
