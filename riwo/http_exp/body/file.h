// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_FILE_H
#define RIWO_HTTP_EXP_BODY_FILE_H

#include <riwo/http_exp/global.h>
#include <cstdio>
#include <fstream>

namespace riwo::http_exp
{

struct file_span
{
	uint64_t offset = 0;
	uint64_t length = 0;
};

namespace concepts
{

template <typename Stream>
concept input_file_stream =
	std::same_as<std::remove_cvref_t<Stream>,std::ifstream> or
	std::same_as<std::remove_cvref_t<Stream>,std::fstream>;

template <typename Stream>
concept output_file_stream =
	std::same_as<std::remove_cvref_t<Stream>,std::ofstream> or
	std::same_as<std::remove_cvref_t<Stream>,std::fstream>;

} //namespace concepts

template <typename Stream>
requires (concepts::input_file_stream<Stream> or
	concepts::output_file_stream<Stream>)
class borrowed_file_ref
{
public:
	explicit borrowed_file_ref(Stream &stream) noexcept;
	borrowed_file_ref(const borrowed_file_ref&) = delete;
	borrowed_file_ref &operator=(const borrowed_file_ref&) = delete;
	borrowed_file_ref(borrowed_file_ref &&other) noexcept;
	borrowed_file_ref &operator=(borrowed_file_ref &&other) noexcept;

	[[nodiscard]] Stream *get() const noexcept;

private:
	Stream *m_stream = nullptr;
};

template <typename Stream>
requires (concepts::input_file_stream<Stream> or
	concepts::output_file_stream<Stream>)
[[nodiscard]] borrowed_file_ref<Stream> borrowed_file(Stream &stream) noexcept;

class RIWO_HTTP_EXP_API owned_c_file
{
	RIWO_DISABLE_COPY(owned_c_file)

public:
	explicit owned_c_file(std::FILE *file = nullptr) noexcept;
	~owned_c_file();
	owned_c_file(owned_c_file &&other) noexcept;
	owned_c_file &operator=(owned_c_file &&other) noexcept;

	[[nodiscard]] std::FILE *get() const noexcept;
	[[nodiscard]] std::FILE *release() noexcept;

private:
	std::FILE *m_file = nullptr;
};

class RIWO_HTTP_EXP_API borrowed_c_file
{
	RIWO_DISABLE_COPY(borrowed_c_file)

public:
	explicit borrowed_c_file(std::FILE *file = nullptr) noexcept;
	borrowed_c_file(borrowed_c_file &&other) noexcept;
	borrowed_c_file &operator=(borrowed_c_file &&other) noexcept;

	[[nodiscard]] std::FILE *get() const noexcept;

private:
	std::FILE *m_file = nullptr;
};

[[nodiscard]] RIWO_HTTP_EXP_API owned_c_file
owned_file(std::FILE *file) noexcept;

[[nodiscard]] RIWO_HTTP_EXP_API borrowed_c_file
borrowed_file(std::FILE *file) noexcept;

class RIWO_HTTP_EXP_API file_body_source
{
	RIWO_DISABLE_COPY(file_body_source)

public:
	explicit file_body_source(std::filesystem::path path,
		optional<file_span> span = {});
	explicit file_body_source(std::ifstream &&stream,
		optional<file_span> span = {});
	explicit file_body_source(std::fstream &&stream,
		optional<file_span> span = {});
	explicit file_body_source(borrowed_file_ref<std::ifstream> stream,
		optional<file_span> span = {});
	explicit file_body_source(borrowed_file_ref<std::fstream> stream,
		optional<file_span> span = {});
	explicit file_body_source(owned_c_file file,
		optional<file_span> span = {});
	explicit file_body_source(borrowed_c_file file,
		optional<file_span> span = {});
	~file_body_source();
	file_body_source(file_body_source &&) noexcept;
	file_body_source &operator=(file_body_source &&) noexcept;

	[[nodiscard]] optional<uint64_t> size() const noexcept;
	[[nodiscard]] bool replayable() const noexcept;
	[[nodiscard]] size_t read_some(mutable_buffer buffer,
		error_code &error) noexcept;
	void read_some(mutable_buffer buffer,
		asio::any_completion_handler<void(error_code,size_t)> handler);
	void rewind(asio::any_completion_handler<void(error_code)> handler);
	void finish(error_code &error) noexcept;
	void finish(asio::any_completion_handler<void(error_code)> handler);
	void cancel() noexcept;

private:
	class impl;
	std::unique_ptr<impl> m_impl;
};

class RIWO_HTTP_EXP_API file_sink
{
	RIWO_DISABLE_COPY(file_sink)

public:
	explicit file_sink(std::filesystem::path path);
	explicit file_sink(std::ofstream &&stream);
	explicit file_sink(std::fstream &&stream);
	explicit file_sink(borrowed_file_ref<std::ofstream> stream);
	explicit file_sink(borrowed_file_ref<std::fstream> stream);
	explicit file_sink(owned_c_file file);
	explicit file_sink(borrowed_c_file file);
	~file_sink();
	file_sink(file_sink &&) noexcept;
	file_sink &operator=(file_sink &&) noexcept;

	void prepare(error_code &error) const noexcept;
	[[nodiscard]] bool random_access() const noexcept;
	[[nodiscard]] size_t write(const_buffer buffer,
		error_code &error) noexcept;
	void write(const_buffer buffer,
		asio::any_completion_handler<void(error_code,size_t)> handler);
	[[nodiscard]] size_t write_at(uint64_t offset, const_buffer buffer,
		error_code &error) noexcept;
	void write_at(uint64_t offset, const_buffer buffer,
		asio::any_completion_handler<void(error_code,size_t)> handler);
	void finish(error_code &error) noexcept;
	void finish(asio::any_completion_handler<void(error_code)> handler);
	void cancel() noexcept;

private:
	class impl;
	std::unique_ptr<impl> m_impl;
};

namespace concepts
{

template <typename Sink>
concept body_sink_model = requires(
	Sink &sink,
	const_buffer buffer,
	error_code &error,
	asio::any_completion_handler<void(error_code,size_t)> write_handler,
	asio::any_completion_handler<void(error_code)> finish_handler)
{
	{ sink.write(buffer, error) } -> std::same_as<size_t>;
	{ sink.write(buffer, std::move(write_handler)) } -> std::same_as<void>;
	{ sink.finish(error) } -> std::same_as<void>;
	{ sink.finish(std::move(finish_handler)) } -> std::same_as<void>;
	{ sink.cancel() } noexcept -> std::same_as<void>;
};

} //namespace concepts

static_assert(concepts::body_sink_model<file_sink>);

} //namespace riwo::http_exp

#include <riwo/http_exp/body/detail/file.h>

#endif //RIWO_HTTP_EXP_BODY_FILE_H
