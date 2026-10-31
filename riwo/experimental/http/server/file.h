// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_SERVER_FILE_H
#define RIWO_EXP_HTTP_SERVER_FILE_H

#include <riwo/experimental/http/protocol/response_head.h>
#include <riwo/experimental/http/protocol/request_head.h>
#include <riwo/experimental/http/protocol/range.h>
#include <riwo/experimental/http/body/source.h>

namespace riwo::experimental::http
{

struct file_response_options
{
	optional<std::string> content_type {};
	optional<std::string> content_disposition {};
	optional<std::string> cache_control {};

	size_t max_ranges = 16;
	uint64_t max_range_overhead = 1024U * 1024U;
};

class RIWO_EXP_HTTP_API file_response_source
{
	RIWO_DISABLE_COPY(file_response_source)

public:
	file_response_source(std::filesystem::path path,
		const request_head &request, file_response_options options = {}
	);
	~file_response_source();

	file_response_source(file_response_source &&other) noexcept;
	file_response_source &operator=(file_response_source &&other) noexcept;

public:
	[[nodiscard]] size_t read_some(const mutable_buffer &buffer, error_code &error) noexcept;
	void read_some(const mutable_buffer &buffer, asio::any_completion_handler<void(error_code,size_t)> handler);

	void finish(error_code &error) noexcept;
	void finish(asio::any_completion_handler<void(error_code)> handler);

	void rewind(asio::any_completion_handler<void(error_code)> handler);
	void cancel() noexcept;

public:
	[[nodiscard]] const response_head &response() const noexcept;
	[[nodiscard]] bool has_body() const noexcept;

	[[nodiscard]] optional<uint64_t> size() const noexcept;
	[[nodiscard]] bool replayable() const noexcept;

private:
	class impl;
	std::unique_ptr<impl> m_impl {};
};

static_assert(concepts::body_source_model<file_response_source>);
static_assert(concepts::finalizable_body_source_model<file_response_source>);

} //namespace riwo::experimental::http


#endif //RIWO_EXP_HTTP_SERVER_FILE_H
