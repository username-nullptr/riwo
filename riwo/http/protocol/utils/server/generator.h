// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_SERVER_GENERATOR_H
#define RIWO_HTTP_PROTOCOL_UTILS_SERVER_GENERATOR_H

#include <riwo/http/protocol/utils/core/container_helper.h>
#include <riwo/http/protocol/utils/core/generator_types.h>

namespace riwo::http
{

template <>
class RIWO_HTTP_API generator<protocol_model::server> final :
	public mutable_headers<generator<protocol_model::server>>,
	public mutable_cookies<cookie,generator<protocol_model::server>>,
	public mutable_chunk_attributes<generator<protocol_model::server>>
{
	RIWO_DISABLE_COPY(generator)

public:
	using version_t = http::version;

	explicit generator(version_enum version = version_t::v11);
	~generator() override;

	generator(generator &&other) noexcept;
	generator &operator=(generator &&other) noexcept;

public:
	generator &set_status(status_enum status);
	[[nodiscard]] status_enum status() const noexcept;

	generator &set_redirect (
		core_concepts::text_p<char> auto &&url,
		redirect_enum type = redirect::moved_permanently
	);

public:
	[[nodiscard]] std::string header_data(size_t body_size = 0);
	[[nodiscard]] std::string header_data(size_t body_size, method_enum request_method);

	[[nodiscard]] const_buffer body_buffer(const const_buffer &buffer) noexcept;
	[[nodiscard]] std::string body_data(const const_buffer &buffer);
	[[nodiscard]] std::string chunk_end_data(const headers_t &trailer_headers = {});

	[[nodiscard]] version_enum version() const noexcept;
	[[nodiscard]] generator_state pro_state() const noexcept;
	generator &reset() noexcept;

private:
	class impl;
	impl *m_impl;
};

using server_generator = generator<protocol_model::server>;

} //namespace riwo::http
#include <riwo/http/protocol/utils/server/detail/generator.h>


#endif //RIWO_HTTP_PROTOCOL_UTILS_SERVER_GENERATOR_H
