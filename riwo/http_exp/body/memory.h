// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_MEMORY_H
#define RIWO_HTTP_EXP_BODY_MEMORY_H

#include <riwo/http_exp/global.h>

namespace riwo::http_exp
{

class RIWO_HTTP_EXP_API memory_body_source
{
public:
	explicit memory_body_source(std::vector<std::byte> bytes);
	explicit memory_body_source(const const_buffer &bytes);

	memory_body_source();
	~memory_body_source();

	memory_body_source(const memory_body_source &other);
	memory_body_source &operator=(const memory_body_source &other);

	memory_body_source(memory_body_source &&) noexcept;
	memory_body_source &operator=(memory_body_source &&) noexcept;

public:
	void read_some(const mutable_buffer& buffer,
		asio::any_completion_handler<void(error_code,size_t)> handler
	);
	[[nodiscard]] size_t read_some (
		const mutable_buffer &buffer, error_code &error
	) noexcept;

	void rewind(asio::any_completion_handler<void(error_code)> handler);
	void cancel() noexcept;

	[[nodiscard]] optional<uint64_t> size() const noexcept;
	[[nodiscard]] constexpr bool replayable() const noexcept { return true; }

private:
	std::vector<std::byte> m_bytes {};
	size_t m_offset = 0;
	bool m_cancelled = false;
};

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_BODY_MEMORY_H
