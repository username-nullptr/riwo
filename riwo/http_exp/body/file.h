// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_FILE_H
#define RIWO_HTTP_EXP_BODY_FILE_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

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
	std::unique_ptr<impl> m_impl;
};

} //namespace riwo::http_exp

#endif //RIWO_HTTP_EXP_BODY_FILE_H
