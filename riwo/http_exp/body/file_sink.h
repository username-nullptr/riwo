// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_FILE_SINK_H
#define RIWO_HTTP_EXP_BODY_FILE_SINK_H

#include <riwo/http_exp/body/file_handle.h>

namespace riwo::http_exp { namespace detail {
class output_file_backend;
} //namespace detail

class RIWO_HTTP_EXP_API file_sink
{
	RIWO_DISABLE_COPY(file_sink)

public:
	explicit file_sink(std::ofstream &&stream);
	explicit file_sink(std::fstream &&stream);

	explicit file_sink(const borrowed_file_ref<std::ofstream> &stream);
	explicit file_sink(const borrowed_file_ref<std::fstream> &stream);

	explicit file_sink(owned_c_file file);
	explicit file_sink(borrowed_c_file file);

	explicit file_sink(const std::filesystem::path &path);
	~file_sink();

	file_sink(file_sink &&) noexcept;
	file_sink &operator=(file_sink &&) noexcept;

public:
	void prepare(error_code &error) const noexcept;
	[[nodiscard]] bool random_access() const noexcept;

	void write(const const_buffer& buffer,
		asio::any_completion_handler<void(error_code,size_t)> handler
	);
	[[nodiscard]] size_t write(const const_buffer &buffer, error_code &error) noexcept;

	void write_at(uint64_t offset, const const_buffer &buffer,
		asio::any_completion_handler<void(error_code,size_t)> handler
	);
	[[nodiscard]] size_t write_at (
		uint64_t offset, const const_buffer &buffer, error_code &error
	) noexcept;

	void finish(error_code &error) noexcept;
	void finish(asio::any_completion_handler<void(error_code)> handler);

	void cancel() noexcept;

private:
	std::unique_ptr<detail::output_file_backend> m_backend {};
	bool m_cancelled = false;
	bool m_finished = false;
};

namespace concepts
{

template <typename Sink>
concept body_sink_model = requires(
	Sink &sink, const_buffer buffer, error_code &error,
	asio::any_completion_handler<void(error_code,size_t)> write_handler,
	asio::any_completion_handler<void(error_code)> finish_handler)
{
	{ sink.write(buffer, error) } -> std::same_as<size_t>;
	{ sink.write(buffer, std::move(write_handler)) } -> std::same_as<void>;

	{ sink.finish(error) } -> std::same_as<void>;
	{ sink.finish(std::move(finish_handler)) } -> std::same_as<void>;

	{ sink.cancel() } noexcept -> std::same_as<void>;
};

template <typename Sink>
concept body_sink_model_p = body_sink_model<std::remove_cvref_t<Sink>>;

} //namespace concepts

static_assert(concepts::body_sink_model<file_sink>);

} //namespace riwo::http_exp


#endif //RIWO_HTTP_EXP_BODY_FILE_SINK_H
