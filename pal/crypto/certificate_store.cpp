#if !defined(_CRT_SECURE_NO_WARNINGS)
	#define _CRT_SECURE_NO_WARNINGS
#endif

#include <pal/crypto/certificate_store>
#include <pal/memory>
#include <pal/version>
#include <cstdio>
#include <filesystem>

namespace pal::crypto {

#if TODO_remove_if_unused
namespace {

using buffer_type = temporary_buffer<8192>;

std::unique_ptr<FILE, int(*)(FILE *)> open_file (const char *path) noexcept
{
	return {std::fopen(path, "r"), &std::fclose};
}

result<std::span<const std::byte>> load_file (const char *path, buffer_type &buffer, size_t size) noexcept
{
	if (auto f = open_file(path))
	{
		if (std::fread(buffer.get(), size, 1, f.get()) == 1)
		{
			return {static_cast<const std::byte *>(buffer.get()), size};
		}
	}
	return unexpected{std::error_code{errno, std::generic_category()}};
}

} // namespace

result<certificate_store> certificate_store::from_pkcs12_file (const char *path) noexcept
{
	std::error_code error;
	if (auto size = std::filesystem::file_size(path, error);  !error)
	{
		buffer_type buffer{size};
		return load_file(path, buffer, size).and_then(import_pkcs12);
	}
	return unexpected{error};
}
#endif

} // namespace pal::crypto
