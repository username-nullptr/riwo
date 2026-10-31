// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_EXP_HTTP_BODY_DETAIL_FILE_SOURCE_BACKEND_H
#define RIWO_EXP_HTTP_BODY_DETAIL_FILE_SOURCE_BACKEND_H

#include <riwo/experimental/http/body/file_source.h>

namespace riwo::experimental::http::detail
{

class RIWO_DECL_HIDDEN input_file_backend
{
	RIWO_DISABLE_COPY_MOVE(input_file_backend)

public:
	input_file_backend() = default;
	virtual ~input_file_backend() = default;

	[[nodiscard]] virtual size_t read(mutable_buffer, error_code&) = 0;
	virtual void rewind(error_code&) = 0;
	virtual void finish(error_code&) = 0;

	[[nodiscard]] virtual optional<uint64_t> size() const noexcept = 0;
	[[nodiscard]] virtual bool replayable() const noexcept = 0;
};

using input_file_backend_ptr = std::unique_ptr<input_file_backend>;

[[nodiscard]] RIWO_DECL_HIDDEN input_file_backend_ptr
make_input_file_backend(std::ifstream &&stream, optional<file_span> span);

[[nodiscard]] RIWO_DECL_HIDDEN input_file_backend_ptr
make_input_file_backend(std::fstream &&stream, optional<file_span> span);

[[nodiscard]] RIWO_DECL_HIDDEN input_file_backend_ptr
make_input_file_backend(std::ifstream *stream, optional<file_span> span);

[[nodiscard]] RIWO_DECL_HIDDEN input_file_backend_ptr
make_input_file_backend(std::fstream *stream, optional<file_span> span);

[[nodiscard]] RIWO_DECL_HIDDEN input_file_backend_ptr
make_input_file_backend(owned_c_file file, optional<file_span> span);

[[nodiscard]] RIWO_DECL_HIDDEN input_file_backend_ptr
make_input_file_backend(borrowed_c_file file, optional<file_span> span);

[[nodiscard]] RIWO_DECL_HIDDEN input_file_backend_ptr
make_input_file_backend(const std::filesystem::path &path, const optional<file_span> &span);

} //namespace riwo::experimental::http::detail


#endif //RIWO_EXP_HTTP_BODY_DETAIL_FILE_SOURCE_BACKEND_H
