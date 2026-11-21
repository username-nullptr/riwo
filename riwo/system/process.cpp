// SPDX-FileCopyrightText: 2025-2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "riwo/system/process.h"
#include <riwo/system/user.h>

namespace riwo::system::detail { namespace
{

class RIWO_DECL_HIDDEN capture_operation
{
	using clock_t = std::chrono::steady_clock;
	using read_channel_t = process::read_channel;

	struct channel
	{
		read_channel_t stream {};
		size_t limit = 0;

		std::string &output;
		bool &truncated;

		std::array<char,16 * 1024> buffer {};
	};

public:
	explicit capture_operation(const process_capture_options &options) :
		m_channels {{
			{
				read_channel_t::std_output, options.max_stdout_bytes,
				m_result.stdout_data, m_result.stdout_truncated
			},
			{
				read_channel_t::std_error, options.max_stderr_bytes,
				m_result.stderr_data, m_result.stderr_truncated
			},
		}}
	{
		m_child.set_work_path(options.work_path);
		if( options.timeout > clock_t::duration::zero() )
		{
			const auto now = clock_t::now();
			m_deadline = now + std::min(options.timeout, clock_t::time_point::max() - now);
		}
	}

	~capture_operation()
	{
		if( m_child.joinable() )
		{
			m_child.kill();
			RIWO_UNUSED(m_child.join());
		}
	}

	[[nodiscard]] sys_expected<process_capture_result> run
	(const process::path_t &cmd, const std::vector<process::path_t> &args)
	{
		if( auto result = m_child.start_program(cmd, args); not result )
			return sys_unexpected(result.error());

		if( auto result = m_child.close_stdin(); not result )
			return sys_unexpected(result.error());

		if( m_deadline )
		{
			m_timer.expires_at(*m_deadline);
			m_timer.async_wait([this](const auto &error)
			{
				if( not error and m_open_streams != 0 )
					stop(make_system_error_code(std::errc::timed_out));
			});
		}
		for(auto &stream : m_channels)
			read(stream);

		m_context.run();
		if( m_error )
			return sys_unexpected(m_error);

		std::chrono::nanoseconds remaining {};
		if( m_deadline )
		{
			const auto duration = *m_deadline - clock_t::now();
			remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);

			if( remaining <= std::chrono::nanoseconds::zero() )
				remaining = -std::chrono::nanoseconds(1);
		}
		auto status = m_child.join(remaining);
		if( not status )
			return sys_unexpected(status.error());

		m_result.exit_code = *status;
		return std::move(m_result);
	}

private:
	void stop(error_code error)
	{
		if( m_error )
			return ;

		m_error = error;
		m_child.kill();

		m_child.cancel(false);
		m_timer.cancel();
	}

	void read(channel &stream)
	{
		m_child.async_read(stream.stream, asio::buffer(stream.buffer),
		[this, &stream](error_code error, size_t size)
		{
			if( m_error )
				return ;

			const auto keep = std::min(size, stream.limit - stream.output.size());
			stream.output.append(stream.buffer.data(), keep);

			stream.truncated |= keep < size;
			m_child.normalize_read_error(error);

			if( not error )
				read(stream);

			else if( error == make_error_code(asio::error::eof) )
			{
				if( --m_open_streams == 0 )
					m_timer.cancel();
			}
			else
				stop(error);
		});
	}

	process_capture_result m_result {};
	std::array<channel,2> m_channels;

	optional<clock_t::time_point> m_deadline {};
	size_t m_open_streams = m_channels.size();
	error_code m_error {};

	io_context_t m_context {};
	process m_child {m_context.get_executor()};
	asio::steady_timer m_timer {m_context};
};

} //namespace

sys_expected<process_capture_result> process::capture
(const path_t &cmd, const std::vector<path_t> &args, const process_capture_options &options)
{
	const auto has_null = [](const path_t &path) {
		return path.native().find(path_t::value_type{}) != path_t::string_type::npos;
	};
	if( cmd.empty() or has_null(cmd) or has_null(options.work_path) or
		std::ranges::any_of(args, has_null) or options.timeout < std::chrono::steady_clock::duration::zero() )
		return sys_unexpected(make_system_error_code(std::errc::invalid_argument));
	try {
		return capture_operation(options).run(cmd, args);
	}
	catch(...) {
		return sys_unexpected(exception_error(std::current_exception()));
	}
}

sys_expected<uint64_t> process::set_single(std::string_view key)
{
	return home_directory().and_then([&](const auto &path) mutable {
		return set_single(path, key);
	});
}

} //namespace riwo::system::detail
