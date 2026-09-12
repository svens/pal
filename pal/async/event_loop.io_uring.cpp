#include <pal/version.hpp>

#if __pal_os_linux

#include <pal/async/__event_loop.hpp>
#include <pal/error.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <endian.h>
#include <linux/io_uring.h>
#include <new>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace pal::async::__event_loop
{

namespace
{

int sys_setup (unsigned entries, ::io_uring_params &params) noexcept
{
	return static_cast<int>(::syscall(__NR_io_uring_setup, entries, &params));
}

int sys_enter (int ring, unsigned to_submit, unsigned min_complete, unsigned flags, const void *arg, size_t argsz) noexcept
{
	return static_cast<int>(::syscall(__NR_io_uring_enter, ring, to_submit, min_complete, flags, arg, argsz));
}

int sys_register (int ring, unsigned opcode, void *arg, unsigned nr_args) noexcept
{
	return static_cast<int>(::syscall(__NR_io_uring_register, ring, opcode, arg, nr_args));
}

unsigned load_acquire (unsigned &value) noexcept
{
	return std::atomic_ref<unsigned>{value}.load(std::memory_order_acquire);
}

void store_release (unsigned &target, unsigned value) noexcept
{
	std::atomic_ref<unsigned>{target}.store(value, std::memory_order_release);
}

struct submission_ring
{
	unsigned *head = nullptr;
	unsigned *tail = nullptr;
	unsigned *mask = nullptr;
	unsigned *entries = nullptr;
	::io_uring_sqe *sqes = nullptr;

	/// Filled by us but not yet published to \c tail: submission stays a plain store until an enter needs it.
	unsigned local_tail = 0;
};

struct completion_ring
{
	unsigned *head = nullptr;
	unsigned *tail = nullptr;
	unsigned *mask = nullptr;
	::io_uring_cqe *cqes = nullptr;
};

struct mapping
{
	void *address = MAP_FAILED;
	size_t size = 0;

	mapping () noexcept = default;

	mapping (void *address, size_t size) noexcept
		: address{address}
		, size{size}
	{
	}

	mapping &operator= (mapping &&that) noexcept
	{
		std::swap(address, that.address);
		std::swap(size, that.size);
		return *this;
	}

	~mapping () noexcept
	{
		if (address != MAP_FAILED)
		{
			::munmap(address, size);
		}
	}

	explicit operator bool () const noexcept
	{
		return address != MAP_FAILED;
	}
};

struct io_uring_loop: posix_loop
{
	unique_fd ring{};
	int enter_ring = -1;
	bool wake_armed = false;
	submission_ring sq{};
	completion_ring cq{};
	mapping ring_map{}, sqe_map{};

	~io_uring_loop () noexcept override
	{
		if (enter_ring != -1)
		{
			::io_uring_rsrc_update update
			{
				.offset = static_cast<uint32_t>(enter_ring),
				.resv = 0,
				.data = 0,
			};
			std::ignore = sys_register(ring, IORING_UNREGISTER_RING_FDS, &update, 1);
		}
	}

	std::error_code register_socket (socket_state &state) noexcept override
	{
		// Every SQE names its own fd, so there is nothing to register up front. Both directions start with
		// room to arm, which is what 'ready' means to this backend.
		state.receive.ready = true;
		state.send.ready = true;
		return {};
	}

	void close_socket (socket_state &state) noexcept override;

	unsigned to_submit () const noexcept
	{
		// Without SQPOLL, only io_uring_enter consumes SQEs.
		// Tail minus head counts pending submissions.
		return sq.local_tail - load_acquire(*sq.head);
	}

	int enter (unsigned min_complete, const void *arg, size_t argsz, unsigned flags) const noexcept;
	void complete_cqe (const ::io_uring_cqe &cqe) noexcept;
	size_t harvest (size_t limit) noexcept;
	::io_uring_sqe *acquire_sqe () noexcept;
	void arm_wake () noexcept;
	void arm_socket_op (task &t, socket_state &state, socket_state::direction &d) noexcept;
};

// Receive-side per-op block. A send carries its destination in the SQE, so only a receive needs one.
struct native_op
{
	::msghdr message;
	::iovec buffer;
};

static_assert(sizeof(native_op) <= io_state::native_capacity);
static_assert(alignof(native_op) <= alignof(std::max_align_t));

// Operations armed with the kernel at once, per socket and direction.
constexpr size_t max_armed = 8;

// Tasks are max_align_t-aligned, so bit 0 of user_data is free to carry the direction and the small
// integers below can never collide with a task address.
constexpr uint64_t wake_user_data = 1;
constexpr uint64_t cancel_user_data = 2;
constexpr uint64_t send_tag = 1;

static_assert(alignof(task) > send_tag);

constexpr ::__kernel_timespec to_kernel_timespec (impl_type::clock::duration d) noexcept
{
	const auto ts = to_timespec(d);
	return {.tv_sec = ts.tv_sec, .tv_nsec = ts.tv_nsec};
}

int io_uring_loop::enter (unsigned min_complete, const void *arg, size_t argsz, unsigned flags) const noexcept
{
	store_release(*sq.tail, sq.local_tail);

	int r = 0;
	do
	{
		r = sys_enter(
			enter_ring,
			to_submit(),
			min_complete,
			IORING_ENTER_GETEVENTS | IORING_ENTER_REGISTERED_RING | flags,
			arg,
			argsz
		);
	} while (r < 0 && errno == EINTR);
	return r;
}

size_t io_uring_loop::harvest (size_t limit) noexcept
{
	auto head = *cq.head;
	const auto tail = load_acquire(*cq.tail);

	size_t n = 0;
	for (; head != tail && n < limit; ++head, ++n)
	{
		complete_cqe(cq.cqes[head & *cq.mask]);
	}

	store_release(*cq.head, head);
	return n;
}

::io_uring_sqe *io_uring_loop::acquire_sqe () noexcept
{
	if (to_submit() == *sq.entries)
	{
		// EBUSY is the completion ring backing up rather than the submission ring: reap first, retry.
		if (enter(0, nullptr, 0, 0) < 0 && errno == EBUSY)
		{
			harvest(static_cast<size_t>(-1));
			std::ignore = enter(0, nullptr, 0, 0);
		}

		if (to_submit() == *sq.entries)
		{
			return nullptr;
		}
	}

	auto &sqe = sq.sqes[sq.local_tail & *sq.mask];
	sqe = {};
	++sq.local_tail;
	return &sqe;
}

void io_uring_loop::arm_wake () noexcept
{
	if (wake_armed)
	{
		return;
	}

	auto *sqe = acquire_sqe();
	if (sqe == nullptr)
	{
		return;
	}

	sqe->opcode = IORING_OP_POLL_ADD;
	sqe->fd = wake.read;
	sqe->len = IORING_POLL_ADD_MULTI;
	sqe->poll32_events = ::htole32(POLLIN);
	sqe->user_data = wake_user_data;
	wake_armed = true;
}

void io_uring_loop::complete_cqe (const ::io_uring_cqe &cqe) noexcept
{
	if (cqe.user_data == wake_user_data)
	{
		wake.drain(*this);
		if ((cqe.flags & IORING_CQE_F_MORE) == 0)
		{
			wake_armed = false;
		}
		return;
	}

	if (cqe.user_data == cancel_user_data)
	{
		// The cancel request's own completion. The ops it cancelled arrive as their own CQEs.
		return;
	}

	const bool send = (cqe.user_data & send_tag) != 0;
	auto &t = *std::bit_cast<task *>(cqe.user_data & ~send_tag);
	auto &op = io(t);
	auto &state = *op.owner;
	auto &d = send ? state.send : state.receive;

	--d.armed;
	on_socket_event(state, d);

	if (cqe.res < 0)
	{
		op.native_error = -cqe.res;
	}
	else if (send)
	{
		op.bytes = static_cast<size_t>(cqe.res);
		op.truncated = false;
	}
	else
	{
		// MSG_TRUNC made res the datagram's own length, so a short window shows up as res past its end
		// rather than as a msg_flags bit to read back.
		auto &n = *std::launder(reinterpret_cast<native_op *>(op.native.data()));
		const auto size = static_cast<size_t>(cqe.res);
		const auto window = t.span().size();
		op.bytes = std::min(size, window);
		op.truncated = size > window;
		op.endpoint_size = n.message.msg_namelen;
	}

	ready_.push(t);
}

void io_uring_poll (impl_type &base, impl_type::clock::duration timeout) noexcept
{
	auto &self = static_cast<io_uring_loop &>(base);
	self.arm_wake();

	unsigned min_complete = 1;
	unsigned flags = 0;
	::__kernel_timespec ts{};
	::io_uring_getevents_arg arg{};
	const void *argp = nullptr;
	size_t argsz = 0;

	if (timeout == impl_type::clock::duration::zero() || !self.wake_armed)
	{
		// Without an armed wake, posts cannot interrupt a blocking wait.
		// Poll without blocking so the next iteration retries arming.
		min_complete = 0;
	}
	else if (timeout != impl_type::clock::duration::max())
	{
		ts = to_kernel_timespec(timeout);
		arg.ts = reinterpret_cast<uint64_t>(&ts);
		argp = &arg;
		argsz = sizeof(arg);
		flags = IORING_ENTER_EXT_ARG;
	}

	std::ignore = self.enter(min_complete, argp, argsz, flags);
	self.harvest(self.config_.poll_batch);
}

void io_uring_loop::arm_socket_op (task &t, socket_state &state, socket_state::direction &d) noexcept
{
	auto &op = io(t);
	const auto payload = t.span();
	const bool send = &d == &state.send;

	auto *sqe = acquire_sqe();
	if (sqe == nullptr)
	{
		op.native_error = ENOBUFS;
		ready_.push(t);
		return;
	}

	sqe->fd = static_cast<int32_t>(state.handle);
	sqe->user_data = std::bit_cast<uint64_t>(&t) | (send ? send_tag : 0);

	if (send)
	{
		// op.endpoint lives in the task's scratch and stays put until the completion, as addr2 requires.
		sqe->opcode = IORING_OP_SEND;
		sqe->addr = reinterpret_cast<uint64_t>(payload.data());
		sqe->len = static_cast<uint32_t>(payload.size());
		sqe->addr2 = reinterpret_cast<uint64_t>(op.endpoint.data());
		sqe->addr_len = static_cast<uint16_t>(op.endpoint_size);
	}
	else
	{
		auto &n = *new (op.native.data()) native_op{};
		n.buffer = {.iov_base = payload.data(), .iov_len = payload.size()};
		n.message.msg_name = op.endpoint.data();
		n.message.msg_namelen = io_state::endpoint_capacity;
		n.message.msg_iov = &n.buffer;
		n.message.msg_iovlen = 1;

		sqe->opcode = IORING_OP_RECVMSG;
		sqe->addr = reinterpret_cast<uint64_t>(&n.message);
		sqe->len = 1;
		sqe->msg_flags = MSG_TRUNC;
	}

	++d.armed;
}

void io_uring_drain_socket (impl_type &base, socket_state &state, socket_state::direction &d) noexcept
{
	auto &self = static_cast<io_uring_loop &>(base);
	while (d.armed < max_armed)
	{
		auto *t = d.pending.try_pop();
		if (t == nullptr)
		{
			break;
		}
		self.arm_socket_op(*t, state, d);
	}
	d.ready = d.armed < max_armed;
}

void io_uring_loop::close_socket (socket_state &state) noexcept
{
	bool cancel_pending = true;
	while (state.armed() > 0)
	{
		if (cancel_pending)
		{
			// handle<> closes the socket only after socket_state teardown, so the fd is still live.
			if (auto *sqe = acquire_sqe(); sqe != nullptr)
			{
				sqe->opcode = IORING_OP_ASYNC_CANCEL;
				sqe->fd = static_cast<int32_t>(state.handle);
				sqe->cancel_flags = IORING_ASYNC_CANCEL_ALL | IORING_ASYNC_CANCEL_FD;
				sqe->user_data = cancel_user_data;
				cancel_pending = false;
			}
		}

		std::ignore = enter(1, nullptr, 0, 0);
		harvest(static_cast<size_t>(-1));
	}

	impl_type::close_socket(state);
}

bool has_required_features (const ::io_uring_params &params) noexcept
{
	// - NODROP: without it the kernel discards CQEs when the completion ring overflows.
	// - EXT_ARG: without it io_uring_enter takes no wait timeout, which run_for() needs.
	// - SINGLE_MMAP: both rings share one mapping, so there is no second one to hold.
	constexpr uint32_t required = IORING_FEAT_NODROP | IORING_FEAT_EXT_ARG | IORING_FEAT_SINGLE_MMAP;
	return (params.features & required) == required;
}

bool has_required_opcodes (int ring) noexcept
{
	constexpr unsigned count = 256;
	alignas(::io_uring_probe) std::byte storage[sizeof(::io_uring_probe) + count * sizeof(::io_uring_probe_op)]{};

	auto &probe = *::new (storage) ::io_uring_probe;
	if (sys_register(ring, IORING_REGISTER_PROBE, &probe, count) < 0)
	{
		return false;
	}

	const auto supported = [&probe](unsigned op) noexcept
	{
		return op < probe.ops_len && (probe.ops[op].flags & IO_URING_OP_SUPPORTED) != 0;
	};

	// SENDMSG_ZC is never issued here: it first appears in 6.1, which two requirements need and neither can
	// be probed for directly.
	// - IORING_OP_SEND gained its destination-address fields in 6.1, without gaining an opcode of its own,
	//   so a 6.0 kernel supports the opcode but rejects addr2 with EINVAL.
	// - IORING_ASYNC_CANCEL_FD (5.19, used at teardown) is a cancel flag; REGISTER_PROBE reports opcodes.
	return supported(IORING_OP_RECVMSG)
		&& supported(IORING_OP_SEND)
		&& supported(IORING_OP_SENDMSG_ZC)
		&& supported(IORING_OP_POLL_ADD)
		&& supported(IORING_OP_ASYNC_CANCEL)
	;
}

int register_ring_fd (int ring) noexcept
{
	::io_uring_rsrc_update update
	{
		.offset = static_cast<uint32_t>(-1),
		.resv = 0,
		.data = static_cast<uint64_t>(ring),
	};

	if (sys_register(ring, IORING_REGISTER_RING_FDS, &update, 1) != 1)
	{
		return -1;
	}

	return static_cast<int>(update.offset);
}

mapping map_ring (int ring, size_t size, uint64_t offset) noexcept
{
	auto *address = ::mmap(
		nullptr,
		size,
		PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_POPULATE,
		ring,
		static_cast<::off_t>(offset)
	);
	return {address, size};
}

template <typename T>
T *at (void *base, uint32_t offset) noexcept
{
	return reinterpret_cast<T *>(static_cast<std::byte *>(base) + offset);
}

} // namespace

result<impl_ptr> make_io_uring_loop (const event_loop_config &config) noexcept
{
	auto *self = new (std::nothrow) io_uring_loop{};
	if (self == nullptr)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	impl_ptr owner{self};

	::io_uring_params params{};
	params.flags |= IORING_SETUP_CQSIZE
		| IORING_SETUP_SUBMIT_ALL
		| IORING_SETUP_SINGLE_ISSUER
		| IORING_SETUP_DEFER_TASKRUN
	;
	params.cq_entries = static_cast<unsigned>(config.completion_depth);

	self->ring = unique_fd{sys_setup(static_cast<unsigned>(config.submission_depth), params)};
	if (!self->ring)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}

	if (!has_required_features(params) || !has_required_opcodes(self->ring))
	{
		return make_unexpected(std::errc::not_supported);
	}

	self->enter_ring = register_ring_fd(self->ring);
	if (self->enter_ring < 0)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}

	const auto ring_size = std::max(
		params.sq_off.array + params.sq_entries * sizeof(unsigned),
		params.cq_off.cqes + params.cq_entries * sizeof(::io_uring_cqe)
	);

	self->ring_map = map_ring(self->ring, ring_size, IORING_OFF_SQ_RING);
	if (!self->ring_map)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}

	self->sqe_map = map_ring(self->ring, params.sq_entries * sizeof(::io_uring_sqe), IORING_OFF_SQES);
	if (!self->sqe_map)
	{
		return unexpected{pal::this_thread::last_system_error()};
	}

	auto *base = self->ring_map.address;

	self->sq.head = at<unsigned>(base, params.sq_off.head);
	self->sq.tail = at<unsigned>(base, params.sq_off.tail);
	self->sq.mask = at<unsigned>(base, params.sq_off.ring_mask);
	self->sq.entries = at<unsigned>(base, params.sq_off.ring_entries);
	self->sq.sqes = static_cast<::io_uring_sqe *>(self->sqe_map.address);
	self->sq.local_tail = *self->sq.head;

	self->cq.head = at<unsigned>(base, params.cq_off.head);
	self->cq.tail = at<unsigned>(base, params.cq_off.tail);
	self->cq.mask = at<unsigned>(base, params.cq_off.ring_mask);
	self->cq.cqes = at<::io_uring_cqe>(base, params.cq_off.cqes);

	auto *sq_array = at<unsigned>(base, params.sq_off.array);
	for (unsigned i = 0; i < params.sq_entries; ++i)
	{
		sq_array[i] = i;
	}

	auto wake = wake_channel::make();
	if (!wake)
	{
		return unexpected{wake.error()};
	}
	self->wake = std::move(*wake);

	self->poll_fn = &io_uring_poll;
	self->drain_fn = &io_uring_drain_socket;
	self->config_ = config;

	self->arm_wake();

	return owner;
}

} // namespace pal::async::__event_loop

#endif // __pal_os_linux
