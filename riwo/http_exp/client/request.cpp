// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "request.h"

namespace riwo::http_exp
{

namespace
{

class borrowed_body_source
{
public:
	explicit borrowed_body_source(const_buffer body) : body_(body) {}

	[[nodiscard]] optional<uint64_t> size() const noexcept {
		return body_.size();
	}

	[[nodiscard]] bool replayable() const noexcept {
		return true;
	}

	[[nodiscard]] size_t read_some(mutable_buffer buffer,
		error_code &error) noexcept
	{
		error.clear();
		if( cancelled_ )
		{
			error = asio::error::make_error_code(asio::error::operation_aborted);
			return 0;
		}
		if( buffer.size() == 0 ) return 0;
		if( offset_ == body_.size() )
		{
			error = asio::error::make_error_code(asio::error::eof);
			return 0;
		}
		const auto size = std::min(buffer.size(), body_.size() - offset_);
		std::memcpy(buffer.data(),
			static_cast<const std::byte*>(body_.data()) + offset_, size);
		offset_ += size;
		return size;
	}

	void read_some(mutable_buffer buffer,
		asio::any_completion_handler<void(error_code,size_t)> handler)
	{
		error_code error;
		auto size = read_some(buffer, error);
		std::move(handler)(error, size);
	}

	void rewind(asio::any_completion_handler<void(error_code)> handler)
	{
		offset_ = 0;
		cancelled_ = false;
		std::move(handler)(error_code{});
	}

	void cancel() noexcept {
		cancelled_ = true;
	}

private:
	const_buffer body_;
	size_t offset_ = 0;
	bool cancelled_ = false;
};

} //namespace

request::request(http_exp::method request_method, riwo::url request_url) :
	head_ {std::move(request_method), std::move(request_url)}
{
}

request::~request() = default;
request::request(request &&) noexcept = default;
request &request::operator=(request &&) noexcept = default;

http_exp::method request::method() const noexcept {
	return head_.method;
}

const riwo::url &request::url() const noexcept {
	return head_.url;
}

http_exp::fields &request::headers() noexcept {
	return head_.headers;
}

const http_exp::fields &request::headers() const noexcept {
	return head_.headers;
}

request &request::borrowed_body(const_buffer body)
{
	source_ = detail::make_body_source(borrowed_body_source(body));
	file_.clear();
	body_spec_ = http_exp::body_spec::known(body.size());
	trailers_ = {};
	return *this;
}

request &request::file(std::filesystem::path path)
{
	source_.reset();
	file_ = std::move(path);
	body_spec_ = http_exp::body_spec::streaming();
	trailers_ = {};
	return *this;
}

request &request::trailers(fields values)
{
	body_spec::trailer_spec description;
	for( const auto &value : values )
	{
		const auto duplicate = std::ranges::any_of(description.names,
		[&](const std::string &name) {
			return name.size() == value.name.size() &&
				std::ranges::equal(name, value.name, [](char lhs, char rhs) {
					return std::tolower(static_cast<unsigned char>(lhs)) ==
						std::tolower(static_cast<unsigned char>(rhs));
				});
		});
		if( !duplicate ) description.names.push_back(value.name);
	}
	if( auto length = body_spec_.length() )
		body_spec_ = body_spec::known(*length, std::move(description));
	else if( body_spec_.type() == body_spec::mode::streaming )
		body_spec_ = body_spec::streaming(std::move(description));
	else if( !values.empty() )
		throw std::invalid_argument("An empty HTTP body cannot have trailers");
	trailers_ = std::move(values);
	return *this;
}

const body_spec &request::body_description() const noexcept {
	return body_spec_;
}

bool request::has_body_source() const noexcept {
	return static_cast<bool>(source_) || !file_.empty();
}

} //namespace riwo::http_exp
