# PROFILE

Where the time inside a packet goes. [measure](measure.md) says what the relay
reaches; this says what it spends. Re-run it whenever `pal`'s async or net
implementation changes.

The output is a comparison: a differential between two captures at the same load.

## 1. What can be acted on

Optimisation scope is `pal`, the relay sample, and kernel knobs. Every stage carries
what could move it; the rest account for the time.

| stage | moved by |
|---|---|
| softirq receive | ring sizes, coalescing, RSS -- [tune_linux](tune_linux.md) |
| netfilter and conntrack | absent by provisioning, costed in [results](results.md#32-what-netfilter-costs) |
| syscall entry and exit | pal: larger batches, or a different backend |
| kernel recvmmsg body | pal, socket options, receive buffer size |
| kernel sendmmsg body | pal, socket options |
| pal event loop | pal |
| relay application | the sample |

What a run finds at [the standard point](#2-the-standard-point) on `Standard_F4s_v2`
is in [the packet budget](results.md#4-the-packet-budget).

Two things are out of scope and not measured.

1. Anything whose only remedy is a host change or a kernel rebuild.
2. Microarchitecture. Hardware counters need a virtual PMU, which Azure exposes on HPC
   sizes only, so `perf` samples `cpu-clock` here and `perf stat`, `perf c2c` and
   `perf lock` have nothing to count. `perf/prepare` reports which a deployment is.

## 2. The standard point

    ninja deploy
    ninja profile-ship
    ninja tune
    ninja profile-baseline

1. The point is `profile_streams` and `profile_relay_args` in `cluster.conf`, at the
   shared `warmup`. Streams are per client, so another deployment needs them adjusted:
   the criterion is the highest load that still captures without loss.

       Measured: 4600 sessions held one thread at 71% of a core, 230 Kpps,
       with no loss under capture.

       Measured: the same 1000 streams cost 61% of a core on one
       deployment and 69% on another, same VM size and CPU model.

2. One thread is one core with no sibling, so nothing in the budget is an SMT artifact.
3. The load is a fixed absolute, not the knee. The knee moves with the code, and two
   captures either side of a change have to describe the same work.
4. Tuned host, netfilter absent, per
   [the guest agent's netfilter table](tune_linux_azure.md#4-the-guest-agents-netfilter-table).
5. The profile build is `Release` plus `-g -fno-omit-frame-pointer`. Frame-pointer call
   graphs cost about one percent; dwarf unwinding copies 8 KB of stack per sample. The
   debug info resolves inlined frames, which is most of `pal`.

## 3. Capture

These targets run the point. Where two captures are taken, the difference between them
is the least a comparison has to beat.

| target | captures | the baseline in `.profile/state.cmake` |
|---|---|---|
| `profile` | one, and renders its flamegraph | neither read nor written |
| `profile-baseline` | two | refuses if one is set, writes it otherwise |
| `profile-compare` | two, and renders what moved | refuses unless one is set, appends and diffs |
| `profile-clear` | none | removes it, keeping the captures it named |

`perf/prepare` weakens the machine's isolation so perf can observe it, and
`perf/capture` records one window; `ninja help` lists the targets and each script
carries its own usage. Both run on the relay.

1. Sampling is system-wide. Softirq is a third of this machine's busy time and no
   per-process profile sees it.
2. The capture runs inside the counter window, not alongside it: `profile_seconds` is
   shorter than `runtime`. Shares and ns/packet come from rates and survive that; a
   count divided by a count does not.
3. `profile_freq` 99 over 30 s is about 12000 samples, one percent of CPU being 120 of
   them. That attributes stages. Ranking functions within a stage needs a higher rate
   and transfers proportionally more.
4. Starting the capture costs the relay a burst of drops. The harness starts it when its
   own warm-up sleep returns, so the clients are given five seconds more and the burst
   lands before any of them applies a threshold. It stays inside the counter window,
   where it shows as `UdpRcvbufErrors` on a capture that is otherwise clean.

       Measured: one load run with a capture and one without, everything
       else equal -- 12% loss in the first measured interval and
       UdpRcvbufErrors +59987, against no loss and no errors.

       Measured: on the boundary, launch skew decided the outcome. Three of
       four clients saw the burst in their first measured interval at 4%
       loss; the fourth saw the same event one interval earlier and passed.
       Offset, the counters still logged +16976 against 12M datagrams.

5. The `bpftrace` scripts disturb it throughout, and go outside any window whose numbers
   are kept.

       Measured: with batch_fill attached, relay.cpu went from about 75% of
       a core to 86%, UdpRcvbufErrors appeared, and every client crossed the
       rtt threshold.

## 4. Comparing captures

`profile-baseline` writes `.profile/state.cmake`: the deployment, thread count, session
count and conntrack state the reference was taken at, and the ordered stamps of every
capture since. `profile-compare` refuses when any of them disagree.

1. The deployment is the VM's `vmId`, read from IMDS. A redeploy is a different machine,
   and [the standard point](#2-the-standard-point) is what that costs.
2. A stamp is `<commit>-threads_<n>-sessions_<n>-exe_<hash>-<utc>`. The hash is a prefix
   of the shipped binary's SHA-256, which a compare run reads back to report when its
   binary is the one the previous run measured. The UTC field keeps two measurements of
   one binary at one load apart. The deployment and the conntrack state live in the state
   file.
3. Every comparison is against the first stamp.
4. Per stage, the difference is between the baseline's first pass and this run's. The
   pair floor beside it is the larger of the two sides' own pairs, and bounds what one
   pair could resolve.
5. Read a delta against the spread its stage holds across same-code captures, which
   [the packet budget](results.md#4-the-packet-budget) records.
6. In-scope stages come first, per [what can be acted on](#1-what-can-be-acted-on). The
   stages moved by host knobs and provisioning follow as context, and so does the whole
   packet.
7. Any two stamps can be diffed directly:

       cmake -Dprofile_dir=.profile "-Dstamps=<earlier>;<later>" -P tools/profile_diff.cmake

8. Captures outlive the baseline that named them. `profile-clear` removes the state file
   and lists the rest.
9. The per-packet cost divides the jiffies charged to non-idle states by the datagrams
   counted over the same window. `host/counters delta` computes it, alongside the receive
   rate and machine busy.

Self-check for the apparatus: `git switch` to the baseline's commit, rebuild, ship,
`profile-compare`. What comes back is the spread
[the packet budget](results.md#4-the-packet-budget) records.

## 5. Folding happens on the workstation

Symbols are resolved on the VM, which holds the binary, its debug info and `kallsyms`.
What travels is text naming every frame.

[FlameGraph](https://github.com/brendangregg/FlameGraph) is a dependency:
`stackcollapse-perf.pl`, `flamegraph.pl` and `difffolded.pl` on `PATH`.

Artifacts land in `.profile/` under the stamp
[comparing captures](#4-comparing-captures) describes. Nothing there is committed.

    difffolded.pl before.folded after.folded | flamegraph.pl > diff.svg

Each capture's relay log travels with it as `<label>.relay.log`, holding the
configuration the relay printed at startup. What a run varied beyond what
[the stamp](#4-comparing-captures) names -- batch bounds, handler cost -- is
read from there.

## 6. Syscalls per packet

`UdpInDatagrams` divided by the `recvmmsg` count is datagrams per receive syscall. `pal`
offers up to `drain_batch` slots per call. Scale both counts to the same seconds before
dividing, per [capture](#3-capture).

1. Near the cap: syscall overhead is amortised and the cost is the kernel's per-datagram
   path.
2. Near one: the loop returns before the batch fills, and the lever is how long it waits
   rather than how many slots it offers.
3. `perf/bt/batch_fill` gives the distribution, which separates a steady low fill from a
   bursty one.

## 7. Queue delay

`perf/bt/queue_delay` measures the gap between the kernel queueing a datagram on the
socket and the relay reading it. That gap is the part of rtt the relay owns, and it
answers to receive buffer size and to how long the loop waits. `UdpRcvbufErrors` says the
queue overflowed; this says how close it ran.

1. It times one datagram in 64. Tracking all of them fills the map in under a second at
   this packet rate, because an entry lives until its datagram is consumed and a dropped
   one never returns to be deleted.
2. It is the only shipped script probing a kernel function rather than a tracepoint, so a
   kernel upgrade can break it. An empty histogram means the probes did not attach; the
   script says how to check.

## 8. Pending flow

`perf/bt/pending_depth` reports how many slots pal offered per drain, how many drains a
wakeup costs, and whether the send side is backing up. These move when the pending flow
or the backend changes.

The offered count saturates at `drain_batch` whenever the task pool has spare tasks, so
it measures the pool, not the socket. What was actually waiting is the `recvmmsg` return
value in [syscalls per packet](#6-syscalls-per-packet).

What can be probed is a property of the build. `impl_type::drain_actionable` is inlined at
its only call site, so a uprobe on its surviving symbol counts nothing; the drain
functions reached through the backend's function-pointer table cannot be inlined and do
report. A silent uprobe is a build question first.

## 9. Rules for what gets committed

1. Shipped `bpftrace` scripts use stable tracepoints and uprobes on our own binaries.
   Kprobes on kernel internals stay ad-hoc, with `perf/bt/queue_delay` as the stated
   exception.
2. `tools/stage_budget.awk` holds the stage patterns. They age as kernel symbols drift: a
   stage reading zero is a pattern to fix before it is a result. Netfilter is the
   exception -- the provisioning leaves it no work, so a low reading is expected. It is
   not exactly zero, because the pattern catches symbols outside the table's path;
   `nft list ruleset` decides whether a larger one means the table returned.
3. Captures are artifacts. What belongs in [results](results.md) is a conclusion drawn
   from one, not the file.

## 10. Linux only

`pal`'s epoll backend batches with `recvmmsg` and `sendmmsg`; the kqueue backend issues
one syscall per datagram. The stages here assume the Linux path. The other backends need
their own instrument.
