#include <pal/crypto/__certificate.hpp>
#include <pal/memory.hpp>
#include <filesystem>
#include <fstream>

namespace pal::crypto
{

bool certificate_store::empty () const noexcept
{
	return !impl_ || !impl_->head;
}

certificate_store::const_iterator certificate_store::begin () const noexcept
{
	if (impl_)
	{
		return const_iterator{impl_->head};
	}
	return {};
}

result<certificate_store> certificate_store::from_pkcs12 (const std::filesystem::path &path) noexcept
{
	std::error_code ec;
	const auto size = std::filesystem::file_size(path, ec);
	if (ec)
	{
		return pal::unexpected{ec};
	}

	static constexpr size_t stack_size = 4096;
	temporary_buffer<stack_size> buf{std::nothrow, size};
	if (!buf)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	std::ifstream file{path, std::ios::binary};
	if (!file)
	{
		return make_unexpected(std::errc::no_such_file_or_directory);
	}

	if (!file.read(reinterpret_cast<char *>(buf.get().data()), static_cast<std::streamsize>(size)))
	{
		return make_unexpected(std::errc::io_error);
	}

	return import_pkcs12(std::as_bytes(buf.get()));
}

} // namespace pal::crypto
