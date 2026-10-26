// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_TRANSPORT_PREFACE_H
#define RIWO_HTTP_EXP_TRANSPORT_PREFACE_H

#include <riwo/http_exp/transport/connection.h>

namespace riwo::http_exp
{

constexpr std::string_view v2_preface {
	"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
};
enum class preface_result {
	need_more, v2, v1
};

// Matches the RFC 9113 client connection preface without consuming or
// discarding bytes. Every byte offered to the detector remains available for
// the selected protocol, which is what lets the server replay a non-matching
// prefix to the HTTP/1 parser.
class RIWO_DECL_HIDDEN v2_preface_detector
{
	RIWO_DISABLE_COPY_MOVE(v2_preface_detector)

public:
	using detect_handler_t = asio::any_completion_handler <
		void(error_code, preface_result, std::string)
	>;
	explicit v2_preface_detector(size_t max_bytes = v2_preface.size());
	~v2_preface_detector() = default;

public:
	[[nodiscard]] preface_result feed(const const_buffer &input);
	[[nodiscard]] preface_result result() const noexcept;

	[[nodiscard]] const std::string &buffer() const noexcept;
	[[nodiscard]] size_t buffered_size() const noexcept;

	[[nodiscard]] std::string take_buffer();
	void reset() noexcept;

	void detect(const connection_ptr &stream,
		detect_handler_t handler
	);

private:
	size_t m_max_bytes = 0;
	std::string m_buffer {};
	preface_result m_result = preface_result::need_more;
};

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_TRANSPORT_PREFACE_H
