#include <pal/crypto/session.hpp>
#include <pal/memory.hpp>
#include <array>
#include <cstring>
#include <cstddef>
#include <span>

namespace pal::crypto
{

namespace
{

constexpr size_t handshake_buf_cap = 16 * 1024 + 256;
constexpr size_t record_cap = 16 * 1024 + 53;
constexpr size_t close_notify_cap = 256;

} // namespace

struct session::impl_type //{{{1
{
	connected_channel channel;
	std::array<std::byte, record_cap> wire{};
	size_t first = 0;
	size_t last = 0;
	bool pending_plain = false;
	enum transport transport;

	explicit impl_type (connected_channel channel, enum transport transport) noexcept
		: channel{std::move(channel)}
		, transport{transport}
	{
	}

	/// True when there are unconsumed ciphertext bytes buffered locally.
	[[nodiscard]] bool has_data () const noexcept
	{
		return first < last;
	}

	/// View of the unconsumed ciphertext in the buffer.
	[[nodiscard]] std::span<const std::byte> ciphertext () const noexcept
	{
		return std::span{wire}.subspan(first, last - first);
	}

	/// Space available for the next transport read.
	std::span<std::byte> free_space () noexcept
	{
		return std::span{wire}.subspan(last);
	}

	/// Mark \a n bytes at the front as consumed by the TLS engine.
	void consume (size_t n) noexcept
	{
		first += n;
	}

	/// Mark \a n bytes just written into free_space() as valid ciphertext.
	void accept (size_t n) noexcept
	{
		last += n;
	}

	/// Shift any remaining data to the front, resetting first to zero.
	void compact () noexcept
	{
		if (first != 0)
		{
			std::memmove(wire.data(), wire.data() + first, last - first);
			last -= first;
			first = 0;
		}
	}

	/// Write \a data to \a dev.
	///
	/// For stream: loops until all bytes are sent. For datagram: sends each record separately.
	result<size_t> write (__session::device &dev, std::span<const std::byte> data) const noexcept
	{
		size_t sent_total = 0;
		while (sent_total < data.size())
		{
			const auto chunk = (transport == transport::datagram)
				? data.subspan(sent_total, dtls_record_size(data.subspan(sent_total)))
				: data.subspan(sent_total);
			auto sent = dev.send(chunk);
			if (!sent)
			{
				return pal::unexpected(sent.error());
			}
			sent_total += *sent;
		}
		return sent_total;
	}

	/// Read from \a dev into free_space(), returning bytes received.
	[[nodiscard]] result<size_t> read (__session::device &dev) noexcept
	{
		auto received = dev.receive(free_space());
		if (!received)
		{
			return pal::unexpected(received.error());
		}
		accept(*received);
		return *received;
	}
};

// session: pimpl boilerplate {{{1

session::session () noexcept = default;

session::session (impl_ptr impl) noexcept
	: impl_{std::move(impl)}
{
}

session::session (session &&) noexcept = default;
session &session::operator= (session &&) noexcept = default;
session::~session () noexcept = default;

session::operator bool () const noexcept
{
	return impl_ != nullptr;
}

result<session> session::from (connected_channel &&channel, transport transport) noexcept //{{{1
{
	// clang-format off
	return pal::make_unique<impl_type>(std::move(channel), transport).transform([] (auto p)
	{
		return session{std::move(p)};
	});
	// clang-format on
}

result<session>
session::run_handshake_impl (__session::device &dev, handshake_channel &handshake, transport transport) noexcept //{{{1
{
	auto session = from(connected_channel{}, transport);
	if (!session)
	{
		return pal::unexpected(session.error());
	}

	std::array<std::byte, handshake_buf_cap> out_buf{};
	for (auto &impl = *session->impl_;;)
	{
		auto step = handshake.step(impl.ciphertext(), std::span{out_buf});
		if (!step)
		{
			return pal::unexpected(step.error());
		}

		impl.consume(step->consumed);

		if (auto write = impl.write(dev, std::span{out_buf}.first(step->produced)); !write)
		{
			return pal::unexpected(write.error());
		}

		if (step->connected)
		{
			impl.compact();
			impl.channel = std::move(*step->connected);
			return std::move(*session);
		}

		if (step->want_output)
		{
			continue;
		}

		impl.compact();

		if (auto read = impl.read(dev); !read)
		{
			return pal::unexpected(read.error());
		}
	}
}

result<size_t> session::send_impl (__session::device &dev, std::span<const std::byte> plain) noexcept //{{{1
{
	auto remaining = plain;
	const size_t total = remaining.size();

	while (!remaining.empty())
	{
		auto encrypt = impl_->channel.encrypt(remaining, impl_->wire);
		if (!encrypt)
		{
			return pal::unexpected(encrypt.error());
		}

		remaining = remaining.subspan(encrypt->consumed);

		if (auto write = impl_->write(dev, std::span{impl_->wire}.first(encrypt->produced)); !write)
		{
			return pal::unexpected(write.error());
		}
	}

	return total;
}

result<size_t> session::receive_impl (__session::device &dev, std::span<std::byte> out) noexcept //{{{1
{
	for (;;)
	{
		if (impl_->pending_plain)
		{
			auto decrypt = impl_->channel.decrypt(out);
			if (!decrypt)
			{
				return pal::unexpected(decrypt.error());
			}

			impl_->pending_plain = decrypt->want_output;

			if (decrypt->produced > 0)
			{
				return decrypt->produced;
			}
		}

		if (impl_->has_data())
		{
			auto decrypt = impl_->channel.decrypt(impl_->ciphertext(), out);
			if (!decrypt)
			{
				return pal::unexpected(decrypt.error());
			}

			impl_->consume(decrypt->consumed);
			impl_->pending_plain = decrypt->want_output;

			if (decrypt->peer_closed)
			{
				return pal::unexpected(make_error_code(secure_channel_errc::closed));
			}

			if (decrypt->produced > 0)
			{
				return decrypt->produced;
			}
			continue;
		}

		impl_->compact();

		if (auto read = impl_->read(dev); !read)
		{
			return pal::unexpected(read.error());
		}
	}
}

result<void> session::close_notify_impl (__session::device &dev) noexcept //{{{1
{
	std::array<std::byte, close_notify_cap> out{};
	for (;;)
	{
		auto close_notify = impl_->channel.close_notify(out);
		if (!close_notify)
		{
			return pal::unexpected(close_notify.error());
		}

		if (auto write = impl_->write(dev, std::span{out}.first(close_notify->produced)); !write)
		{
			return pal::unexpected(write.error());
		}

		if (!close_notify->want_output)
		{
			return {};
		}
	}
}

// session::peer_certificate, selected_protocol {{{1

result<certificate> session::peer_certificate () const noexcept
{
	return impl_->channel.peer_certificate();
}

std::string_view session::selected_protocol () const noexcept
{
	return impl_->channel.selected_protocol();
}

size_t session::max_message_size () const noexcept
{
	return impl_->channel.max_message_size();
}

// }}}1

} // namespace pal::crypto
