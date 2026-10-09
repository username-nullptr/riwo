// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "error.h"

namespace riwo::websocket { namespace
{

#if RIWO_USING_BOOST_ASIO && defined(__GNUC__) && !defined(__clang__)
// Boost.System deliberately gives error_category a protected non-virtual
// destructor. This static final category is never deleted polymorphically, but
// GCC still diagnoses the supported inheritance pattern under
// -Wnon-virtual-dtor.
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif //BOOST & GNU

class RIWO_DECL_HIDDEN websocket_error_category final : public error_category_t
{
	RIWO_DISABLE_COPY_MOVE(websocket_error_category)

public:
	websocket_error_category() = default;
#if !RIWO_USING_BOOST_ASIO
	~websocket_error_category() override = default;
#endif //RIWO_USING_BOOST_ASIO

public:
	[[nodiscard]] const char *name() const noexcept override {
		return "riwo::websocket";
	}
	[[nodiscard]] std::string message(int code) const override
	{
		switch(static_cast<errc>(code))
		{
#define X_MACRO(e,v,d) case errc::e: return d;
		RIWO_WEBSOCKET_ERRC_TABLE
#undef X_MACRO
		default:
			break;
		}
		return "Unknown WebSocket error";
	}
};

#if RIWO_USING_BOOST_ASIO && defined(__GNUC__) && !defined(__clang__)
# pragma GCC diagnostic pop
#endif //BOOST & GNU

} //namespace

const error_category_t &error_category() noexcept
{
	static websocket_error_category category;
	return category;
}

error_code make_error_code(errc value) noexcept
{
	return {static_cast<int>(value), error_category()};
}

} //namespace riwo::websocket
