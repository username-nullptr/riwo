// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "body_spec.h"
#include <riwo/http_exp/protocol/fields.h>
#include <riwo/core/utils/string_tools.h>

namespace riwo::http_exp { namespace
{

[[nodiscard]] bool equal_name(std::string_view lhs, std::string_view rhs) noexcept
{
	return strtls::ascii_equal(lhs, rhs, false);
}

void validate(body_spec::mode mode, const body_spec::trailer_spec &trailers)
{
	if( mode == body_spec::mode::empty and not trailers.names.empty() )
		invalid_argument::loc_throw("An empty HTTP body cannot have trailers");

	fields validator;
	for(size_t index=0; index<trailers.names.size(); ++index)
	{
		validator.add(trailers.names[index], "value");
		for(size_t previous=0; previous<index; ++previous)
		{
			if( equal_name(trailers.names[index], trailers.names[previous]) )
				invalid_argument::loc_throw("Duplicate HTTP trailer name");
		}
		const auto &name = trailers.names[index];
		if( equal_name(name, "content-length") or equal_name(name, "transfer-encoding") or
			equal_name(name, "host") or equal_name(name, "trailer") )
			invalid_argument::loc_throw("Forbidden HTTP trailer name");
	}
}

} //namespace

body_spec::body_spec(mode type, optional<uint64_t> length, trailer_spec trailers) :
	m_type(type), m_length(length), m_trailers(std::move(trailers))
{
	validate(m_type, m_trailers);
}

body_spec::body_spec(body_spec &&other) noexcept :
	m_type(std::exchange(other.m_type, mode::empty)),
	m_length(std::exchange(other.m_length, nullopt)),
	m_trailers(std::move(other.m_trailers))
{
	other.m_trailers.names.clear();
}

body_spec &body_spec::operator=(body_spec &&other) noexcept
{
	if( this != &other )
	{
		m_type = std::exchange(other.m_type, mode::empty);
		m_length = std::exchange(other.m_length, nullopt);
		m_trailers = std::move(other.m_trailers);
		other.m_trailers.names.clear();
	}
	return *this;
}

body_spec body_spec::empty()
{
	return {mode::empty, nullopt, {}};
}

body_spec body_spec::known(uint64_t size, trailer_spec trailers)
{
	return {mode::known_length, size, std::move(trailers)};
}

body_spec body_spec::streaming(trailer_spec trailers)
{
	return {mode::streaming, nullopt, std::move(trailers)};
}

body_spec body_spec::head(uint64_t content_length)
{
	return {mode::head, content_length, {}};
}

body_spec::mode body_spec::type() const noexcept
{
	return m_type;
}

optional<uint64_t> body_spec::length() const noexcept
{
	return m_length;
}

const body_spec::trailer_spec &body_spec::trailers() const noexcept
{
	return m_trailers;
}

} //namespace riwo::http_exp
