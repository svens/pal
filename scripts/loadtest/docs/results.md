# RESULTS

The rules governing what may be recorded here are
[what makes a valid row](measure.md#7-what-makes-a-valid-row); this file is
data.

## 1. How to read a row

1. Every row is the audio profile unless its notes say otherwise, which is why
   there is no profile column.
2. Say in the notes whether a row is a ceiling or as deployed. The two are not
   comparable, for the reason in
   [what makes a valid row](measure.md#7-what-makes-a-valid-row) item 4.
3. `machine busy` is the whole machine, not the relay process.
4. `sessions/core` is the headline figure, and `core` means vCPU.
5. The CPU model travels with the row, per
   [VM sizes and what changes with them](tune_linux_azure.md#6-vm-sizes-and-what-changes-with-them)
   item 1.

## 2. Results

One row per configuration worth keeping.

| VM size | CPU model | sessions | sessions/core | machine busy | Mbps | Kpps | notes |
|---|---|---|---|---|---|---|---|
| Standard_F4s_v2 | Xeon Platinum 8272CL @ 2.60GHz | 9600 | 2400 | 88 | 568 | 480 | as deployed, untuned; 8 clients x 1200 streams, warmup 15, runtime 60; UdpRcvbufErrors +5710 at this point against the image's 1 MiB rmem_max |
| Standard_F4s_v2 | Xeon Platinum 8272CL @ 2.60GHz | 11200 | 2800 | 95 | 663 | 560 | as deployed, tuned; same deployment as the row above; UdpRcvbufErrors +17130 at this point, rmem_max 8 MiB |
| Standard_F4s_v2 | Xeon Platinum 8272CL @ 2.60GHz | 12800 | 3200 | 90 | 755 | 638 | ceiling, tuned; same deployment; security table dropped and nf_conntrack unloaded with walinuxagent stopped, both verified absent after the ladder |

## 3. Thread count

A second deployment of the same shape, so these rows are comparable with each
other and not with section 2, per
[what makes a valid row](measure.md#7-what-makes-a-valid-row) item 1. Every row
is tuned. `--threads` is the relay's own option; the default is
`hardware_concurrency`, which is 4 on this size.

### 3.1 What each thread count reaches

Ceiling rows: netfilter removed with `walinuxagent` stopped, verified absent
after each ladder.

| threads | sessions | machine busy | Kpps | Mbps | relay.cpu at the knee | physical cores used |
|---|---|---|---|---|---|---|
| 1 | 6400 | 26 | 320 | 379 | 96% of one core | 1 |
| 2 | 8000 | 50 | 400 | 474 | 187% of one core | 1 |

1. A thread count is not a core count here. The relay pins thread `i` to CPU `i`
   and steers to it with a `cpu % thread_count` reuseport filter, and CPUs 0 and
   1 are one physical core, so `--threads=2` is one core and its sibling.

       Measured: lscpu -e on the relay put CPU 0 and 1 on core 0, CPU 2 and
       3 on core 1.

2. Per physical core the rate is flat: 320 Kpps at one thread on one core, and
   638 at four threads on two. The second thread of a pair adds 80, which is
   [what a sibling is worth](tune_linux_azure.md#6-vm-sizes-and-what-changes-with-them)
   item 2 measured on this workload.
3. The knee is the thread, not the machine: at one thread the ladder ends with
   the machine three quarters idle.
4. Both knees are the rung below a failure, not a range exhausted.

       Measured: 6600 sessions at one thread put the thread at 100% of a
       core with UdpRcvbufErrors +538594 and 1% loss. 8200 at two threads
       put them at 198% together.

5. 8000 sessions at two threads is the boundary rung and it went both ways --
   failing on one ladder, passing on the next -- which is
   [repeat each point](measure.md#7-what-makes-a-valid-row) item 2 showing up
   at exactly the rung it warns about.

### 3.2 What netfilter costs

One load, 4800 sessions at 240 Kpps received, run twice per thread count: once
with the security table dropped and `nf_conntrack` unloaded, once with
`walinuxagent` restarted so it reinstalled both. Nothing else differs, so the
delta is conntrack rather than a knee that also moves with threshold noise.

| threads | machine busy, no conntrack | machine busy, conntrack | relay.cpu, no conntrack | relay.cpu, conntrack | cost |
|---|---|---|---|---|---|
| 1 | 20 | 23 | 72% | 83% | 15% |
| 2 | 27 | 33 | 101% | 124% | 22% |
| 4 | 30 | 35 | 112% | 130% | 17% |

1. Conntrack costs 15 to 22 percent of the CPU the same traffic needs without
   it, and both figures move together.
2. That is more than the 9 percent
   [the guest agent's netfilter table](tune_linux_azure.md#4-the-guest-agents-netfilter-table)
   item 2 recorded from a flame graph, which was taken at four threads and a
   higher session count.
3. Receive rate is identical across each pair to within a tenth of a Kpps, so
   the pairs differ in cost and not in what they carried.
