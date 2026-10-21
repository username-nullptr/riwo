// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_DETAIL_DECODER_H
#define RIWO_HTTP_EXP_BODY_DETAIL_DECODER_H

#include <riwo/http_exp/body/reader.h>

namespace riwo::http_exp::detail
{

class RIWO_HTTP_EXP_API decoding_body_reader_adapter final :
	public body_reader_adapter,
	public std::enable_shared_from_this<decoding_body_reader_adapter>
{
	RIWO_DISABLE_COPY_MOVE(decoding_body_reader_adapter)

public:
	decoding_body_reader_adapter(
		std::shared_ptr<body_reader_state> source, std::string encoding);
	~decoding_body_reader_adapter() override;

	[[nodiscard]] body_read_result read_some_sync(mutable_buffer buffer,
		error_code &error) noexcept override;
	void read_some(mutable_buffer buffer, read_handler_t handler) override;
	[[nodiscard]] fields trailers_sync(error_code &error) noexcept override;
	void trailers(trailers_handler_t handler) override;
	void cancel() noexcept override;

private:
	class async_operation;
	friend class async_operation;

	[[nodiscard]] body_read_result decode_sync(mutable_buffer buffer,
		error_code &error);
	[[nodiscard]] bool fill_input_sync(error_code &error);
	[[nodiscard]] bool drain_source_sync(error_code &error);
	[[nodiscard]] bool inflate_step(mutable_buffer buffer, size_t &produced,
		bool &ended, error_code &error);

	struct impl;
	std::shared_ptr<impl> m_impl;
};

[[nodiscard]] RIWO_HTTP_EXP_API std::shared_ptr<body_reader_state>
make_decoding_body_reader(std::shared_ptr<body_reader_state> source,
	std::string encoding);

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_BODY_DETAIL_DECODER_H
