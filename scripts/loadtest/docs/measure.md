# MEASURE

## 1. The oracle

1. A run is `warmup` seconds followed by `runtime` seconds of measurement.
   Thresholds apply only once the second phase starts.
2. A violation latches for the whole process, so without the split the oracle
   reports the point where startup crossed a threshold rather than the point where
   the service did.

       Measured: at 9000 sessions the first intervals reported tens of
       milliseconds of rtt where the settled run reported about one.

## 2. Traffic profiles

1. `kbps` and `payload` are a pair: the generator paces at `kbps * 125 / payload`
   packets per second. `payload` is the whole datagram, so it covers RTP and SRTP
   overhead as well as media.

   | profile | kbps | payload | pps | models |
   |---|---|---|---|---|
   | audio | 48 | 120 | 50 | Opus, 20 ms packetization |
   | video | 1500 | 1200 | 156 | 720p 1:1, packets sized to the MTU |
   | bundled | 1550 | 940 | 206 | audio and video muxed onto one allocation |

2. Audio is the default.
3. The other two put several times the packet rate on each client, so one generator
   process sustains proportionally fewer streams.
4. Reach a higher session count by adding client VMs rather than load per VM.
   Otherwise the sweep finds the client's limit and reports it as the relay's.

## 3. One point: run

    ninja run
    cmake -Dstreams=500 -Druntime=120 -P azure/run.cmake

1. `run` starts the generator on every client VM at once, waits for them, and
   prints the relay-side counter deltas for the same window.
2. It does not start the relay; `start-relay` puts it in a tmux session beside a
   process monitor and a counter watch.
3. The relay-side counters bracket the measured window, shortened by the time it
   took to launch the clients. Each client keeps its own clock, so only between the
   last one leaving warm-up and the first one finishing is every client actually
   sending.
4. Read the launch skew off the run log: rows above `running` are the transient, and
   a client whose rows stop before the window ends was launched too late.

## 4. A ladder: sweep

1. `sweep` ramps `streams` until a run fails and reports the last passing point.
2. `sweep` owns the relay rather than watching one. It refuses to start while any
   relay is running, restarts it between steps, and stops it at the end, so each
   rung starts from an empty relay.
3. There is no relay afterwards, and a following `run` will ask for one. An aborted
   sweep is the opposite case: its relay survives, and the next sweep refuses until
   `stop-relay` clears it.
4. Restarted steps run detached rather than under tmux, each writing
   `relay-<streams>.log` on the relay. That is where a step which killed the relay
   explains itself.
5. Each rung costs `warmup` plus `runtime` plus roughly fifteen seconds of launch
   and polling, and the ladder walks `sweep_start` to `sweep_max` in `sweep_step`
   increments. Deploy and provisioning are several minutes on top.

## 5. Relay-side counters

`host/counters` documents each counter; `run` and `sweep` print its `delta` rows.

1. **The run did not measure the service.** `softnet.dropped`, the NIC-level discard
   counters, and `cpu steal_pct` all mean the load never
   reached the relay, or the hypervisor took the CPU needed to process it. `run` and
   `sweep` warn on them, and `sweep` repeats the warning at the end. Neither aborts.
2. **The service reached its limit.** `UdpRcvbufErrors` means the relay was not
   draining its socket fast enough. That is what the run is for, so it is not
   treated as a fault. The exception is a ceiling having clamped the request below
   what the process asked for, which turns it back into a host problem;
   [socket buffer ceilings](tune_linux.md#62-socket-buffer-ceilings) prints both
   halves. Under `profile`, some of it is the capture starting, per
   [capture](profile.md#3-capture).
3. **A tuning signal, not a verdict.** `softnet.squeezed`; see
   [backlog and budget](tune_linux.md#61-backlog-and-budget).

## 6. CPU: process versus machine

1. Two CPU figures are reported, and the gap between them is the useful part.

   1. `relay cpu_seconds` and `relay core_pct` are the relay process alone, from
      `/proc/<pid>/stat`. They cannot see softirq time, and softirq is where much of
      a relay's cost lives, growing with packet rate.
   2. `cpu busy_pct` is the whole machine from `/proc/stat`, with the softirq share
      beside it as `cpu softirq_pct`.

2. Size a fleet from `cpu busy_pct`, which is what a results row records.

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

   Run the pair close together.
2. Repeat each point and take the median.
3. Rows are of two kinds and comparing across them is meaningless.

   1. **Ceiling.** Netfilter absent, which is how the VMs are provisioned. This
      bounds what the relay reaches with nothing else in the way, and is the
      starting point for profiling the relay itself.
   2. **As deployed.** Netfilter restored by hand before the sweep, as the
      production nodes have it.

   Which kind a row is has to be read off the host rather than assumed, per
   [the guest agent's netfilter table](tune_linux_azure.md#4-the-guest-agents-netfilter-table).
4. The headline figure is sessions per core at a fixed loss and rtt threshold.

## 8. Caveats

1. `rtt` times one datagram over client VM to relay and back to the same VM. Both
   ends are in one process, so it is measured against a single clock.
2. "Core" means vCPU. What that is worth in physical cores is
   [a property of the VM size](tune_linux_azure.md#5-vm-sizes-and-what-changes-with-them).
3. Mbps is IP level, which is what the kernel counts.
4. Bandwidth is not the constraint at the audio profile: 8000 sessions is around 400
   Kpps and 475 Mbps, a small fraction of the link each way, so the difference
   between IP, frame and wire accounting -- up to 25% -- is noise against that
   margin.
5. That stops holding for the video and bundled profiles. Two thousand bundled
   sessions is about 3.1 Gbps each way, half the link, and at that point the NIC
   ceiling matters and so does wire-level framing -- add 20 bytes per frame for
   preamble and inter-frame gap, which the kernel's counters do not include.
