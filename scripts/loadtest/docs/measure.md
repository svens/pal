# MEASURE

How to run a ladder and read what comes back. Host tuning is
[tune_linux.md](tune_linux.md); getting machines is [cluster.md](cluster.md).

The application under test is a UDP relay, driven by a load generator running on
separate client VMs. Both sides are measured: the generator reports loss and
latency as a receiver sees them, and the relay host reports what the kernel did
underneath.

## 1. The oracle

1. The exit code is the only contract the load generator offers. `--max-loss`
   and `--max-rtt-ms` turn a threshold violation into a nonzero exit, and that
   is the whole pass/fail rule. Nothing parses its output.
2. A run is `warmup` seconds followed by `runtime` seconds of measurement, so it
   takes the sum of the two. Thresholds apply only once the second phase starts.
3. A violation latches for the whole process, so without the split the oracle
   reports the point where startup crossed a threshold rather than the point
   where the service did.

       Measured: at 9000 sessions the first intervals reported tens of
       milliseconds of rtt where the settled run reported about one.

4. The transient grows with session count, so `warmup` is a constant to re-check
   at the top rung of a ladder rather than one that holds across VM sizes. The
   shipped value is what the rungs measured so far needed.

## 2. Traffic profiles

1. `kbps` and `payload` are a pair, not two independent knobs: the generator
   paces at `kbps * 125 / payload` packets per second. `payload` is the whole
   datagram, so it covers RTP and SRTP overhead as well as media.

   | profile | kbps | payload | pps | models |
   |---|---|---|---|---|
   | audio | 48 | 120 | 50 | Opus, 20 ms packetization |
   | video | 1500 | 1200 | 156 | 720p 1:1, packets sized to the MTU |
   | bundled | 1550 | 940 | 206 | audio and video muxed onto one allocation |

2. Audio is the default. It keeps the relay CPU-bound, which is what a
   sessions-per-core figure is supposed to measure.
3. The other two put several times the packet rate on each client, so one
   generator process sustains proportionally fewer streams.
4. Reach a higher session count by adding client VMs rather than load per VM.
   Otherwise the sweep finds the client's limit and reports it as the relay's.

## 3. One point: run

    ninja run
    cmake -Dstreams=500 -Druntime=120 -P azure/run.cmake

1. `run` starts the generator on every client VM at once, waits for them, and
   prints the relay-side counter deltas for the same window.
2. It does not start the relay. The relay is the side being watched, so
   `start-relay-linux` puts it in a tmux session where a process monitor and a
   counter watch can sit next to it.
3. The relay-side counters bracket the measured window, shortened by the time it
   took to launch the clients. Each client keeps its own clock, so only between
   the last one leaving warm-up and the first one finishing is every client
   actually sending.
4. Read the launch skew off the run log: rows above `running` are the transient,
   and a client whose rows stop before the window ends was launched too late.

## 4. A ladder: sweep

1. `sweep` ramps `streams` until a run fails and reports the last passing point.
   It needs no output parsing, because [the oracle](#1-the-oracle) is the entire
   contract.
2. `sweep` owns the relay rather than watching one. It refuses to start while
   any relay is running, restarts it between steps, and stops it at the end, so
   each rung starts from an empty relay.
3. There is deliberately no relay afterwards, and a following `run` will ask for
   one. An aborted sweep is the opposite case: its relay survives, and the next
   sweep refuses until `stop-relay-linux` clears it.
4. The restart exists because the relay's client map never evicts. With
   `relay_restart` off, a twenty-step ladder reaches its last rung holding every
   earlier rung's sessions in the same lookup, so the knee it reports depends on
   how the ladder got there. Turning it off on purpose is how to measure what
   that costs.
5. Restarted steps run detached rather than under tmux, each writing
   `relay-<streams>.log` on the relay. That is where a step which killed the
   relay explains itself.
6. Each rung costs `warmup` plus `runtime` plus roughly fifteen seconds of
   launch and polling, and the ladder walks `sweep_start` to `sweep_max` in
   `sweep_step` increments. Deploy and provisioning are several minutes on top.

## 5. Relay-side counters

`host/counters` is the relay-side half of the measurement. The generator sees
loss and rtt from a receiver, which cannot separate "the relay was slow" from
"the kernel dropped it first" from "we ran out of CPU". These can.

1. **The run did not measure the service.** `softnet.dropped`, the NIC-level
   discard counters, and `machine.steal` above one percent all mean the load
   never reached the relay, or the hypervisor took the CPU needed to process it.
   `run` and `sweep` warn when any of them moves, and `sweep` repeats the
   warning at the end so it cannot be lost among twenty step reports. Neither
   aborts: a stray drop during warm-up should not end a half-hour sweep, and a
   few stolen jiffies are ordinary on a shared host. Sustained steal is
   [the hypervisor's](tune_linux_azure.md#5-what-the-hypervisor-owns), not
   something to tune.
2. **The service reached its limit.** `UdpRcvbufErrors` means the relay was not
   draining its socket fast enough. That is what the run is for, so it is
   deliberately not treated as a fault. The exception is a ceiling having
   clamped the request below what the process asked for, which turns it back
   into a host problem;
   [socket buffer ceilings](tune_linux.md#72-socket-buffer-ceilings) prints both
   halves.
3. **A tuning signal, not a verdict.** `softnet.squeezed` with `dropped` flat
   means a softirq pass was cut short by its allowance rather than by load. Some
   of that is normal at high packet rates; see
   [backlog and budget](tune_linux.md#71-backlog-and-budget).

`run` and `sweep` report the relay's receive rate from its own counters, so the
Kpps and Mbps in a results row are measured rather than derived from what the
clients were asked to send. Machine busy comes from the same pair of snapshots
over the same window, and `sweep` keeps it for the last rung that passed --
without it, a knee at 60% and one at 90% print identically though only one has
headroom.

## 6. CPU: process versus machine

1. Two CPU figures are reported, and the gap between them is the useful part.

   1. `relay.cpu` is the relay process alone, from `/proc/<pid>/stat`. It cannot
      see softirq time, and softirq is where much of a relay's cost lives,
      growing with packet rate.
   2. `machine.cpu` is the whole machine from `/proc/stat`, with the softirq
      share broken out.

2. A high softirq share means the kernel's receive path dominates and
   application work will not move the limit. High process time with low softirq
   means the opposite.
3. `relay.cpu` reads `unmeasurable: pid <before> -> <after>` when the process
   did not survive the window as one pid. A relay that exited has no figure; a
   restarted one counts from zero.
4. Size a fleet from `machine.cpu`, which is what a results row records.
   `relay.cpu` alone understates the load, and worst where it matters most.

       Measured: 76% of four cores by the process figure against 89%
       machine busy at the same moment. The gap closes as load rises --
       14 points at 9600 sessions, 1 point at 11200 -- so extrapolating
       the next rung from relay.cpu predicts headroom that is not there.

## 7. What makes a valid row

1. An untuned and a tuned row belong to the same deployment or neither means
   anything.

       Measured: two clusters of the same size and CPU model differed by
       about 12% -- 405 Kpps at 75% machine busy on one, 400 Kpps at 84%
       on the other.

   Run the pair close together rather than separated by other experiments: the
   spread between deployments is larger than most of the knobs worth measuring.
2. Repeat each point and take the median. On a cloud VM the run-to-run spread is
   large enough to invent differences that are not there, and boundary rungs are
   where it matters most.
3. Record machine busy rather than process CPU, for the reason in
   [CPU: process versus machine](#6-cpu-process-versus-machine): the process
   figure cannot see softirq, and the gap between them closes as load rises, so
   it is least trustworthy exactly where it would be used.
4. Rows are of two kinds and comparing across them is meaningless.

   1. **As deployed.** Netfilter present, as the production nodes have it. This
      is the number that predicts capacity.
   2. **Ceiling.** Netfilter removed by hand before the sweep. This bounds what
      the relay could reach with nothing else in the way, and is the useful
      starting point for profiling the relay itself. It is never the deployable
      figure.

   Nothing in `host/` removes it and nothing should, for the reason in
   [the guest agent's netfilter table](tune_linux_azure.md#4-the-guest-agents-netfilter-table).
   Note which kind a row is when adding it.
5. The headline figure is sessions per core at a fixed loss and rtt threshold.
   Raw Mbps does not survive a change of VM size; sessions per core does.

## 8. Profiling

    sudo perf/00_sysctl                  # once per boot
    perf/01_run ./<relay> --threads=4
    perf/02_report <relay>               # writes perf_<relay>.svg

A flame graph shows where CPU went. It does not show why packets were dropped,
which is [relay-side counters](#5-relay-side-counters). `02_report` needs
FlameGraph's `stackcollapse-perf.pl` and `flamegraph.pl` on `PATH`.

## 9. Caveats

1. `rtt` times one datagram over client VM to relay and back to the same VM.
   Both ends are in one process, so it is measured against a single clock.
2. "Core" means vCPU. What that is worth in physical cores is
   [a property of the VM size](tune_linux_azure.md#6-vm-sizes-and-what-changes-with-them).
3. Mbps is IP level, which is what the kernel counts. At the audio profile it
   carries no signal Kpps does not, since at a fixed profile it is just Kpps
   times the packet size.
4. Bandwidth is not the constraint at the audio profile: 8000 sessions is around
   400 Kpps and 475 Mbps, a small fraction of the link each way, so the
   difference between IP, frame and wire accounting -- up to 25% -- is noise
   against that margin.
5. That stops holding for the video and bundled profiles. Two thousand bundled
   sessions is about 3.1 Gbps each way, half the link, and at that point the NIC
   ceiling matters and so does wire-level framing -- add 20 bytes per frame for
   preamble and inter-frame gap, which the kernel's counters do not include.
