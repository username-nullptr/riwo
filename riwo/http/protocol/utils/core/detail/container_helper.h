// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_PROTOCOL_UTILS_CORE_DETAIL_CONTAINER_HELPER_H
#define RIWO_HTTP_PROTOCOL_UTILS_CORE_DETAIL_CONTAINER_HELPER_H

#include <riwo/core/algorithm/uuid.h>

namespace riwo::http
{

template <typename Derived>
const_headers<Derived>::const_headers(const headers_t *headers) :
	m_headers(headers)
{

}

template <typename Derived>
auto const_headers<Derived>::header
(const core_concepts::text_p<char> auto &key) const noexcept -> optional<value_t>
{
	auto it = headers().find(strtls::to_string(key));
	if( it == headers().end() )
		return nullopt;
	return it->second;
}

template <typename Derived>
bool const_headers<Derived>::contains_header
(const core_concepts::text_p<char> auto &key, const value_t &value) const noexcept
{
	auto it = headers().find(strtls::to_string(key));
	if( it != headers().end() )
		return it->second == value;
	return false;
}

template <typename Derived>
bool const_headers<Derived>::contains_header
(const core_concepts::text_p<char> auto &key) const noexcept
{
	auto it = headers().find(strtls::to_string(key));
	return it != headers().end();
}

template <typename Derived>
auto const_headers<Derived>::headers() const noexcept -> const headers_t&
{
	return *m_headers;
}

template <typename Cookie, typename Derived>
const_cookies<Cookie,Derived>::const_cookies(const cookies_t *cookies) :
	m_cookies(cookies)
{

}

template <typename Cookie, typename Derived>
auto const_cookies<Cookie,Derived>::cookie
(const core_concepts::text_p<char> auto &key) const noexcept -> optional<cookie_t>
{
	auto it = cookies().find(strtls::to_string(key));
	if( it == cookies().end() )
		return nullopt;
	return it->second;
}

template <typename Cookie, typename Derived>
bool const_cookies<Cookie,Derived>::contains_cookie
(const core_concepts::text_p<char> auto &key) const noexcept
{
	return cookies().contains(strtls::to_string(key));
}

template <typename Cookie, typename Derived>
auto const_cookies<Cookie,Derived>::cookies() const noexcept -> const cookies_t&
{
	return *m_cookies;
}

template <typename Derived>
const_chunk_attributes<Derived>::const_chunk_attributes(const values_t *chunk_attributes) :
	m_chunk_attributes(chunk_attributes)
{

}

template <typename Derived>
bool const_chunk_attributes<Derived>::contains_chunk_attribute(const value_t &attr) const noexcept
{
	return chunk_attributes().contains(attr);
}

template <typename Derived>
const const_chunk_attributes<Derived>::values_t&
const_chunk_attributes<Derived>::chunk_attributes() const noexcept
{
	return *m_chunk_attributes;
}

template <typename Derived>
template <core_concepts::text_p<char> T>
auto mutable_headers<Derived>::set_header
(T &&key, typename base_t::value_t value) noexcept -> base_t::derived_t&
{
	headers()[strtls::to_string(std::forward<T>(key))] = std::move(value);
	return static_cast<base_t::derived_t&>(*this);
}

template <typename Derived>
template <core_concepts::text_p<char> T>
auto mutable_headers<Derived>::unset_header(const T &key) noexcept -> base_t::derived_t&
{
	headers().erase(strtls::to_string(key));
	return static_cast<base_t::derived_t&>(*this);
}

template <typename Derived>
auto mutable_headers<Derived>::headers() noexcept -> base_t::headers_t&
{
	return remove_const(*this->m_headers);
}

template <typename Derived>
template <typename Opt>
auto mutable_headers<Derived>::set_header(Opt &&opt)
	noexcept requires file_opt_token_v<Opt>
{
	auto token = make_file_opt_token(std::forward<decltype(opt)>(opt));
	using token_t = std::remove_cvref_t<decltype(*token)>;

	using pair_t = std::pair<body_norms_t,token_t>;
	using expected_t = sys_expected<pair_t>;

	if( not token )
		return expected_t(sys_unexpected(token.error()));

	using header_t = base_t::header_t;
	body_norms_t mode;

	if( token->ranges.empty() )
	{
		set_header(header_t::content_length, token->file_size);
		set_header(header_t::content_type  , token->mime_type);
		mode = basic_body_norms();
	}
	else if( token->ranges.size() == 1 )
	{
		auto &range = token->ranges.front();
		auto end = range.begin + range.total - 1;

		if( range.total == 0 or end >= token->file_size )
		{
			return expected_t(sys_unexpected (
				std::make_error_code(std::errc::invalid_seek)
			));
		}
		set_header(header_t::content_length, range.total     );
		 set_header(header_t::content_type  , token->mime_type);
		set_header(header_t::accept_ranges , "bytes"         );

		set_header(header_t::content_range, value {
			"bytes {}-{}/{}", range.begin, end, token->file_size
		});
		mode = range;
	}
	else
	{
		multipart_body_norms norms;
		{
			constexpr std::string_view prefix =
				"multipart/byteranges; boundary=";

			auto generate_boundary = [&]
			{
				using namespace std::chrono;
				norms.boundary = std::format("{}_{}",
					uuid::generate().to_string(),
					duration_cast<milliseconds>(
						system_clock::now().time_since_epoch()
					).count()
				);
				set_header(header_t::content_type,
					std::string(prefix) + norms.boundary
				);
			};
			auto it = headers().find(header_t::content_type);
			if( it == headers().end() )
				generate_boundary();
			else
			{
				if( it->second->size() > prefix.size() and it->second->starts_with(prefix) )
					norms.boundary = it->second->substr().substr(prefix.size());
				else
					generate_boundary();
			}
		}
		size_t content_length = 0;
		/*
			--boundary<CR><LF>
			Content-Type: xxx<CR><LF>
			Content-Range: bytes 3-11/96<CR><LF>
			<CR><LF>
			012345678<CR><LF>
			--boundary<CR><LF>
			Content-Type: xxx<CR><LF>
			Content-Range: bytes 0-7/96<CR><LF>
			<CR><LF>
			01235467<CR><LF>
			--boundary--<CR><LF>
		*/
		auto ct_line = std::format("{}: {}",
			header_t::content_type, token->mime_type
		);
		for(auto &range : token->ranges)
		{
			auto end = range.begin + range.total - 1;
			if( range.total == 0 or end >= token->file_size )
			{
				return expected_t(sys_unexpected (
					std::make_error_code(std::errc::invalid_seek)
				));
			}
			auto &package = norms.packages.emplace_back();
			package.range = range;

			auto cr_line = std::format("{}: bytes {}-{}/{}",
				header_t::content_range, range.begin, end, token->file_size
			);
			package.headers.emplace_back(ct_line);
			package.headers.emplace_back(cr_line);

			content_length += 2 + norms.boundary.size() + 2 +  // --boundary<CR><LF>
							  ct_line.size() + 2 +             // Content-Type: xxx<CR><LF>
							  cr_line.size() + 2 +             // Content-Range: bytes 3-11/96<CR><LF>
							  2 +                              // <CR><LF>
							  range.total + 2;                 // 012345678<CR><LF>
		}
		content_length += 2 + norms.boundary.size() + 2 + 2;   // --boundary--<CR><LF>

		set_header(header_t::content_length, content_length);
		set_header(header_t::accept_ranges , "bytes"       );
		mode = norms;
	}
	return expected_t(pair_t (
		std::move(mode), std::move(*token)
	));
}

template <typename Derived>
template <typename Opt>
auto mutable_headers<Derived>::make_file_opt_token(Opt &&opt)
	noexcept requires file_opt_token_v<Opt>
{
	using opt_t = std::remove_cvref_t<Opt>;
	if constexpr( is_any_string_v<opt_t> or std::same_as<opt_t,std::filesystem::path> or
		is_fstream_v<opt_t,char> or is_ifstream_v<opt_t,char> )
	{
		using token_t = decltype(http::make_file_opt_token(std::forward<Opt>(opt)));
		using type = token_t::type;

		using res_token_t = file_opt_token<type,file_optype::multiple> ;
		res_token_t token(std::forward<Opt>(opt));

		auto expected = token.init(std::ios::in | std::ios::binary);
		if( expected )
			return sys_expected<res_token_t>(std::move(token));
		return sys_expected<res_token_t>(sys_unexpected(expected.error()));
	}
	else if constexpr( opt_t::optype == file_optype::single )
	{
		using type = opt_t::type;
		using res_token_t = file_opt_token<type,file_optype::multiple> ;

		res_token_t token(std::forward<Opt>(opt));
		if( token.stream->is_open() )
			return sys_expected<res_token_t>(std::move(token));

		auto expected = token.init(std::ios::in | std::ios::binary);
		if( expected )
			return sys_expected<res_token_t>(std::move(token));
		return sys_expected<res_token_t>(sys_unexpected(expected.error()));
	}
	else
	{
		auto expected = opt.init(std::ios::in | std::ios::binary);
		if( expected )
			return sys_expected<opt_t>(std::forward<Opt>(opt));
		return sys_expected<opt_t>(sys_unexpected(expected.error()));
	}
}

template <typename Cookie, typename Derived>
template <core_concepts::text_p<char> T>
auto mutable_cookies<Cookie,Derived>::set_cookie
(T &&key, typename base_t::cookie_t value) noexcept -> base_t::derived_t &
{
	cookies()[strtls::to_string(std::forward<T>(key))] = std::move(value);
	return static_cast<base_t::derived_t&>(*this);
}

template <typename Cookie, typename Derived>
template <core_concepts::text_p<char> T>
auto mutable_cookies<Cookie,Derived>::unset_cookie(const T &key) noexcept -> base_t::derived_t&
{
	cookies().erase(strtls::to_string(key));
	return static_cast<base_t::derived_t&>(*this);
}

template <typename Cookie, typename Derived>
auto mutable_cookies<Cookie,Derived>::cookies() noexcept -> base_t::cookies_t&
{
	return remove_const(*this->m_cookies);
}

template <typename Derived>
auto mutable_chunk_attributes<Derived>::
set_chunk_attribute(typename base_t::value_t attr) noexcept -> base_t::derived_t&
{
	if( auto [it, inserted] = chunk_attributes().emplace(std::move(attr)); not inserted )
	{
		chunk_attributes().erase(it);
		chunk_attributes().emplace(std::move(attr));
	}
	return static_cast<base_t::derived_t&>(*this);
}

template <typename Derived>
auto mutable_chunk_attributes<Derived>::
unset_chunk_attribute(const typename base_t::value_t &attr) noexcept -> base_t::derived_t&
{
	chunk_attributes().erase(attr);
	return static_cast<base_t::derived_t&>(*this);
}

template <typename Derived>
auto mutable_chunk_attributes<Derived>::chunk_attributes() noexcept -> base_t::values_t&
{
	return remove_const(*this->m_chunk_attributes);
}

} //namespace riwo::http


#endif //RIWO_HTTP_PROTOCOL_UTILS_CORE_DETAIL_CONTAINER_HELPER_H
