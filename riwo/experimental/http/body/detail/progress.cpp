// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "progress.h"

namespace riwo::experimental::http::detail { namespace
{

class RIWO_DECL_HIDDEN progress_body_reader_adapter final : public body_reader_adapter,
	public std::enable_shared_from_this<progress_body_reader_adapter>
{
public:
	progress_body_reader_adapter
	(body_reader_state_ptr source, uint64_t total, body_progress_handler handler) :
		m_source(std::move(source)), m_handler(std::move(handler)), m_total(total) {}

	[[nodiscard]] body_read_result read_some_sync
	(mutable_buffer buffer, error_code &error) noexcept override
	{
		auto result = m_source->read_some(buffer, error);
		if( not error and result.bytes )
			notify(result.bytes);
		return result;
	}

	void read_some(mutable_buffer buffer, read_handler_t handler) override
	{
		auto self = shared_from_this();
		m_source->read_some(buffer,
		[self = std::move(self), handler = std::move(handler)]
		(error_code error, body_read_result result) mutable
		{
			if( not error and result.bytes )
				self->notify(result.bytes);
			std::move(handler)(error, result);
		});
	}

	fields trailers_sync(error_code &error) noexcept override {
		return m_source->trailers(error);
	}
	void trailers(trailers_handler_t handler) override {
		m_source->trailers(std::move(handler));
	}
	void cancel() noexcept override {
		m_source->cancel();
	}

private:
	void notify(uint64_t bytes) noexcept
	{
		m_transferred += bytes;
		try {
			m_handler(m_transferred, m_total);
		}
		catch(...) {}
	}

	body_reader_state_ptr m_source {};
	body_progress_handler m_handler {};

	uint64_t m_total = 0;
	uint64_t m_transferred = 0;
};

} //namespace

body_reader_state_ptr make_progress_body_reader
(body_reader_state_ptr source, uint64_t total, body_progress_handler handler)
{
	if( not source or not handler )
		return source;

	return make_body_reader_state (
		std::make_shared<progress_body_reader_adapter>
			(std::move(source), total, std::move(handler)),
		true
	);
}

} //namespace riwo::experimental::http::detail
