#include <pal/version.hpp>

#if __pal_os_linux

#include <pal/async/__event_loop.hpp>
#include <pal/error.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <endian.h>
#include <limits>
#include <linux/io_uring.h>
#include <memory>
#include <new>
#include <optional>
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

template <typename T>
T load_acquire (T &value) noexcept
{
	return std::atomic_ref<T>{value}.load(std::memory_order_acquire);
}

template <typename T>
void store_release (T &target, T value) noexcept
{
	std::atomic_ref<T>{target}.store(value, std::memory_order_release);
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

} // namespace

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

constexpr size_t buffer_headroom = cache_line_size;
constexpr size_t buffer_kernel_offset = sizeof(uint64_t);

static_assert(buffer_kernel_offset + sizeof(::io_uring_recvmsg_out) + io_state::endpoint_capacity <= buffer_headroom);

constexpr ::msghdr multishot_message = []
{
	::msghdr m{};
	m.msg_namelen = buffer_headroom - buffer_kernel_offset - sizeof(::io_uring_recvmsg_out);
	return m;
}();

struct multishot_ring
{
	multishot_pool &pool;
	mapping ring_map;
	const unsigned mask;
	const uint16_t group;
	size_t on_ring = 0;
	enum class arm_state: uint8_t { parked, armed, queued } state = arm_state::parked;
	intrusive_queue_hook<multishot_ring> rearm_hook{};

	[[nodiscard]] ::io_uring_buf *entries () const noexcept
	{
		return static_cast<::io_uring_buf *>(ring_map.address);
	}

	[[nodiscard]] uint16_t &tail () const noexcept
	{
		return static_cast<::io_uring_buf_ring *>(ring_map.address)->tail;
	}
};

namespace
{

constexpr size_t group_count = size_t{std::numeric_limits<uint16_t>::max()} + 1;
constexpr size_t group_word_bits = std::numeric_limits<uint64_t>::digits;

struct io_uring_loop: posix_loop
{
	unique_fd ring{};
	int enter_ring = -1;
	bool wake_armed = false;
	submission_ring sq{};
	completion_ring cq{};
	mapping ring_map{}, sqe_map{};
	std::array<uint64_t, group_count / group_word_bits> groups_in_use{};
	intrusive_queue<&multishot_ring::rearm_hook> rearm_queue{};

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
	std::error_code start_multishot (multishot_pool &pool) noexcept override;

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

	std::optional<uint16_t> acquire_group () noexcept;
	void release_group (uint16_t group) noexcept;
	void provide (multishot_ring &ms, task &t) noexcept;
	bool arm_multishot (multishot_ring &ms) noexcept;
	void retry_rearms () noexcept;
	void complete_multishot (multishot_ring &ms, const ::io_uring_cqe &cqe) noexcept;
	void close_multishot (multishot_ring &ms) noexcept;
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

// Tasks and multishot rings are aligned past bits 0 and 1, which carry the tags below. The small integers alias
// those tags, so complete_cqe tests them by exact value before decoding any tag.
constexpr uint64_t wake_user_data = 1;
constexpr uint64_t cancel_user_data = 2;
constexpr uint64_t send_tag = 1;
constexpr uint64_t multishot_tag = 2;

static_assert(alignof(task) > (send_tag | multishot_tag));
static_assert(alignof(multishot_ring) > (send_tag | multishot_tag));

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

	if ((cqe.user_data & multishot_tag) != 0)
	{
		complete_multishot(*std::bit_cast<multishot_ring *>(cqe.user_data & ~multishot_tag), cqe);
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
	self.retry_rearms();

	unsigned min_complete = 1;
	unsigned flags = 0;
	::__kernel_timespec ts{};
	::io_uring_getevents_arg arg{};
	const void *argp = nullptr;
	size_t argsz = 0;

	if (timeout == impl_type::clock::duration::zero() || !self.wake_armed || !self.rearm_queue.empty())
	{
		// Without an armed wake, posts cannot interrupt a blocking wait, and a queued multishot re-arm would
		// wait for an unrelated completion. Poll without blocking so the next iteration retries arming.
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

std::optional<uint16_t> io_uring_loop::acquire_group () noexcept
{
	const auto word = std::ranges::find_if(groups_in_use, [](uint64_t bits) noexcept
	{
		return bits != ~uint64_t{0};
	});
	if (word == groups_in_use.end())
	{
		return std::nullopt;
	}

	const auto bit = static_cast<size_t>(std::countr_one(*word));
	*word |= uint64_t{1} << bit;
	return static_cast<uint16_t>(static_cast<size_t>(word - groups_in_use.begin()) * group_word_bits + bit);
}

void io_uring_loop::release_group (uint16_t group) noexcept
{
	groups_in_use[group / group_word_bits] &= ~(uint64_t{1} << (group % group_word_bits));
}

void io_uring_loop::provide (multishot_ring &ms, task &t) noexcept
{
	prime_multishot(ms.pool, t);
	auto &op = io(t);
	op.owner = ms.pool.owner;
	op.native_error = 0;
	op.multishot = true;

	const auto region = headroom_and_buffer(ms.pool, t);
	const auto tail = ms.tail();
	auto &entry = ms.entries()[tail & ms.mask];
	entry.addr = reinterpret_cast<uint64_t>(region.data() + buffer_kernel_offset);
	entry.len = static_cast<uint32_t>(region.size() - buffer_kernel_offset);
	std::memcpy(&entry.bid, region.data(), sizeof(entry.bid));
	store_release(ms.tail(), static_cast<uint16_t>(tail + 1));

	++ms.on_ring;
	++stats_.io_in_flight;
}

bool io_uring_loop::arm_multishot (multishot_ring &ms) noexcept
{
	auto *sqe = acquire_sqe();
	if (sqe == nullptr)
	{
		ms.state = multishot_ring::arm_state::queued;
		rearm_queue.push(ms);
		return false;
	}

	auto &state = *ms.pool.owner;
	sqe->opcode = IORING_OP_RECVMSG;
	sqe->fd = static_cast<int32_t>(state.handle);
	sqe->addr = reinterpret_cast<uint64_t>(&multishot_message);
	sqe->len = 1;
	sqe->msg_flags = MSG_TRUNC;
	sqe->ioprio = IORING_RECV_MULTISHOT;
	sqe->flags = IOSQE_BUFFER_SELECT;
	sqe->buf_group = ms.group;
	sqe->user_data = std::bit_cast<uint64_t>(&ms) | multishot_tag;

	ms.state = multishot_ring::arm_state::armed;
	++state.receive.armed;
	return true;
}

void io_uring_loop::retry_rearms () noexcept
{
	auto queued = std::move(rearm_queue);
	while (auto *ms = queued.try_pop())
	{
		if (!arm_multishot(*ms))
		{
			rearm_queue.splice(std::move(queued));
			return;
		}
	}
}

void io_uring_loop::complete_multishot (multishot_ring &ms, const ::io_uring_cqe &cqe) noexcept
{
	if ((cqe.flags & IORING_CQE_F_BUFFER) != 0)
	{
		auto &t = slot(ms.pool, cqe.flags >> IORING_CQE_BUFFER_SHIFT);
		--ms.on_ring;

		auto &op = io(t);
		if (cqe.res < 0)
		{
			op.native_error = -cqe.res;
		}
		else
		{
			const auto *region = headroom_and_buffer(ms.pool, t).data() + buffer_kernel_offset;
			::io_uring_recvmsg_out out;
			std::memcpy(&out, region, sizeof(out));
			std::memcpy(op.endpoint.data(), region + sizeof(out), io_state::endpoint_capacity);
			op.endpoint_size = std::min<uint32_t>(out.namelen, io_state::endpoint_capacity);
			op.bytes = std::min<size_t>(out.payloadlen, ms.pool.buffer_size);
			op.truncated = out.payloadlen > ms.pool.buffer_size;
		}
		ready_.push(t);
	}

	if ((cqe.flags & IORING_CQE_F_MORE) == 0)
	{
		--ms.pool.owner->receive.armed;
		if (ms.on_ring != 0 && ms.pool.ring == &ms)
		{
			ms.state = multishot_ring::arm_state::queued;
			rearm_queue.push(ms);
		}
		else
		{
			ms.state = multishot_ring::arm_state::parked;
		}
	}
}

void io_uring_requeue_multishot (multishot_pool &pool, task &t) noexcept
{
	auto &self = static_cast<io_uring_loop &>(*pool.loop);
	auto &ms = *pool.ring;
	self.provide(ms, t);
	if (ms.state == multishot_ring::arm_state::parked)
	{
		self.arm_multishot(ms);
	}
}

std::error_code io_uring_loop::start_multishot (multishot_pool &pool) noexcept
{
	const auto acquired = acquire_group();
	if (!acquired)
	{
		return std::make_error_code(std::errc::no_buffer_space);
	}
	const auto group = *acquired;

	const auto entries = std::bit_ceil(pool.task_count);
	std::unique_ptr<multishot_ring> ms{new (std::nothrow) multishot_ring
	{
		.pool = pool,
		.ring_map = {},
		.mask = static_cast<unsigned>(entries - 1),
		.group = group,
	}};
	if (ms == nullptr)
	{
		release_group(group);
		return std::make_error_code(std::errc::not_enough_memory);
	}

	const auto size = entries * sizeof(::io_uring_buf);
	ms->ring_map = mapping{::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0), size};

	::io_uring_buf_reg reg{};
	reg.ring_addr = reinterpret_cast<uint64_t>(ms->ring_map.address);
	reg.ring_entries = static_cast<uint32_t>(entries);
	reg.bgid = group;

	if (!ms->ring_map || sys_register(ring, IORING_REGISTER_PBUF_RING, &reg, 1) < 0)
	{
		const auto error = pal::this_thread::last_system_error();
		release_group(group);
		return error;
	}

	pool.ring = ms.release();
	for (size_t i = 0; i < pool.task_count; ++i)
	{
		auto &t = slot(pool, i);
		const auto bid = static_cast<uint16_t>(i);
		std::memcpy(headroom_and_buffer(pool, t).data(), &bid, sizeof(bid));
		provide(*pool.ring, t);
	}
	arm_multishot(*pool.ring);

	return {};
}

void io_uring_loop::close_multishot (multishot_ring &ms) noexcept
{
	if (ms.state == multishot_ring::arm_state::queued)
	{
		rearm_queue.remove(ms);
	}

	stats_.io_in_flight -= ms.on_ring;

	::io_uring_buf_reg reg{};
	reg.bgid = ms.group;
	std::ignore = sys_register(ring, IORING_UNREGISTER_PBUF_RING, &reg, 1);

	release_group(ms.group);
}

void io_uring_loop::close_socket (socket_state &state) noexcept
{
	std::unique_ptr<multishot_ring> ms{state.pool != nullptr ? std::exchange(state.pool->ring, nullptr) : nullptr};

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

	if (ms)
	{
		close_multishot(*ms);
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

	// SENDMSG_ZC is never issued here: it first appears in 6.1, which these requirements need and none can be
	// probed for directly.
	// - IORING_OP_SEND gained its destination-address fields in 6.1, without gaining an opcode of its own,
	//   so a 6.0 kernel supports the opcode but rejects addr2 with EINVAL.
	// - IORING_ASYNC_CANCEL_FD (5.19, used at teardown) is a cancel flag; REGISTER_PROBE reports opcodes.
	// - IORING_REGISTER_PBUF_RING (5.19) is a register opcode, IORING_RECV_MULTISHOT (6.0) a RECVMSG flag.
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
	self->requeue_multishot_fn = &io_uring_requeue_multishot;
	self->multishot_headroom = buffer_headroom;
	self->config_ = config;

	self->arm_wake();

	return owner;
}

} // namespace pal::async::__event_loop

#endif // __pal_os_linux
