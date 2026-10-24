// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_FILE_SOURCE_H
#define RIWO_HTTP_EXP_BODY_FILE_SOURCE_H

#include <riwo/http_exp/body/file_handle.h>

namespace riwo::http_exp { namespace detail {
class input_file_backend;
} //namespace detail

struct file_span
{
	uint64_t offset = 0;
	uint64_t length = 0;
};

class RIWO_HTTP_EXP_API file_body_source
{
	RIWO_DISABLE_COPY(file_body_source)

public:
	explicit file_body_source(std::ifstream &&stream, const optional<file_span> &span = {});
	explicit file_body_source(std::fstream &&stream, const optional<file_span> &span = {});

	explicit file_body_source(const borrowed_file_ref<std::ifstream> &stream, optional<file_span> span = {});
	explicit file_body_source(const borrowed_file_ref<std::fstream> &stream, optional<file_span> span = {});

	explicit file_body_source(owned_c_file file, const optional<file_span> &span = {});
	explicit file_body_source(borrowed_c_file file, const optional<file_span> &span = {});

	explicit file_body_source(const std::filesystem::path &path, const optional<file_span> &span = {});
	~file_body_source();

	file_body_source(file_body_source &&other) noexcept;
	file_body_source &operator=(file_body_source &&other) noexcept;

public:
	void read_some(const mutable_buffer &buffer,
		asio::any_completion_handler<void(error_code,size_t)> handler
	);
	[[nodiscard]] size_t read_some(const mutable_buffer &buffer, error_code &error) noexcept;
	void rewind(asio::any_completion_handler<void(error_code)> handler);

	void finish(error_code &error) noexcept;
	void finish(asio::any_completion_handler<void(error_code)> handler);

	void cancel() noexcept;

public:
	[[nodiscard]] optional<uint64_t> size() const noexcept;
	[[nodiscard]] bool replayable() const noexcept;

private:
	std::unique_ptr<detail::input_file_backend> m_backend {};
	bool m_cancelled = false;
};

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_BODY_FILE_SOURCE_H
