// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "encoder.h"
#include <riwo/experimental/http/protocol/compression.h>

namespace riwo::experimental::http::detail { namespace
{

class RIWO_DECL_HIDDEN gzip_body_source final : public body_source,
	public std::enable_shared_from_this<gzip_body_source>
{
public:
	explicit gzip_body_source(body_source_ptr value) :
		source(std::move(value)) {}

	[[nodiscard]] optional<uint64_t> size() const noexcept override {
		return {};
	}
	[[nodiscard]] bool replayable() const noexcept override {
		return source->replayable();
	}

	[[nodiscard]] size_t drain(const mutable_buffer &target) noexcept
	{
		const auto size = std::min(target.size(), pending.size() - pending_offset);
		if( size != 0 )
			std::memcpy(target.data(), pending.data() + pending_offset, size);

		pending_offset += size;
		if( pending_offset == pending.size() )
		{
			pending.clear();
			pending_offset = 0;
		}
		return size;
	}

	[[nodiscard]] size_t read_some_sync(mutable_buffer target, error_code &error) noexcept override
	{
		error.clear();
		if( target.size() == 0 )
			return 0;

		if( pending_offset != pending.size() )
			return drain(target);

		if( ended )
		{
			error = asio::error::make_error_code(asio::error::eof);
			return 0;
		}
		for(;;)
		{
			const auto read = source->read_some_sync(buffer(input), error);
			if( error == asio::error::make_error_code(asio::error::eof) )
			{
				error.clear();
				auto encoded = encoder.append({}, true);

				if( not encoded )
				{
					error = encoded.error();
					return 0;
				}
				pending = std::move(*encoded);
				ended = true;

				if( pending.empty() )
				{
					error = asio::error::make_error_code(asio::error::eof);
					return 0;
				}
				return drain(target);
			}
			if( error )
				return 0;

			if( read == 0 )
			{
				error = make_error_code(errc::body_length_mismatch);
				return 0;
			}
			auto encoded = encoder.append(std::string_view (
				reinterpret_cast<const char*>(input.data()), read)
			);
			if( not encoded )
			{
				error = encoded.error();
				return 0;
			}
			pending = std::move(*encoded);

			if( not pending.empty() )
				return drain(target);
		}
	}

	void read_some(mutable_buffer target, read_handler_t handler) override
	{
		if( target.size() == 0 )
		{
			std::move(handler)(error_code{}, 0);
			return ;
		}
		if( pending_offset != pending.size() )
		{
			std::move(handler)(error_code{}, drain(target));
			return ;
		}
		if( ended )
		{
			std::move(handler)(asio::error::make_error_code(asio::error::eof), 0);
			return ;
		}
		auto self = shared_from_this();

		source->read_some(buffer(input),
		[self, target, handler = std::move(handler)](error_code error, size_t read) mutable
		{
			const bool eof = error == asio::error::make_error_code(asio::error::eof);
			if( error and not eof )
			{
				std::move(handler)(error, 0);
				return ;
			}
			if( not eof and read == 0 )
			{
				std::move(handler)(make_error_code(errc::body_length_mismatch), 0);
				return ;
			}
			auto encoded = self->encoder.append(eof ? std::string_view{} :
				std::string_view(reinterpret_cast<const char*>(self->input.data()), read), eof
			);
			if( not encoded )
			{
				std::move(handler)(encoded.error(), 0);
				return ;
			}
			self->pending = std::move(*encoded);
			if( eof )
				self->ended = true;

			if( not self->pending.empty() )
			{
				std::move(handler)(error_code{}, self->drain(target));
				return ;
			}
			if( self->ended )
			{
				std::move(handler) (
					asio::error::make_error_code(asio::error::eof), 0
				);
				return ;
			}
			self->read_some(target, std::move(handler));
		});
	}

	void rewind(rewind_handler_t handler) override
	{
		auto self = shared_from_this();
		source->rewind(
		[self, handler = std::move(handler)](error_code error) mutable
		{
			if( not error )
			{
				if( auto reset = self->encoder.reset(); not reset )
					error = reset.error();
				else
				{
					self->pending.clear();
					self->pending_offset = 0;
					self->ended = false;
				}
			}
			std::move(handler)(error);
		});
	}

	void finish_sync(error_code &error) noexcept override {
		source->finish_sync(error);
	}
	void finish(rewind_handler_t handler) override {
		source->finish(std::move(handler));
	}
	void cancel() noexcept override { source->cancel(); }

private:
	body_source_ptr source {};
	gzip_encoder encoder {};

	std::array<std::byte,16384> input {};
	std::string pending {};

	size_t pending_offset = 0;
	bool ended = false;
};

} //namespace

body_source_ptr make_gzip_body_source(body_source_ptr source)
{
	return std::make_shared<gzip_body_source>(std::move(source));
}

} //namespace riwo::experimental::http::detail
