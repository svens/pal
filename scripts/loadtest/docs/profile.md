# PROFILE

Re-run the profile whenever `pal`'s async or net implementation changes.
`profile-baseline` and `profile-compare` give the difference between two captures at
the same load.

## 1. What can be acted on

`tools/stage_budget.awk` marks each stage in scope or context.

| stage | moved by |
|---|---|
| softirq receive | ring sizes, coalescing, RSS -- [tune_linux](tune_linux.md) |
| netfilter and conntrack | absent by provisioning |
| syscall entry and exit | pal: larger batches, or a different backend |
| io_uring machinery | pal: operations per datagram, registered files and ring fd |
| kernel receive path | pal, socket options, receive buffer size |
| kernel send path | pal, socket options |
| pal event loop | pal |
| relay application | the sample |

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

2. One thread is one core with no sibling, so nothing in the budget is an SMT artifact.
3. The load is a fixed absolute, not the knee.
4. Tuned host.
5. The profile build is `Release` plus `-g -fno-omit-frame-pointer`. The debug info
   resolves inlined frames, which is most of `pal`.

## 3. Capture

These targets run the point.

| target | captures | the baseline in `.profile/state.cmake` |
|---|---|---|
| `profile` | one, and renders its flamegraph | neither read nor written |
| `profile-baseline` | two | refuses if one is set, writes it otherwise |
| `profile-compare` | two, and renders what moved | refuses unless one is set, appends and diffs |
| `profile-clear` | none | removes it, keeping the captures it named |

`perf/prepare` weakens the machine's isolation so perf can observe it, and
`perf/capture` records one window. Both run on the relay.

1. Sampling is system-wide, so softirq time is included.
2. The capture runs inside the counter window, not alongside it: `profile_seconds` is
   shorter than `runtime`. Shares and ns/packet come from rates and survive that; a
   count divided by a count does not.
3. The sample rate is `profile_freq`; ranking functions within a stage needs a higher one.
4. Starting the capture costs the relay a burst of drops. `client_warmup` in
   `azure/harness.cmake` moves the burst ahead of every client's threshold. It stays
   inside the counter window,
   where it shows as `UdpRcvbufErrors` on a capture that is otherwise clean.

       Measured: one load run with a capture and one without, everything
       else equal -- 12% loss in the first measured interval and
       UdpRcvbufErrors +59987, against no loss and no errors.

       Measured: on the boundary, launch skew decided the outcome. Three of
       four clients saw the burst in their first measured interval at 4%
       loss; the fourth saw the same event one interval earlier and passed.
       Offset, the counters still logged +16976 against 12M datagrams.

5. The `bpftrace` scripts disturb the relay throughout, and go outside any window whose
   numbers are kept.

       Measured: with batch_fill attached, relay.cpu went from about 75% of
       a core to 86%, UdpRcvbufErrors appeared, and every client crossed the
       rtt threshold.

## 4. Comparing captures

`profile-baseline` writes `.profile/state.cmake`: the deployment, relay mode, thread
count, session count and conntrack state the reference was taken at, and the ordered
stamps of every capture since. `profile-compare` refuses when any of them disagree.

1. The deployment is the VM's `vmId`, read from IMDS. A redeploy is a different machine.
2. A stamp is `<commit>-<mode>-threads_<n>-sessions_<n>-exe_<hash>-<utc>`. The hash is a
   prefix of the shipped binary's SHA-256, which a compare run reads back to report when
   its binary is the one the previous run measured. The UTC field keeps two measurements
   of one binary at one load apart.
3. Every comparison is against the first stamp.
4. Per stage, the difference is between the baseline's first pass and this run's. The
   pair floor beside it is the larger of the two sides' own pairs, and bounds what one
   pair could resolve.
5. Read a delta against the spread its stage holds across same-code captures.
6. In-scope stages come first, per [what can be acted on](#1-what-can-be-acted-on). The
   stages moved by host knobs and provisioning follow as context, and so does the whole
   packet.
7. Any two stamps can be diffed directly:

       cmake -Dprofile_dir=.profile "-Dstamps=<earlier>;<later>" -P tools/profile_diff.cmake

8. Captures outlive the baseline that named them. `profile-clear` removes the state file
   and lists the rest.

Self-check for the apparatus: `git switch` to the baseline's commit, rebuild, ship,
`profile-compare`. What comes back is the same-code spread.

## 5. Folding happens on the workstation

Symbols are resolved on the VM, which holds the binary, its debug info and `kallsyms`.
What travels is text naming every frame.

[FlameGraph](https://github.com/brendangregg/FlameGraph) is a dependency:
`stackcollapse-perf.pl`, `flamegraph.pl` and `difffolded.pl` on `PATH`.

Artifacts land in `.profile/` under the stamp
[comparing captures](#4-comparing-captures) describes. Nothing there is committed.

Each capture's relay log travels with it as `<label>.relay.log`, holding the
configuration the relay printed at startup. What a run varied beyond what
[the stamp](#4-comparing-captures) names -- batch bounds, handler cost -- is
read from there.

## 6. Syscalls per packet

Epoll backend: `UdpInDatagrams` divided by the `recvmmsg` count is datagrams per receive
syscall. `pal` offers up to `drain_batch` slots per call. Scale both counts to the same seconds before
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

## 8. Pending flow

`perf/bt/pending_depth` reports how many slots pal offered per drain, how many drains a
wakeup costs, and whether the send side is backing up. These move when the pending flow
or the backend changes.

The offered count saturates at `drain_batch` whenever the task pool has spare tasks, so
it measures the pool, not the socket. What was actually waiting is the `recvmmsg` return
value in [syscalls per packet](#6-syscalls-per-packet).

## 9. Rules for what gets committed

1. Shipped `bpftrace` scripts use stable tracepoints and uprobes on our own binaries,
   except [queue delay](#7-queue-delay).
2. `tools/stage_budget.awk` holds the stage patterns. They age as kernel symbols drift: a
   stage reading zero is a pattern to fix before it is a result. Netfilter is the
   exception -- the provisioning leaves it no work, so a low reading is expected. It is
   not exactly zero, because the pattern catches symbols outside the table's path;
   `nft list ruleset` decides whether a larger one means the table returned.
