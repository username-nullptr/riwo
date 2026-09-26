// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_TOOLS_CORE_GENERATOR_H
#define RIWO_HTTP_TOOLS_CORE_GENERATOR_H

#include <riwo/http/protocol/utils/core/container_helper.h>
#include <riwo/http/protocol/utils/core/generator_types.h>
#include <riwo/http/protocol/utils/core/body_norms.h>

namespace riwo::http
{

template <>
class RIWO_HTTP_API generator<protocol_model::base> :
	public mutable_headers<generator<protocol_model::base>>,
	public mutable_chunk_attributes<generator<protocol_model::base>>
{
	RIWO_DISABLE_COPY_MOVE(generator)

public:
	using state_t = generator_state;

	generator();
	~generator() override = 0;

	[[nodiscard]] virtual std::string header_data(size_t body_size) noexcept;
	[[nodiscard]] std::string header_data_no_body(bool preserve_content_length = false) noexcept;

	// Returns a borrowed slice for Content-Length framing and advances the
	// generator state without copying the payload. Other framing modes return an
	// empty buffer and continue to use body_data().
	[[nodiscard]] const_buffer body_buffer(const const_buffer &buffer) noexcept;
	[[nodiscard]] virtual std::string body_data(const const_buffer &buffer) noexcept;
	[[nodiscard]] virtual std::string chunk_end_data(const headers_t &trailer_headers) noexcept;

	[[nodiscard]] std::string header_data() noexcept;
	[[nodiscard]] std::string chunk_end_data() noexcept;

public:
	[[nodiscard]] virtual version_enum version() const noexcept = 0;
	[[nodiscard]] state_t state() const noexcept;
	generator &reset();

protected:
	class impl;
	impl *m_impl;
};

template <protocol_model> class generator_v10 {};
template <protocol_model> class generator_v11 {};

template <>
class RIWO_HTTP_API generator_v10<protocol_model::base> final :
	public generator<protocol_model::base>
{
	RIWO_DISABLE_COPY_MOVE(generator_v10)

public:
	using generator::generator;
	[[nodiscard]] version_enum version() const noexcept override;
};

template <>
class RIWO_HTTP_API generator_v11<protocol_model::base> final :
	public generator<protocol_model::base>
{
	RIWO_DISABLE_COPY_MOVE(generator_v11)

public:
	using generator::generator;
	[[nodiscard]] std::string header_data(size_t body_size) noexcept override;
	[[nodiscard]] version_enum version() const noexcept override;
};

// ... ...
// class RIWO_HTTP_API generator_v12 final : public generator
// class RIWO_HTTP_API generator_v20 final : public generator

using base_generator = generator<protocol_model::base>;
using base_generator_v10 = generator_v10<protocol_model::base>;
using base_generator_v11 = generator_v11<protocol_model::base>;

} //namespace riwo::http
#include <riwo/http/protocol/utils/core/detail/generator.h>


#endif //RIWO_HTTP_TOOLS_CORE_GENERATOR_H
