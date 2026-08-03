# TUNE-LINUX-AZURE

What an Azure Linux guest does to the tuning and the measurement. The knobs
themselves are in [tune_linux.md](tune_linux.md).

Everything here is a property of the platform rather than of Linux.

## 1. Accelerated networking presents two interfaces

1. There are two network interfaces where a reader expects one.

   1. A synthetic netvsc device, conventionally `eth0`. This is what the
      application binds to and what routing uses.
   2. A Mellanox virtual function, named `enP<n>s1` and driven by `mlx5_core`.
      This is where the hardware queues actually live.

2. Ring sizes, interrupt coalescing, receive flow hashing and XPS all have to
   reach the VF. Sent to the synthetic device they succeed and change nothing:
   no error anywhere, and no counter moves.
3. Check which interface a script chose before investigating anything else. If
   ethtool or IRQ tuning appears to have done nothing, this is the first
   candidate and costs one line of output to rule out.
4. Without accelerated networking there is one interface and it is the right
   one, so none of this section applies.

## 2. Which interface a knob reaches

1. `get_nic` in `host/_common.sh` picks the VF by elimination: it skips `lo`,
   `eth0` and the `docker*` and `veth*` families, and takes the first interface
   left. On bare metal that leaves the single real interface, which is also
   correct.
2. `NIC=<name>` overrides it. Use that rather than editing the heuristic when a
   machine names things unusually.
3. IRQ discovery reads sysfs first, at `/sys/class/net/<nic>/device/msi_irqs`,
   and only falls back to searching `/proc/interrupts` by name. The order
   matters here specifically: `/proc/interrupts` names an interrupt after
   whatever requested it, and `mlx5` names them after the PCI device, so a name
   search finds nothing on the one NIC this suite exists to tune.
4. A synthetic device with no VF has no interrupts of its own. That is reported
   as an error rather than as success with nothing to do, and it is itself the
   answer to why [interrupt affinity](tune_linux.md#5-interrupt-affinity-03_irq)
   did not help.

## 3. Image defaults that differ from stock

1. The image ships `net.core.rmem_max` at 1 MiB and `net.core.wmem_max` at 4
   MiB, which are ceilings and not sizes, per
   [socket buffer ceilings](tune_linux.md#72-socket-buffer-ceilings).
2. The receive ceiling sits below what the application requests and the send
   ceiling above it. So on a machine where the tuning has not been applied, the
   receive side runs at half the depth its own configuration states and the send
   side is untouched -- an asymmetry nothing in the application reports.
3. This is the measurement that established the ceilings matter at all.

       Measured: an earlier configuration requesting 16 MiB per direction
       against these same ceilings. The receive side was clamped to a
       sixteenth of the request. The clamp is silent in both cases; only
       the magnitude differed.

4. The clamp has been read off a running relay.

       Measured: udp_relay_mturn on the shipped image held rb 2097152 --
       exactly twice the 1 MiB ceiling, which is the clamp signature -- while
       tb 4194304 sat below the 4 MiB send ceiling. The send side got what it
       asked for and the receive side did not, in one reading.

5. What the other stages find on this image.

   | knob | as shipped |
   |---|---|
   | receive flow hashing | already `sdfn` |
   | ring sizes | 1024, of an 8192 maximum |
   | interrupt coalescing | adaptive on, 8 us / 128 frames |
   | IRQ affinity | already one queue per CPU |
   | XPS map | already one queue per CPU |
   | irqbalance | not installed |
   | cpuidle | no driver, no states exposed |

6. Only ring sizes and interrupt coalescing change anything, so an untuned run
   differs from a tuned one in ring depth, moderation window and socket
   ceilings, and nowhere else.
7. Anything read here is a property of one image at one time. Re-read it rather
   than trusting this section after an image change, which `05_sysctl show`
   prints alongside what a running process actually holds.

## 4. The guest agent's netfilter table

1. The guest agent installs a `security` table whose rules match only TCP to the
   wireserver at 168.63.129.16. Nothing in it concerns UDP.
2. Every UDP packet pays for it anyway. Conntrack tracked every relay flow, and
   each packet paid a hash lookup and a chain walk for rules that could never
   match it.

       Measured: perf on the running relay put __nf_conntrack_find_get top
       at 5.81%, with nft_do_chain a further 3.01%. The module held 10528
       tracked flows. Exempting UDP with two raw-table NOTRACK rules
       recovered about 6%: relay cpu 303% to 275% at 9600 sessions.
       Dropping the table and unloading nf_conntrack would take the chain
       traversal as well, roughly 9% together.

3. The VMs are provisioned without either. `azure/template/cloud-init.yaml`
   removes the agent package, deletes the table it installed before that point,
   and leaves a `modprobe.d` file denying `nf_conntrack` the load. The template
   sets `provisionVMAgent: false`, so the platform stops expecting an agent.
4. The package goes rather than the service, because the agent also ships
   `walinuxagent-network-setup.service`, which installs the table before
   `network-pre.target`.
5. A ladder here measures the relay and `pal`, not a deployment. Production
   nodes running Kubernetes have conntrack and cannot give it up, because
   kube-proxy implements Service DNAT with it, so a capacity claim for such a
   node subtracts what item 6 records.
6. What the table costs at a fixed load, rather than at a knee, is
   [what netfilter costs](results.md#32-what-netfilter-costs).
7. Measuring that cost again means putting both back and rebooting.

       sudo rm /etc/modprobe.d/90-loadtest.conf
       sudo apt -y install walinuxagent
       sudo reboot

   Read `nft list ruleset` and `lsmod` before trusting a row either way.

## 5. What the hypervisor owns

1. Clock, idle depth and the host's own scheduling are not the guest's. cpufreq
   is usually absent entirely rather than present and fixed, and idle states are
   the hypervisor's to choose, so
   [clock and idle states](tune_linux.md#32-clock-and-idle-states-01_cpu) finds
   nothing to set here.
2. What the hypervisor took from the guest is visible as `machine.steal`.
   Sustained steal means the run measured the neighbours rather than the
   service; [measure.md](measure.md) treats it as a reason to discard a run
   rather than as a knob.

## 6. VM sizes and what changes with them

1. One VM size is served from more than one host generation, and sessions per
   core is not comparable across them. The row's own hardware therefore travels
   with every measurement rather than being remembered separately: `counters`
   emits the CPU model it saw.
2. The relay size measured here is `Standard_F4s_v2`. Its four vCPUs are two
   physical cores with SMT, and the second thread of a pair contributes far less
   than the first on a syscall-bound workload, so a per-core figure is not a
   per-physical-core one.
3. Accelerated networking is what presents the VF that
   [two interfaces](#1-accelerated-networking-presents-two-interfaces)
   describes. A size or a deployment without it has one interface, and the NIC
   knobs reach it directly.
4. Nothing in this file is applied to the client VMs, per
   [building and shipping](cluster.md#4-building-and-shipping) item 5.
