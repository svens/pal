# TUNE-LINUX

## 1. Using these scripts

1. Every script takes a verb and defaults to `show`, so reading is the default and
   changing is deliberate.

        host/02_ethtool_ring          what it is now, and what apply changes
        sudo host/02_ethtool_ring apply

2. `host/tune` runs every stage in packet-path order, which is what a harness should
   invoke on the relay before a run.
3. None of this survives a reboot, a deallocate/start cycle or a redeploy. Re-apply
   after any of them.

## 2. Preconditions

### 2.1 CPU isolation (00_boot)

1. Reports what the kernel command line has, and prints the line to add. It never
   edits GRUB.
2. The parameters and what they remove from the packet path:

   1. `isolcpus` keeps the general scheduler off the listed CPUs, leaving them for
      threads that pin themselves there.
   2. `nohz_full` stops the periodic timer tick on them while a single task runs.
   3. `rcu_nocbs` moves RCU callback processing off them, which otherwise lands as
      unpredictable softirq work.
   4. `irqaffinity` confines device IRQs to the CPUs left over, so the isolated ones
      are not woken by interrupts.

3. Isolation pays off only together with thread pinning. The relay measured here
   pins its loop threads, and its CPU-keyed CBPF steering assumes they stay put.
4. Split on whole physical cores. An SMT sibling of a housekeeping CPU shares
   execution resources with the CPU taking every interrupt.
5. Isolation and [interrupt affinity](#4-interrupt-affinity-03_irq) are alternatives.
   `irqaffinity` confines every interrupt to the housekeeping CPUs, so softirq runs
   only there; the relay's steering key then takes one value per housekeeping CPU
   regardless of thread count, and a single housekeeping CPU sends every packet to
   one thread. Choose isolation for a pinned single-queue workload, IRQ spreading for
   multi-queue RSS.

### 2.2 Clock and idle states (01_cpu)

1. Both effects show up as latency, not throughput.

   1. Governor. An on-demand governor raises clocks only after it has seen load, so
      the first packets of a burst are processed slowly. "Bursty at low rate" is a
      relay's normal state.
   2. Idle states. A deep C-state is cheap to enter and expensive to leave, and a
      relay at moderate load enters one between packets. Its exit latency is added to
      every packet arriving on an idle CPU, which is why a lightly loaded relay can
      show worse rtt than a busy one.

2. `apply` sets every governor to `performance` and disables idle states 2 and
   deeper, on a machine that has them. A cloud VM usually has neither, per
   [image defaults](tune_linux_azure.md#3-image-defaults-that-differ-from-stock); `show`
   prints what exists.
3. Excluding deeper C-states permanently needs `intel_idle.max_cstate=1` on the
   kernel command line, which is [CPU isolation](#21-cpu-isolation-00_boot)
   territory.

## 3. NIC

Every knob here must reach the interface that owns the hardware queues; on a cloud
guest that is often not the interface the application binds to, which
[tune_linux_azure.md](tune_linux_azure.md) covers.

### 3.1 Receive flow hashing (02_ethtool_hash)

1. Hash UDP flows over source and destination address *and* port (`sdfn`). The
   default on many drivers is address-only, which for a relay means every packet
   from one client VM lands on one queue no matter how many sessions it carries.
2. The script also reports the channel count, since that is how many queues the hash
   has to spread over.
3. This is the prerequisite for the rest of the NIC knobs: with address-only hashing
   there is a single busy queue, and spreading its interrupt or its transmit map has
   nothing to spread.

### 3.2 Ring sizes (02_ethtool_ring)

1. Rings are how many descriptors the NIC can fill before the driver gets to them.
   Requests above the hardware maximum are refused rather than clamped, so `show`
   prints the maximum next to the current value.
2. No measured effect on this workload.

       Measured: 8000 sessions, one F4s_v2 relay, audio profile, equal
       offered load. Stock 316% relay cpu at 84% machine busy; enlarged
       rings alone 320% at 85%. No ring-level counter moved in any
       configuration -- rx_out_of_buffer and every *_discards_phy stayed
       at zero.

### 3.3 Interrupt coalescing (02_ethtool_coalesce)

1. Coalescing is how long the NIC waits before raising an interrupt. `apply` fixes it
   at the script's target, adaptive off.
2. Above saturation the knob reads backwards. Less moderation produced *more*
   latency.

       Measured: 8800 sessions, one F4s_v2 relay, audio profile, otherwise
       untuned, 32 MiB accounted receive buffer per socket. Only rx-usecs and
       rx-frames and their transmit counterparts varied.

   | coalescing | relay cpu | machine busy | rtt |
   |---|---|---|---|
   | 32/64 | 398% | 100% | 131-186 ms |
   | adaptive | 351% | 92% | ~950 us |
   | 128/128 | 337% | 89% | ~900 us |
   | 256/256 | 300% | 85% | ~950 us |
   | 512/512 | 261% | 81% | ~1 ms |
   | 1024/1024 | 226% | 78% | ~1 ms |
   | 2048/2048 | 211% | 77% | 1-2 ms |

3. The reason is where the cost lands. At 440 Kpps a 32 us window lets each queue
   interrupt about that often, and the interrupt load starves the CPU that drains the
   socket -- so the packets already queued pay, not the ones waiting for an
   interrupt.
4. `apply` asks only for the parameters the driver reports. `ethtool` takes a
   coalescing set as one write, so one parameter the driver lacks fails the whole
   set. A field reported as `n/a` is left out and verified as nonexistent rather than
   refused.

## 4. Interrupt affinity (03_irq)

1. Give each receive queue's interrupt its own CPU. Left alone, several can land on
   one CPU while others idle, and that CPU becomes the ceiling for the whole relay.
2. `apply` stops irqbalance first; it rewrites `smp_affinity` continuously and would
   undo the mapping.

## 5. Software receive steering

1. RPS is never applied and has no stage script, which is what the gap at `04_` in
   `host/` is.
2. RPS is the software stand-in for RSS: the CPU taking the interrupt hands the
   packet to another for protocol processing. It helps only when there are more CPUs
   than hardware receive queues, and a VF with a queue per vCPU leaves nothing to
   spread.
3. RFS conflicts with the relay outright. It steers incoming load with
   `SO_ATTACH_REUSEPORT_CBPF` keyed on CPU id, so the listener a packet reaches is
   chosen by the CPU that processed it; RFS instead moves the packet to the CPU that
   last read that flow, which then selects a different listener than the steering
   intended.

## 6. Kernel queues and buffers (05_sysctl)

### 6.1 Backlog and budget

1. `netdev_max_backlog` bounds the packets queued between the driver and softirq
   processing. Overflow appears as `softnet_stat` `dropped`.
2. `netdev_budget` and `netdev_budget_usecs` bound one softirq pass -- packets and
   time. Hitting either appears as `softnet_stat` `squeezed`: work was left behind
   because the pass ran out of allowance, not because of load.
3. Never exercised. `softnet.dropped` stayed at zero throughout and `squeezed` never
   became significant.

### 6.2 Socket buffer ceilings

1. `rmem_max` and `wmem_max` are ceilings, not sizes: they cap what `setsockopt` may
   ask for, and an oversized request is reduced without an error. The application sets
   its own receive and send buffer sizes, so a ceiling below those leaves it running
   with less than its own configuration states.
2. The image's stock ceilings are in
   [image defaults](tune_linux_azure.md#3-image-defaults-that-differ-from-stock).
3. The kernel accounts `skb->truesize` -- several times payload for a small datagram --
   so a queue holds `2 * request / truesize` packets.
4. `show` prints both halves: the ceiling, and what a running process actually holds,
   read from `ss` as `socket.<label> rb <bytes>`. The label is the process name when
   `ss` reveals one and the local address otherwise; neither needs root.
5. `rmem_default` and `wmem_default` reach no socket in this suite, because every one
   of them asks.
6. The two directions report differently when saturated. Receive-side loss lands in
   `UdpRcvbufErrors`. A send buffer that fills surfaces as a failed `send_to` that the
   relay rearms past without counting, so the evidence is a gap between its in and out
   counters and there is no counter row to find.

### 6.3 UDP memory accounting

1. `udp_mem` is system-wide, in pages, as min/pressure/max. It is independent of the
   per-socket limits: crossing `pressure` makes the kernel start trimming regardless
   of how large a socket buffer is. `udp_rmem_min` is the floor kept per socket under
   that pressure.
2. `udp_mem` is reported, not written. `udp_rmem_min` is written to match
   `session_receive_buffer_size`, which sizes each of turn mode's per-session sockets.

### 6.4 Busy polling

1. Reported, never applied. `busy_poll` and `busy_read` let a receive path spin
   instead of sleeping, trading CPU for latency.
2. They are the defaults for `SO_BUSY_POLL` and `SO_BUSY_READ`, so writing them
   reaches sockets that never opted in.

       Measured: 800 sessions, 40 Kpps, one F4s_v2 relay. Setting both to
       50 doubled relay process CPU, 94% of a core to 195%, for the same
       throughput.

## 7. Transmit queue mapping (06_xps)

1. Pin one transmit queue per CPU. Without XPS any CPU may pick any queue, so two can
   contend on one queue lock while another sits idle; pinning removes the contention
   and keeps the descriptor ring in that CPU's cache.
2. `xps_rxqs` -- transmit on the queue paired with the receive queue a flow arrived on
   -- is left alone.
3. `show` prints a verdict before `apply` writes anything, because resizing rings may
   leave the driver's own default map in place rather than an empty one -- and that map
   may already be what `apply` would write.
4. A device the kernel keeps no map for reads as `<unavailable>`, not as an empty mask,
   and `apply` writes nothing to it. The read is refused for a device that is not
   multiqueue.

## 8. Not covered, and why

### 8.1 UDP GRO and GSO

1. Not host knobs at all. `02_ethtool_coalesce` reports the offload flags and never
   writes them, because none of them coalesce anything for this relay.
2. `rx-udp-gro-forwarding` and `rx-gro-list` act on packets routed *through* the host,
   and a userspace relay forwards none. Locally delivered UDP coalesces only when the
   socket sets `UDP_GRO` and the application splits the buffer `recvmsg` returns;
   sending the same way needs `UDP_SEGMENT`, with `tx-udp-segmentation` deciding only
   whether the NIC or the kernel does it.
3. GRO merges packets sharing a 4-tuple within one poll. The client-facing socket
   carries thousands of sessions at 50 pps, so a drain holds almost no two packets of
   the same session: the coalescing factor is about 1, bought with cmsg parsing on
   every receive.
4. GSO needs consecutive datagrams to one destination, and the peer side fans out to
   thousands of peers, so batching would mean holding packets back. The batching that
   fits this traffic is across flows, which the epoll backend already does with
   `recvmmsg` and `sendmmsg`.

### 8.2 Qdisc and txqueuelen

1. Untouched by any stage. A stock image runs `mq` at the root with `fq_codel` per
   hardware queue.
2. At this profile neither can bind: CoDel drops only once packets sit in the queue,
   which happens only when the driver cannot drain the ring. The measured failure was
   on receive, where `UdpInErrors` equalled `UdpRcvbufErrors`.
3. `fq_codel` carries its own per-queue limit, so `txqueuelen` bounds nothing.
