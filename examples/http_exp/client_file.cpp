// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include "common.h"
#include <iostream>

namespace http = riwo::http_exp;

int main(int argc, const char *argv[])
try
{
	if( argc < 2 )
	{
		std::cerr << "Usage: client_file <upload-file> [download-file] "
			"[base-url] [auto|http1|http2]\n";
		return 2;
	}
	const auto upload_file = std::filesystem::absolute(argv[1]);

	const auto download_file = std::filesystem::absolute(
		argc > 2 ? argv[2] : "riwo-http-exp-downloaded.bin"
	);
	const std::string base = argc > 3 ?
		argv[3] : "http://127.0.0.1:8083";

	http::client_config config;
	config.protocols.requirement = http::example::parse_protocol (
		argc > 4 ? argv[4] : "auto"
	);
	http::client client(config);
	uint64_t upload_total = 0;
	uint64_t upload_done = 0;

	http::request upload_request(http::method::post, base + "/upload");
	upload_request.set_file(upload_file);

	upload_request.on_upload_progress([&](uint64_t transferred, uint64_t total)
	{
		upload_done = transferred;
		upload_total = total;
	});
	auto uploaded = client.fetch(std::move(upload_request));

	std::cout << "Upload ["
		<< http::example::protocol_text(uploaded.selected_protocol())
		<< "] -> " << uploaded.status().code() << ", progress "
		<< upload_done << '/' << upload_total << '\n'
		<< http::example::body_text(uploaded.buffered_body());

	uint64_t download_total = 0;
	uint64_t download_done = 0;
	http::request download_request(http::method::get, base + "/download");

	download_request.on_download_progress([&](uint64_t transferred, uint64_t total)
	{
		download_done = transferred;
		download_total = total;
	});
	auto downloaded = client.send(std::move(download_request));
	const auto etag = downloaded.headers().find_first("ETag");
	const auto saved = downloaded.save_file(download_file);

	std::cout << "Download ["
		<< http::example::protocol_text(downloaded.selected_protocol())
		<< "] -> " << downloaded.status().code() << ", saved " << saved
		<< " bytes, progress " << download_done << '/' << download_total << '\n'
		<< "Destination: " << download_file << '\n';

	if( etag )
	{
		http::request conditional(http::method::get, base + "/download");
		conditional.headers().add("If-None-Match", *etag);
		auto unchanged = client.fetch(std::move(conditional));

		std::cout << "Conditional GET ["
			<< http::example::protocol_text(unchanged.selected_protocol())
			<< "] -> "
			<< unchanged.status().code() << " (ETag " << *etag << ")\n";
	}
	if( std::filesystem::file_size(download_file) != 0 )
	{
		http::request range(http::method::get, base + "/download");
		range.headers().add("Range", "bytes=0-15");

		auto partial = client.send(std::move(range));
		const auto body = partial.read_all();

		std::cout << "Range GET ["
			<< http::example::protocol_text(partial.selected_protocol())
			<< "] -> " << partial.status().code() << ", "
			<< body.size() << " bytes\n";
	}
	return 0;
}
catch(const std::exception &exception)
{
	std::cerr << "HTTP_EXP file client failed: " << exception.what() << '\n';
	return 1;
}
