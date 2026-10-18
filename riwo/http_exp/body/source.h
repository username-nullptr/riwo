// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_SOURCE_H
#define RIWO_HTTP_EXP_BODY_SOURCE_H

#include <riwo/http_exp/body/spec.h>

namespace riwo::http_exp
{

template <typename Source>
concept body_source_model = requires(
	Source &source,
	mutable_buffer buffer,
	asio::any_completion_handler<void(error_code, size_t)> read_handler,
	asio::any_completion_handler<void(error_code)> rewind_handler)
{
	{ source.size() } noexcept -> std::same_as<optional<uint64_t>>;
	{ source.replayable() } noexcept -> std::same_as<bool>;
	{ source.read_some(buffer, std::move(read_handler)) } -> std::same_as<void>;
	{ source.rewind(std::move(rewind_handler)) } -> std::same_as<void>;
	{ source.cancel() } noexcept -> std::same_as<void>;
};

class RIWO_HTTP_EXP_API memory_body_source
{
public:
	memory_body_source();
	explicit memory_body_source(std::vector<std::byte> bytes);
	explicit memory_body_source(const_buffer bytes);

	[[nodiscard]] optional<uint64_t> size() const noexcept;
	[[nodiscard]] bool replayable() const noexcept;
	[[nodiscard]] size_t read_some(mutable_buffer buffer,
		error_code &error) noexcept;
	void read_some(mutable_buffer buffer,
		asio::any_completion_handler<void(error_code,size_t)> handler);
	void rewind(asio::any_completion_handler<void(error_code)> handler);
	void cancel() noexcept;

private:
	std::vector<std::byte> bytes_;
	size_t offset_ = 0;
	bool cancelled_ = false;
};

class RIWO_HTTP_EXP_API file_body_source
{
	RIWO_DISABLE_COPY(file_body_source)

public:
	explicit file_body_source(std::filesystem::path path);
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
	void cancel() noexcept;

private:
	class impl;
	std::unique_ptr<impl> impl_;
};

static_assert(body_source_model<memory_body_source>);
static_assert(body_source_model<file_body_source>);

} //namespace riwo::http_exp

#include <riwo/http_exp/body/detail/source.h>

#endif //RIWO_HTTP_EXP_BODY_SOURCE_H
