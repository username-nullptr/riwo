// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_DETAIL_SOURCE_H
#define RIWO_HTTP_EXP_BODY_DETAIL_SOURCE_H

#include <riwo/http_exp/error.h>

namespace riwo::http_exp::detail
{

class RIWO_HTTP_EXP_API body_source
{
	RIWO_DISABLE_COPY_MOVE(body_source)

public:
	using read_handler_t = asio::any_completion_handler<void(error_code,size_t)>;
	using rewind_handler_t = asio::any_completion_handler<void(error_code)>;

	body_source() = default;
	virtual ~body_source();
	[[nodiscard]] virtual optional<uint64_t> size() const noexcept = 0;
	[[nodiscard]] virtual bool replayable() const noexcept = 0;
	[[nodiscard]] virtual size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept = 0;
	virtual void read_some(mutable_buffer buffer, read_handler_t handler) = 0;
	virtual void rewind(rewind_handler_t handler) = 0;
	virtual void finish_sync(error_code &error) noexcept = 0;
	virtual void finish(rewind_handler_t handler) = 0;
	virtual void cancel() noexcept = 0;
};

template <body_source_model Source>
class body_source_holder final : public body_source
{
public:
	template <typename Source0>
	explicit body_source_holder(Source0 &&source) :
		m_source(std::forward<Source0>(source))
	{
	}

	[[nodiscard]] optional<uint64_t> size() const noexcept override {
		return m_source.size();
	}

	[[nodiscard]] bool replayable() const noexcept override {
		return m_source.replayable();
	}

	[[nodiscard]] size_t read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override
	{
		if constexpr( requires(Source &source) {
			{ source.read_some(buffer, error) } -> std::same_as<size_t>;
		})
			return m_source.read_some(buffer, error);
		else
		{
			error = make_error_code(errc::operation_not_supported);
			return 0;
		}
	}

	void read_some(mutable_buffer buffer, read_handler_t handler) override {
		m_source.read_some(buffer, std::move(handler));
	}

	void rewind(rewind_handler_t handler) override {
		m_source.rewind(std::move(handler));
	}

	void finish_sync(error_code &error) noexcept override
	{
		if constexpr( finalizable_body_source_model<Source> )
		{
			try { m_source.finish(error); }
			catch(...) { error = exception_error(std::current_exception()); }
		}
		else error.clear();
	}

	void finish(rewind_handler_t handler) override
	{
		if constexpr( finalizable_body_source_model<Source> )
			m_source.finish(std::move(handler));
		else std::move(handler)(error_code{});
	}

	void cancel() noexcept override {
		m_source.cancel();
	}

private:
	Source m_source;
};

template <typename Source>
requires body_source_model<std::remove_cvref_t<Source>>
[[nodiscard]] std::shared_ptr<body_source> make_body_source(Source &&source)
{
	using source_t = std::remove_cvref_t<Source>;
	return std::make_shared<body_source_holder<source_t>>(
		std::forward<Source>(source));
}

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_BODY_DETAIL_SOURCE_H
