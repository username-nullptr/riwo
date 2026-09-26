// SPDX-FileCopyrightText: 2026 Xiaoqiang <username_nullptr@163.com>
// SPDX-License-Identifier: MIT

#include <riwo/http/client.h>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, const char *argv[])
{
	if(argc < 2)
	{
		std::cerr << "Usage: client_file <upload-file> [download-file] [base-url]\n";
		return 2;
	}
	const auto upload_file = std::filesystem::absolute(argv[1]);
	const auto download_file = std::filesystem::absolute (
		argc > 2 ? argv[2] : "riwo-downloaded.bin"
	);
	const std::string base_url = argc > 3 ?
		argv[3] : "http://127.0.0.1:8083";

	try {
		riwo::http::client client;

		auto upload = client.upload_file(base_url + "/upload", upload_file);
		std::cout << upload->reply()->read<std::string>();

		client.download_file(base_url + "/download", download_file);
		std::cout << "Downloaded " << std::filesystem::file_size(download_file)
			<< " bytes to " << download_file << '\n';
	}
	catch(const std::exception &exception)
	{
		std::cerr << "File-transfer example failed: " << exception.what() << '\n';
		return 1;
	}
	return 0;
}
