// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "error.h"

namespace riwo::experimental::http { namespace
{

#if RIWO_USING_BOOST_ASIO && defined(__GNUC__) && !defined(__clang__)
// Boost.System deliberately gives error_category a protected non-virtual
// destructor. This static final category is never deleted polymorphically, but
// GCC still diagnoses the supported inheritance pattern under
// -Wnon-virtual-dtor.
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif //BOOST & GNU

class RIWO_DECL_HIDDEN exp_http_error_category final : public error_category_t
{
	RIWO_DISABLE_COPY_MOVE(exp_http_error_category)

public:
	exp_http_error_category() = default;
#if !RIWO_USING_BOOST_ASIO
	~exp_http_error_category() override = default;
#endif

	[[nodiscard]] const char *name() const noexcept override {
		return "riwo::experimental::http";
	}
	[[nodiscard]] std::string message(int code) const override
	{
		switch(static_cast<errc>(code))
		{
#define X_MACRO(e,v,d) case errc::e: return d;
		RIWO_EXP_HTTP_ERRC_TABLE
#undef X_MACRO
		default:
			return "Unknown experimental HTTP error";
		}
	}
};

#if RIWO_USING_BOOST_ASIO && defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif //BOOST & GNU

} //namespace

const error_category_t &error_category() noexcept
{
	static exp_http_error_category category;
	return category;
}

error_code make_error_code(errc value) noexcept
{
	return {static_cast<int>(value), error_category()};
}

} //namespace riwo::experimental::http
