#include <pal/crypto/__certificate.hpp>
#include <pal/memory.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <vector>

namespace pal::crypto
{

namespace
{

namespace fs = std::filesystem;

bool is_pkcs12_extension (const fs::path &p) noexcept
{
	// clang-format off

	using value_type = fs::path::value_type;
	static constexpr std::array<value_type, 4> pfx{'.', 'p', 'f', 'x'};
	static constexpr std::array<value_type, 4> p12{'.', 'p', '1', '2'};

	const auto &native = p.native();
	if (native.size() < pfx.size())
	{
		return false;
	}

	std::array<value_type, 4> tail{};
	std::ranges::transform(native.end() - tail.size(), native.end(), tail.begin(), [] (value_type c)
	{
		return (c >= 'A' && c <= 'Z') ? static_cast<value_type>(c + ('a' - 'A')) : c;
	});

	return tail == pfx || tail == p12;

	// clang-format on
}

result<void> read_file (const fs::path &path, std::span<std::byte> buf) noexcept
{
	std::ifstream file{path, std::ios::binary};
	if (!file)
	{
		return make_unexpected(std::errc::no_such_file_or_directory);
	}
	if (!file.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(buf.size())))
	{
		return make_unexpected(std::errc::io_error);
	}
	return {};
}

result<std::vector<fs::path>> collect_pkcs12_paths (const fs::path &dir)
{
	std::error_code ec;
	const fs::directory_iterator it{dir, ec};
	if (ec)
	{
		return pal::unexpected{ec};
	}

	std::vector<fs::path> paths;
	for (const auto &entry: it)
	{
		if (entry.is_regular_file(ec) && is_pkcs12_extension(entry.path()))
		{
			paths.push_back(entry.path());
		}
	}

	std::ranges::sort(paths, [] (const fs::path &a, const fs::path &b) { return a.filename() < b.filename(); });
	return paths;
}

} // namespace

result<certificate::impl_ptr> certificate_store::load_one (const fs::path &path) noexcept
{
	std::error_code ec;
	const auto size = fs::file_size(path, ec);
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

	auto ok = read_file(path, buf.get());
	if (!ok)
	{
		return pal::unexpected{ok.error()};
	}

	return import_pkcs12_chain(std::as_bytes(buf.get()));
}

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

result<certificate_store> certificate_store::from_pkcs12 (const fs::path &path) noexcept
{
	// clang-format off

	return load_one(path).and_then([] (certificate::impl_ptr head) -> result<certificate_store>
	{
		return pal::make_shared<certificate_store::impl_type>(std::move(head)).transform(to_api);
	});

	// clang-format on
}

result<certificate_store> certificate_store::from_cert_dir (const fs::path &dir) noexcept
{
	std::vector<fs::path> paths;
	try
	{
		auto collected = collect_pkcs12_paths(dir);
		if (!collected)
		{
			return pal::unexpected{collected.error()};
		}
		paths = std::move(*collected);
	}
	catch (const fs::filesystem_error &e)
	{
		return pal::unexpected{e.code()};
	}
	catch (...)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	certificate::impl_ptr head;
	certificate::impl_ptr tail;
	for (const auto &path: paths)
	{
		auto chain = load_one(path);
		if (!chain)
		{
			if (chain.error() == std::errc::not_enough_memory)
			{
				return pal::unexpected{chain.error()};
			}
			continue;
		}

		if (!head)
		{
			head = *chain;
		}
		else
		{
			tail->next = *chain;
		}

		tail = *chain;
		while (tail->next)
		{
			tail = tail->next;
		}
	}

	return pal::make_shared<certificate_store::impl_type>(std::move(head)).transform(to_api);
}

} // namespace pal::crypto
