// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "parser_types.h"

namespace riwo::http { namespace
{

class RIWO_DECL_HIDDEN error_category final : public error_category_t
{
	RIWO_DISABLE_COPY_MOVE(error_category)

public:
	error_category() = default;
#if RIWO_USING_BOOST_ASIO
	virtual ~error_category() = default;
#else //RIWO_USING_BOOST_ASIO
	~error_category() override = default;
#endif //RIWO_USING_BOOST_ASIO

public:
	[[nodiscard]] const char *name() const noexcept override {
		return "riwo::http::request_parser_error";
	}

	[[nodiscard]] std::string message(int code) const override
	{
		switch(static_cast<parse_errc>(code))
		{
#define X_MACRO(e,v,d) case parse_errc::e: return d;
			RIWO_HTTP_PARSE_ERRC_TABLE
	#undef X_MACRO
			default: break;
		}
		return "Unknown error.";
	}
}
g_error_category;

} //namespace

const error_category_t &parse_error_category() noexcept
{
	return g_error_category;
}

error_code make_error_code(parse_errc value) noexcept
{
	return {static_cast<int>(value), parse_error_category()};
}

} //namespace riwo::http
