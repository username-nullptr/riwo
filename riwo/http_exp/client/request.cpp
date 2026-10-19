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
	m_impl(std::make_unique<impl>(
		std::move(request_method), std::move(request_url)))
{
}

request::~request() = default;
request::request(request &&) noexcept = default;
request &request::operator=(request &&) noexcept = default;

http_exp::method request::method() const noexcept {
	return m_impl->head.method;
}

const riwo::url &request::url() const noexcept {
	return m_impl->head.url;
}

http_exp::fields &request::headers() noexcept {
	return m_impl->head.headers;
}

const http_exp::fields &request::headers() const noexcept {
	return m_impl->head.headers;
}

request &request::borrowed_body(const_buffer body)
{
	m_impl->source = detail::make_body_source(borrowed_body_source(body));
	m_impl->file.clear();
	m_impl->body_spec = http_exp::body_spec::known(body.size());
	m_impl->trailers = {};
	return *this;
}

request &request::file(std::filesystem::path path)
{
	m_impl->source.reset();
	m_impl->file = std::move(path);
	m_impl->body_spec = http_exp::body_spec::streaming();
	m_impl->trailers = {};
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
	if( auto length = m_impl->body_spec.length() )
		m_impl->body_spec = body_spec::known(*length, std::move(description));
	else if( m_impl->body_spec.type() == body_spec::mode::streaming )
		m_impl->body_spec = body_spec::streaming(std::move(description));
	else if( !values.empty() )
		throw std::invalid_argument("An empty HTTP body cannot have trailers");
	m_impl->trailers = std::move(values);
	return *this;
}

const body_spec &request::body_description() const noexcept {
	return m_impl->body_spec;
}

bool request::has_body_source() const noexcept {
	return static_cast<bool>(m_impl->source) || !m_impl->file.empty();
}

} //namespace riwo::http_exp
