// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "error.h"

namespace riwo::http_exp { namespace
{

#if RIWO_USING_BOOST_ASIO && defined(__GNUC__) && !defined(__clang__)
// Boost.System deliberately gives error_category a protected non-virtual
// destructor. This static final category is never deleted polymorphically, but
// GCC still diagnoses the supported inheritance pattern under
// -Wnon-virtual-dtor.
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif //BOOST & GNU

class RIWO_DECL_HIDDEN http_exp_error_category final : public error_category_t
{
	RIWO_DISABLE_COPY_MOVE(http_exp_error_category)

public:
	http_exp_error_category() = default;
#if !RIWO_USING_BOOST_ASIO
	~http_exp_error_category() override = default;
#endif

	[[nodiscard]] const char *name() const noexcept override {
		return "riwo::http_exp";
	}
	[[nodiscard]] std::string message(int code) const override
	{
		switch(static_cast<errc>(code))
		{
#define X_MACRO(e,v,d) case errc::e: return d;
		RIWO_HTTP_EXP_ERRC_TABLE
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
	static http_exp_error_category category;
	return category;
}

error_code make_error_code(errc value) noexcept
{
	return {static_cast<int>(value), error_category()};
}

} //namespace riwo::http_exp
