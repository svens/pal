#pragma once

/**
 * \file pal/async/__async.hpp
 * Cross-TU internal seam for pal/async (internal)
 */

#include <pal/require.hpp>
#include <array>
#include <cstddef>
#include <cstring>
#include <new>
#include <system_error>
#include <type_traits>
#include <utility>

namespace pal::async::__async
{

/// Inline closure storage budget for a \ref completion, in bytes: a cache line (64) minus one alignment quantum --
/// the gap the jump from \c thunk_ (pointer-sized) to a \c max_align_t-aligned \c storage_ costs, regardless of
/// how few of those bytes \c thunk_ itself occupies -- so \c completion<Carrier> fits one cache line exactly (see
/// static_assert below). Named separately from any per-op scratch budget (see pal/async/task.hpp) so "closure too
/// big" and "op scratch grew" are distinct compile errors.
inline constexpr size_t closure_capacity = 64 - alignof(std::max_align_t);

template <typename F, typename Signature>
struct signature_t;

template <typename F, typename R, typename... Args>
struct signature_t<F, R(Args...) noexcept>: std::bool_constant<std::is_nothrow_invocable_r_v<R, F, Args...>>
{
};

template <typename F, typename R, typename... Args>
struct signature_t<F, R(Args...)>: std::bool_constant<std::is_invocable_r_v<R, F, Args...>>
{
};

// clang-format off

/// A closure bindable to a \ref completion: inline-storable (trivially copyable/destructible, default
/// constructible, within the closure budget and alignment) and callable as \a Signature. Checked at the public
/// shell, never re-checked internally. Spell \a Signature literally at the app boundary so the diagnostic names
/// it verbatim; internal seams pass \c Op::signature.
template <typename F, typename Signature>
concept handler = requires
{
	requires std::is_trivially_copyable_v<F>;
	requires std::is_trivially_destructible_v<F>;
	requires std::is_default_constructible_v<F>;
	requires sizeof(F) <= closure_capacity;
	requires alignof(F) <= alignof(std::max_align_t);
	requires signature_t<F, Signature>::value;
};

// clang-format on

/// Soft-core completion dispatch unit: one erased "completion thunk" function pointer plus inline closure storage,
/// embedded both in \c task (single-shot ops, timers) and in per-op socket-state slots (multishot recv/accept,
/// connect).  \a Carrier is whatever the completion is embedded in; it is passed to \c Op::dispatch so it can
/// materialize typed arguments (e.g. an endpoint written into scratch state) before invoking the closure.
///
/// \c Op is a type with:
/// \code
/// using signature = void(...) noexcept; // the handler's required call signature
/// static void dispatch (Carrier &carrier, F &f, std::error_code ec, size_t n) noexcept;
/// \endcode
///
/// Non-copyable, non-movable: a completion is always accessed in place, embedded in its carrier.
template <typename Carrier>
class completion
{
public:

	completion () noexcept = default;
	~completion () noexcept = default;

	completion (const completion &) = delete;
	completion &operator= (const completion &) = delete;
	completion (completion &&) = delete;
	completion &operator= (completion &&) = delete;

	[[nodiscard]] bool armed () const noexcept
	{
		return thunk_ != nullptr;
	}

	/// Bind single-shot closure \a f for \a Op. \a f is memcpy'd into inline storage now and copied back out (and
	/// the storage/thunk freed) at the start of the matching \ref complete, before \c Op::dispatch invokes it -- so
	/// the completion is rebindable again from inside its own callback.
	template <typename Op>
	void bind (handler<typename Op::signature> auto f) noexcept
	{
		pal_require(thunk_ == nullptr, "completion rebind while bound");
		std::memcpy(storage_.data(), &f, sizeof(f));
		thunk_ = &single_shot_thunk<Op, decltype(f)>;
	}

	/// Arm multishot closure \a f for \a Op. \a f is placement-new'd into inline storage and invoked in place,
	/// through a laundered pointer, on every \ref complete until \ref stop clears it.
	template <typename Op>
	void arm (handler<typename Op::signature> auto f) noexcept
	{
		pal_require(thunk_ == nullptr, "completion rebind while armed");
		::new (static_cast<void *>(storage_.data())) decltype(f)(std::move(f));
		thunk_ = &multishot_thunk<Op, decltype(f)>;
	}

	/// Disarm a multishot completion (terminal completion or explicit app stop_*), clearing the thunk so the
	/// completion can be bound again.
	void stop () noexcept
	{
		pal_require(thunk_ != nullptr, "completion stop while not armed");
		thunk_ = nullptr;
	}

	/// Invoke the bound closure via \c Op::dispatch(carrier, f, ec, n). For a single-shot completion, the closure
	/// is copied out and the thunk cleared before \c Op::dispatch runs, so a double-complete without rebind
	/// null-derefs at no extra cost in release.
	void complete (Carrier &carrier, std::error_code ec, size_t n) noexcept
	{
		pal_require(thunk_ != nullptr, "complete on unbound completion");
		thunk_(*this, carrier, ec, n);
	}

private:

	using thunk_type = void (*)(completion &, Carrier &, std::error_code, size_t) noexcept;

	template <typename Op, typename F>
	static void single_shot_thunk (completion &self, Carrier &carrier, std::error_code ec, size_t n) noexcept
	{
		F f;
		std::memcpy(&f, self.storage_.data(), sizeof(F));
		self.thunk_ = nullptr;
		Op::dispatch(carrier, f, ec, n);
	}

	template <typename Op, typename F>
	static void multishot_thunk (completion &self, Carrier &carrier, std::error_code ec, size_t n) noexcept
	{
		auto *f = std::launder(reinterpret_cast<F *>(self.storage_.data()));
		Op::dispatch(carrier, *f, ec, n);
	}

	thunk_type thunk_ = nullptr;
	alignas(std::max_align_t) std::array<std::byte, closure_capacity> storage_;
};

static_assert(sizeof(completion<std::max_align_t>) == cache_line_size);

} // namespace pal::async::__async
