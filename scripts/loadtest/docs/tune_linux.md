# TUNE-LINUX

Every knob `host/` touches: what it is, what it was measured to do here, and the
default that follows. The application measured throughout is a UDP relay -- a
backend service forwarding datagrams for many concurrent sessions -- driven by a
separate load generator.

## 1. Using these scripts

1. Every script takes a verb and defaults to `show`, so reading is the default and
   changing is deliberate.

        host/02_ethtool_ring          what it is now, and what apply changes
        sudo host/02_ethtool_ring apply

2. `host/tune` runs every stage in packet-path order, which is what a harness should
   invoke on the relay before a run. A stage that fails does not stop the rest, and
   `host/tune` exits nonzero if any did.
3. None of this survives a reboot, a deallocate/start cycle or a redeploy. Re-apply
   after any of them.
4. These settings fail without saying so. A ring size above the hardware maximum is
   refused, an IRQ affinity write is undone by irqbalance moments later, and a
   `setsockopt` above `rmem_max` is silently clamped -- all three look like success
   from the caller's side, which is why `show` prints current against target.
5. `apply` re-reads what it wrote and reports anything that did not take. Three
   outcomes are distinguished: the write took, the write was refused, or the knob
   does not exist on this machine -- a guest with no cpufreq is a different machine
   from one whose governor was rejected.
6. The stage numbers carry the packet-path order and group by tool, so the three
   `02_` scripts are the ethtool stages and there is no `04_`: software receive
   steering, below, has no script.

## 2. Preconditions

Machine-wide settings that affect every stage, which is why they sit ahead of the
packet path rather than inside it.

### 2.1 CPU isolation (00_boot)

1. Reports what the kernel command line has, and prints the line to add. It never
   edits GRUB, because a bad edit costs a VM.
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
6. NUMA does not arise at the VM sizes measured here, which are single-socket. On a
   multi-socket host, keep the NIC, its IRQs and the relay threads on the node the
   NIC is attached to (`/sys/class/net/<nic>/device/numa_node`) and start the relay
   under `numactl --cpunodebind=<node> --membind=<node>`.

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
   deeper, on a machine that has them. A cloud VM usually has neither, which is
   [the hypervisor's](tune_linux_azure.md#5-what-the-hypervisor-owns); `show` prints
   what exists.
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
2. The target is `min(<requested>, <hardware maximum>)`, so a NIC that caps lower is
   tuned at its cap.
3. No measured effect on this workload.

       Measured: 8000 sessions, one F4s_v2 relay, audio profile, equal
       offered load. Stock 316% relay cpu at 84% machine busy; enlarged
       rings alone 320% at 85%. No ring-level counter moved in any
       configuration -- rx_out_of_buffer and every *_discards_phy stayed
       at zero.

4. At 110 Kpps per queue, 512 us of moderation accumulates roughly 56 packets, which
   1024 descriptors already hold; the size buys room for higher rates and longer
   windows.
5. The script carries the discard counters, which are what would judge the size if it
   ever bound.

### 3.3 Interrupt coalescing (02_ethtool_coalesce)

1. Coalescing is how long the NIC waits before raising an interrupt. Set fixed rather
   than adaptive: adaptive retunes itself continuously, which makes the interrupt
   rate a function of something outside the experiment and stops two rungs at the
   same load from being comparable.
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
4. The three columns disagree about where to stop; a longer window buys further CPU
   for further latency, until the trade turns at the bottom of the table.
   `02_ethtool_coalesce` fixes the window at its own target.
5. Two conditions on reading it. The rtt column was taken at a buffer depth that
   [socket buffer ceilings](#62-socket-buffer-ceilings) no longer applies, and
   against the smaller ceilings now used the same starvation has nowhere to queue and
   surfaces as loss. The curve will also move with VM size and traffic profile, so it
   is a shape to re-measure rather than values to carry forward.
6. The script reports the offload flags and never writes them, per
   [UDP GRO and GSO](#91-udp-gro-and-gso).
7. `apply` asks only for the parameters the driver reports. `ethtool` takes a
   coalescing set as one write, so one parameter the driver lacks fails the whole
   set. A field reported as `n/a` is left out and verified as nonexistent rather than
   refused.

## 4. Interrupt affinity (03_irq)

1. Give each receive queue's interrupt its own CPU. Left alone, several can land on
   one CPU while others idle, and that CPU becomes the ceiling for the whole relay.
2. Stop irqbalance first. It rewrites `smp_affinity` continuously and would undo the
   mapping moments later.
3. Three ways to handle irqbalance, in order of preference: set
   `/proc/irq/<irq>/affinity_hint` and let it honour them; ban each interrupt with
   `--banirq` and restart it; or stop it. The scripts stop it, because on a machine
   that exists to be measured an external process changing the mapping mid-run is
   worse than losing balance elsewhere.
4. Only interrupts that carry packets get a CPU. Counting an async, command or error
   interrupt into the round-robin shifts every queue one CPU along, which on a
   four-CPU machine lands the last queue back on the first one's CPU.
5. `show` prints the per-CPU counts as well as the mask. A mask says where an
   interrupt may be delivered; the counts say where it went.
6. An interface with no interrupts of its own is reported as an error rather than as
   success with nothing to do, per
   [which interface a knob reaches](tune_linux_azure.md#2-which-interface-a-knob-reaches).
7. Unmeasured here as a knob on its own; applied as part of the tuned set.

## 5. Software receive steering

1. RPS is never applied and has no stage script, which is what the gap at `04_` in
   `host/` is.
2. RPS is the software stand-in for RSS: the CPU taking the interrupt hands the
   packet to another for protocol processing. It helps only when there are more CPUs
   than hardware receive queues, and a VF with a queue per vCPU leaves nothing to
   spread. It would add an inter-CPU handoff per packet for no gain.
3. RFS conflicts with the relay outright. It steers incoming load with
   `SO_ATTACH_REUSEPORT_CBPF` keyed on CPU id, so the listener a packet reaches is
   chosen by the CPU that processed it; RFS instead moves the packet to the CPU that
   last read that flow, which then selects a different listener than the steering
   intended.
4. This is where RPS belongs if a future backend drops CBPF steering, or runs on
   hardware with fewer queues than CPUs.

## 6. Kernel queues and buffers (05_sysctl)

One script, because all four are `sysctl` knobs and grouping by tool keeps it
individually runnable when one layer is re-tuned. What `apply` writes and what `show`
compares against come from one target list in the script; a knob absent from it is
reported and never written.

### 6.1 Backlog and budget

1. `netdev_max_backlog` bounds the packets queued between the driver and softirq
   processing. Overflow appears as `softnet_stat` `dropped`.
2. `netdev_budget` and `netdev_budget_usecs` bound one softirq pass -- packets and
   time. Hitting either appears as `softnet_stat` `squeezed`: work was left behind
   because the pass ran out of allowance, not because of load.
3. Never exercised. `softnet.dropped` stayed at zero throughout and `squeezed` never
   became significant, so these are set against a failure this workload does not
   produce.

### 6.2 Socket buffer ceilings

1. `rmem_max` and `wmem_max` are ceilings, not sizes: they cap what `setsockopt` may
   ask for, and an oversized request is reduced without an error. The application sets
   its own receive and send buffer sizes, so a ceiling below those leaves it running
   with less than its own configuration states.
2. Raising them helped: the stock ceiling on the image used sits below what the
   application asks for. The figures are in
   [tune_linux_azure.md](tune_linux_azure.md), a property of the image rather than of
   Linux.
3. Ceilings are derived from the run's latency budget, not maximised. The kernel
   accounts `skb->truesize` -- several times payload for a small datagram -- and
   stores twice what `setsockopt` asked for, so a queue holds
   `2 * request / truesize` packets.
4. Depth beyond the latency budget cannot add capacity: it defers the drop and
   returns it as delay the oracle rejects. The ceilings therefore sit above what the
   application requests and no further.
5. `show` prints both halves: the ceiling, and what a running process actually holds,
   read from `ss` as `socket.<label> rb <bytes>`. The label is the process name when
   `ss` reveals one and the local address otherwise; neither needs root. `rb` at
   exactly twice `rmem_max` is the signature of a request that reached the ceiling,
   and the ceilings are set above anything the application asks for so that equality
   means that and not coincidence.
6. `rmem_default` and `wmem_default` reach no socket in this suite, because every one
   of them asks. They are held equal to the request so that a socket which did not
   ask could not outqueue the ones sized deliberately.
7. The two directions report differently when saturated. Receive-side loss lands in
   `UdpRcvbufErrors`. A send buffer that fills surfaces as a failed `send_to` that the
   relay rearms past without counting, so the evidence is a gap between its in and out
   counters and there is no counter row to find.

### 6.3 UDP memory accounting

1. `udp_mem` is system-wide, in pages, as min/pressure/max. It is independent of the
   per-socket limits: crossing `pressure` makes the kernel start trimming regardless
   of how large a socket buffer is. `udp_rmem_min` is the floor kept per socket under
   that pressure.
2. Reported, not written. The sizes here suit a relay whose sockets are few -- the one
   measured has a socket per role per thread.
3. A relay that allocates a socket per session, the IETF TURN shape, multiplies the
   default by the session count and meets `udp_mem`'s maximum first. At the default a
   stock image ships that is a few hundred sockets, and a knee that looks like the
   service failing. Such a relay wants `udp_mem` raised, `rmem_default` left small,
   and per-socket sizes set explicitly.

### 6.4 Busy polling

1. Reported, never applied. `busy_poll` and `busy_read` let a receive path spin
   instead of sleeping, trading CPU for latency.
2. They are the defaults for `SO_BUSY_POLL` and `SO_BUSY_READ`, so writing them
   reaches sockets that never opted in.
3. Hurt, at the only load it was tried at.

       Measured: 800 sessions, 40 Kpps, one F4s_v2 relay. Setting both to
       50 doubled relay process CPU, 94% of a core to 195%, for the same
       throughput.

4. Spend that only against a latency figure that justifies it.

## 7. Transmit queue mapping (06_xps)

1. Pin one transmit queue per CPU. Without XPS any CPU may pick any queue, so two can
   contend on one queue lock while another sits idle; pinning removes the contention
   and keeps the descriptor ring in that CPU's cache.
2. This matters as much as the receive side for a relay, where every received packet
   is also a sent packet on the same thread.
3. `xps_rxqs` -- transmit on the queue paired with the receive queue a flow arrived on
   -- is left alone. Set it in an experiment that measures the difference, not as
   tuning.
4. `show` prints a verdict before `apply` writes anything, because resizing rings may
   leave the driver's own default map in place rather than an empty one -- and that map
   may already be what `apply` would write.
5. A device the kernel keeps no map for reads as `<unavailable>`, not as an empty mask,
   and `apply` writes nothing to it. The read is refused for a device that is not
   multiqueue.
6. Unmeasured here as a knob on its own; applied as part of the tuned set.

## 8. Ordering constraints

1. `host/tune` owns the order and lists its stages explicitly rather than globbing
   them.
2. [Ring sizes](#32-ring-sizes-02_ethtool_ring) runs before
   [interrupt coalescing](#33-interrupt-coalescing-02_ethtool_coalesce). A moderation
   window is only safe if the ring holds what accumulates during it.
3. [Ring sizes](#32-ring-sizes-02_ethtool_ring) runs before
   [transmit queue mapping](#7-transmit-queue-mapping-06_xps), for a driver that
   reallocates queues on resize. mlx5 does not.
4. [Interrupt affinity](#4-interrupt-affinity-03_irq) stops irqbalance before writing
   affinity.
5. [CPU isolation](#21-cpu-isolation-00_boot) is report-only wherever it runs.

## 9. Not covered, and why

The knobs a tuned receive path would normally include and this one does not.

### 9.1 UDP GRO and GSO

1. Not host knobs at all. `02_ethtool_coalesce` reports the offload flags and never
   writes them, because none of them coalesce anything for this relay.
2. `rx-udp-gro-forwarding` and `rx-gro-list` act on packets routed *through* the host,
   and a userspace relay forwards none. Locally delivered UDP coalesces only when the
   socket sets `UDP_GRO` and the application splits the buffer `recvmsg` returns;
   sending the same way needs `UDP_SEGMENT`, with `tx-udp-segmentation` deciding only
   whether the NIC or the kernel does it.
3. Not worth building on the application side either, for this relay. GRO merges
   packets sharing a 4-tuple within one poll, and the client-facing socket carries
   thousands of sessions at 50 pps, so a drain holds almost no two packets of the same
   session: the coalescing factor is about 1, bought with cmsg parsing on every
   receive.
4. GSO needs consecutive datagrams to one destination, and the peer side fans out to
   thousands of peers, so batching would mean holding packets back -- latency added
   where the knee already shows about 950 us. The batching that fits this traffic is
   across flows, which the epoll backend already does with `recvmmsg` and `sendmmsg`.
5. The one place it would apply is the load generator's send side, which writes
   uniform payloads to a single destination: `UDP_SEGMENT` would raise sessions per
   client VM. That is test capacity, not a better measurement, and no ladder has
   needed it yet.

### 9.2 Qdisc and txqueuelen

1. Untouched by any stage. A stock image runs `mq` at the root with `fq_codel` per
   hardware queue.
2. At this profile neither can bind. CoDel drops only once packets sit in the queue,
   packets sit only when the driver cannot drain the ring, and at the knee the relay
   sent roughly 478 Kpps and 567 Mbps -- a fraction of the link, with the backlog at
   zero. The measured failure was on receive, where `UdpInErrors` equalled
   `UdpRcvbufErrors`.
3. A qdisc counter would therefore read zero at every rung. `txqueuelen` is inert for
   a related reason: `fq_codel` carries its own per-queue limit, so the interface
   value is not what bounds anything.
4. Confirm it once rather than instrument it: `tc -s qdisc show dev <nic>` at the knee
   of the next ladder, recorded in [results.md](results.md). It becomes worth counting
   only where the link is genuinely in play -- a larger VM, or the video and bundled
   profiles, where 2000 sessions is about 3.1 Gbps each way.

### 9.3 Hypervisor-owned

Clock, idle depth, the host's own scheduling and stolen CPU have no stage: they
are [the hypervisor's](tune_linux_azure.md#5-what-the-hypervisor-owns).
