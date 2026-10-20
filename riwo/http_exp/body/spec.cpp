// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "spec.h"

namespace riwo::http_exp { namespace
{

[[nodiscard]] bool equal_name(std::string_view lhs, std::string_view rhs) noexcept
{
	return lhs.size() == rhs.size() and std::ranges::equal(lhs, rhs, [](char left, char right) {
		return std::tolower(static_cast<unsigned char>(left)) ==
			std::tolower(static_cast<unsigned char>(right));
	});
}

void validate(body_spec::mode mode, const body_spec::trailer_spec &trailers)
{
	if( mode == body_spec::mode::empty and not trailers.names.empty() )
		throw std::invalid_argument("An empty HTTP body cannot have trailers");

	fields validator;
	for(size_t index = 0; index < trailers.names.size(); ++index)
	{
		validator.add(trailers.names[index], "value");
		for(size_t previous = 0; previous < index; ++previous)
		{
			if( equal_name(trailers.names[index], trailers.names[previous]) )
				throw std::invalid_argument("Duplicate HTTP trailer name");
		}
		const auto &name = trailers.names[index];
		if( equal_name(name, "content-length") or equal_name(name, "transfer-encoding") or
			equal_name(name, "host") or equal_name(name, "trailer") )
			throw std::invalid_argument("Forbidden HTTP trailer name");
	}
}

} //namespace

body_spec::body_spec(mode type, optional<uint64_t> length, trailer_spec trailers) :
	type_(type), length_(length), trailers_(std::move(trailers))
{
	validate(type_, trailers_);
}

body_spec body_spec::empty() {
	return {mode::empty, nullopt, {}};
}

body_spec body_spec::known(uint64_t size, trailer_spec trailers) {
	return {mode::known_length, size, std::move(trailers)};
}

body_spec body_spec::streaming(trailer_spec trailers) {
	return {mode::streaming, nullopt, std::move(trailers)};
}

body_spec body_spec::head(uint64_t content_length) {
	return {mode::head, content_length, {}};
}

body_spec::mode body_spec::type() const noexcept {
	return type_;
}

optional<uint64_t> body_spec::length() const noexcept {
	return length_;
}

const body_spec::trailer_spec &body_spec::trailers() const noexcept {
	return trailers_;
}

} //namespace riwo::http_exp
