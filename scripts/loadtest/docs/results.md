# RESULTS

The rules governing what may be recorded here are
[what makes a valid row](measure.md#7-what-makes-a-valid-row); this file is data.

## 1. How to read a row

1. Every row is the audio profile unless its notes say otherwise, which is why there is
   no profile column.
2. Say in the notes whether a row is a ceiling or as deployed. The two are not
   comparable, per [what makes a valid row](measure.md#7-what-makes-a-valid-row).
3. `machine busy` is the whole machine, not the relay process.
4. `sessions/core` is the headline figure, and `core` means vCPU.
5. The CPU model travels with the row, per
   [VM sizes and what changes with them](tune_linux_azure.md#6-vm-sizes-and-what-changes-with-them).
6. Rows taken before `host/counters` owned the arithmetic carry a machine busy truncated
   towards zero, so each is between correct and one point low.

## 2. Results

One row per configuration worth keeping.

| VM size | CPU model | sessions | sessions/core | machine busy | Mbps | Kpps | notes |
|---|---|---|---|---|---|---|---|
| Standard_F4s_v2 | Xeon Platinum 8272CL @ 2.60GHz | 9600 | 2400 | 88 | 568 | 480 | as deployed, untuned; 8 clients x 1200 streams, warmup 15, runtime 60; UdpRcvbufErrors +5710 at this point against the image's 1 MiB rmem_max |
| Standard_F4s_v2 | Xeon Platinum 8272CL @ 2.60GHz | 11200 | 2800 | 95 | 663 | 560 | as deployed, tuned; same deployment as the row above; UdpRcvbufErrors +17130 at this point, rmem_max 8 MiB |
| Standard_F4s_v2 | Xeon Platinum 8272CL @ 2.60GHz | 13600 | 3400 | 91 | 805 | 680 | ceiling, tuned; same deployment; security table dropped and nf_conntrack unloaded with walinuxagent stopped |

## 3. Thread count

A second deployment of the same shape, so these rows are comparable with each other and
not with the table above, per
[what makes a valid row](measure.md#7-what-makes-a-valid-row). Every row is tuned.
`--threads` is the relay's own option; the default is `hardware_concurrency`, which is 4
on this size.

### 3.1 What each thread count reaches

Ceiling rows: netfilter removed with `walinuxagent` stopped, verified absent after each
ladder.

| threads | sessions | machine busy | Kpps | Mbps | relay.cpu at the knee | physical cores used |
|---|---|---|---|---|---|---|
| 1 | 6400 | 26 | 320 | 379 | 96% of one core | 1 |
| 2 | 8000 | 50 | 400 | 474 | 187% of one core | 1 |

1. A thread count is not a core count here. The relay pins thread `i` to CPU `i` and
   steers to it with a `cpu % thread_count` reuseport filter, and CPUs 0 and 1 are one
   physical core, so `--threads=2` is one core and its sibling.
2. Per physical core the rate is flat: 320 Kpps at one thread on one core, and 638 at
   four threads on two. The second thread of a pair adds 80, which is
   [what a sibling is worth](tune_linux_azure.md#6-vm-sizes-and-what-changes-with-them)
   measured on this workload.
3. The knee is the thread, not the machine: at one thread the ladder ends with the
   machine three quarters idle.
4. Both knees are the rung below a failure, not a range exhausted.
5. 8000 sessions at two threads is the boundary rung and it went both ways -- failing on
   one ladder, passing on the next -- which is
   [repeat each point](measure.md#7-what-makes-a-valid-row) showing up at exactly the
   rung it warns about.

### 3.2 What netfilter costs

One load, 4800 sessions at 240 Kpps received, run twice per thread count: once with the
security table dropped and `nf_conntrack` unloaded, once with `walinuxagent` restarted so
it reinstalled both. Nothing else differs, so the delta is conntrack rather than a knee
that also moves with threshold noise.

| threads | machine busy, no conntrack | machine busy, conntrack | relay.cpu, no conntrack | relay.cpu, conntrack | cost |
|---|---|---|---|---|---|
| 1 | 20 | 23 | 72% | 83% | 15% |
| 2 | 27 | 33 | 101% | 124% | 22% |
| 4 | 30 | 35 | 112% | 130% | 17% |

1. Conntrack costs 15 to 22 percent of the CPU the same traffic needs without it, and
   both figures move together.
2. That is more than the 9 percent
   [the guest agent's netfilter table](tune_linux_azure.md#4-the-guest-agents-netfilter-table)
   recorded from a flame graph, which was taken at four threads and a higher session
   count.
3. Receive rate is identical across each pair to within a tenth of a Kpps, so the pairs
   differ in cost and not in what they carried.

## 4. The packet budget

Where a packet's time goes at [the standard point](profile.md#2-the-standard-point), one
thread at 4600 sessions on `Standard_F4s_v2`. The capture and the stage patterns are
[profile.md](profile.md).

    Measured: a packet cost 3255 ns. The relay sample held 2.0% and pal's event
    loop 3.8%, against 43.0% softirq receive and 37.4% send path.

    Measured: one recvmmsg per 14.1 datagrams and one sendmmsg per 15.4, at a
    drain_batch of 16. Syscall entry and exit came to 1.2% of machine busy,
    38 ns of the 3255.

## 5. Batch bounds

`event_loop_config` bounds one drain visit with `drain_batch` and one poll
harvest with `poll_batch`. Everything below is
[the standard point](profile.md#2-the-standard-point) with a synthetic per-packet
handler cost, `udp_relay --handler_spin_ns`.

### 5.1 What drain_batch is worth

| drain_batch | ns/packet | syscall entry and exit ns | datagrams/recvmmsg | datagrams/sendmmsg |
|---|---|---|---|---|
| 16 | 3043 | 31.5 | 14.1 | 15.4 |
| 32 | 3062 | 33.4 | 24.6 | 29.2 |
| 64 | 3132 | 20.9 | 39.2 | 53.0 |
| 128 | 2971 | 18.7 | 53.7 | 84.2 |
| 256 | 3029 | 24.6 | 62.8 | 117.5 |

1. The whole packet does not rank the values: every delta sits at or below the
   pair floor, which for the send stage alone is 119 ns.
2. The stage that moves is syscall entry and exit, against a pair floor near 6,
   and it stops at 64.
3. Supply is not what stops it. `perf/bt/batch_fill` put 64% of recvmmsg calls
   at the cap when the cap was 64, against 91% when it was 16.

### 5.2 What a working handler changes

The per-packet saving is fixed, while a batch's last datagram waits for every
earlier datagram in it to be handled. The two move against each other as the
handler grows.

| handler_spin_ns | ns/packet at 16 | ns/packet at 64 |
|---|---|---|
| 0 | 3043 | 3132 |
| 250 | 3510 | 3361 |
| 500 | 3670 | 3541 |
| 1000 | 4218 | 4117 |

    Measured: at 500 ns of handler, one thread held 82% of a core at 16 and
    79.5% at 64.

Queue delay at 500 ns of handler, `perf/bt/queue_delay`:

| drain_batch | mode | beyond 4 ms | beyond 8 ms |
|---|---|---|---|
| 16 | [256,512) us | 0.76% | 155 samples |
| 64 | [512,1K) us | 0.31% | none |

1. The wait is the smaller term: 32 us at 64 slots and 500 ns, against a queue
   whose mode is already hundreds of us.
2. Client rtt cannot see any of it, being quantised to 1 ms over a 5 ms path.

### 5.3 Where the knee lands

Both values reach the knee
[one thread already records](#31-what-each-thread-count-reaches), on a later
deployment: 800 streams per client passes and 850 fails.

    Measured: 8 clients, one thread, release build -- machine busy 26.7% at 16
    and 26.2% at 64.

    Measured: UdpRcvbufErrors at the passing rung, 6928 at 16 and none at 64; at
    the failing rung, 1031772 and 481585.

A rung is 400 sessions here, which cannot resolve the few percent
[5.1](#51-what-drain_batch-is-worth) reports.

### 5.4 poll_batch is unmeasured

`udp_relay --mode=mturn` gives each thread two sockets.

    Measured: @epoll_events never exceeded 4.

A socket-per-session sample is what would exercise it.

## 6. Open

1. `tc -s qdisc show dev <nic>` at the knee of the next ladder, per
   [qdisc and txqueuelen](tune_linux.md#92-qdisc-and-txqueuelen).
2. The video and bundled profiles have no ladder.
3. No row exists for a VM size other than `Standard_F4s_v2`.
