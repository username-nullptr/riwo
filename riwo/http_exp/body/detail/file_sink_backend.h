// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#ifndef RIWO_HTTP_EXP_BODY_DETAIL_FILE_SINK_BACKEND_H
#define RIWO_HTTP_EXP_BODY_DETAIL_FILE_SINK_BACKEND_H

#include <riwo/http_exp/body/file_sink.h>

namespace riwo::http_exp::detail
{

class RIWO_DECL_HIDDEN output_file_backend
{
public:
	virtual ~output_file_backend() = default;
	virtual void prepare(error_code&) const noexcept = 0;
	[[nodiscard]] virtual bool random_access() const noexcept = 0;
	[[nodiscard]] virtual size_t write(const_buffer, error_code&) = 0;
	[[nodiscard]] virtual size_t write_at(uint64_t, const_buffer, error_code&) = 0;
	virtual void finish(error_code&) = 0;
};

using output_file_backend_ptr = std::unique_ptr<output_file_backend>;

[[nodiscard]] RIWO_DECL_HIDDEN output_file_backend_ptr
make_output_file_backend(const std::filesystem::path &path);

[[nodiscard]] RIWO_DECL_HIDDEN output_file_backend_ptr
make_output_file_backend(std::ofstream &&stream);

[[nodiscard]] RIWO_DECL_HIDDEN output_file_backend_ptr
make_output_file_backend(std::fstream &&stream);

[[nodiscard]] RIWO_DECL_HIDDEN output_file_backend_ptr
make_output_file_backend(std::ofstream *stream);

[[nodiscard]] RIWO_DECL_HIDDEN output_file_backend_ptr
make_output_file_backend(std::fstream *stream);

[[nodiscard]] RIWO_DECL_HIDDEN output_file_backend_ptr
make_output_file_backend(owned_c_file file);

[[nodiscard]] RIWO_DECL_HIDDEN output_file_backend_ptr
make_output_file_backend(borrowed_c_file file);

} //namespace riwo::http_exp::detail

#endif //RIWO_HTTP_EXP_BODY_DETAIL_FILE_SINK_BACKEND_H
