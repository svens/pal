# TUNE-LINUX-AZURE

## 1. Accelerated networking presents two interfaces

1. There are two network interfaces where a reader expects one.

   1. A synthetic netvsc device, conventionally `eth0`. This is what the application
      binds to and what routing uses.
   2. A Mellanox virtual function, named `enP<n>s1` and driven by `mlx5_core`. This is
      where the hardware queues actually live.

2. Ring sizes, interrupt coalescing, receive flow hashing and XPS all have to reach
   the VF. Sent to the synthetic device they succeed and change nothing: no error
   anywhere, and no counter moves.
3. Check which interface a script chose before investigating anything else.

## 2. Which interface a knob reaches

1. `get_nic` in `host/_common.sh` picks the VF by elimination: it skips `lo`, `eth0`
   and the `docker*` and `veth*` families, and takes the first interface left.

## 3. Image defaults that differ from stock

1. The image ships `net.core.rmem_max` at 1 MiB and `net.core.wmem_max` at 4 MiB, per
   [socket buffer ceilings](tune_linux.md#62-socket-buffer-ceilings).
2. The receive ceiling sits below what the application requests and the send ceiling
   above it. So on a machine where the tuning has not been applied, the receive side
   runs at half the depth its own configuration states and the send side is untouched --
   an asymmetry nothing in the application reports.

       Measured: udp_relay_mturn on the shipped image held rb 2097152 --
       exactly twice the 1 MiB ceiling -- while tb 4194304 was twice the 2 MiB
       request.

3. What the other stages find on this image.

   | knob | as shipped |
   |---|---|
   | receive flow hashing | already `sdfn` |
   | ring sizes | 1024, of an 8192 maximum |
   | interrupt coalescing | adaptive on, 8 us / 128 frames |
   | IRQ affinity | already one queue per CPU |
   | XPS map | already one queue per CPU |
   | irqbalance | not installed |
   | cpuidle | no driver, no states exposed |
   | cpufreq | absent |

## 4. The guest agent's netfilter table

1. The guest agent installs a `security` table whose rules match only TCP to the
   wireserver at 168.63.129.16. Nothing in it concerns UDP.
2. Every UDP packet pays for it anyway. Conntrack tracked every relay flow, and each
   packet paid a hash lookup and a chain walk for rules that could never match it.

       Measured: perf on the running relay put __nf_conntrack_find_get top
       at 5.81%, with nft_do_chain a further 3.01%. The module held 10528
       tracked flows. Exempting UDP with two raw-table NOTRACK rules
       recovered about 6%: relay cpu 303% to 275% at 9600 sessions.

3. The VMs are provisioned without either. `azure/template/cloud-init.yaml` removes the
   agent package, deletes the table it installed before that point, and leaves a
   `modprobe.d` file denying `nf_conntrack` the load. The template sets
   `provisionVMAgent: false`, so the platform stops expecting an agent.
4. The package goes rather than the service, because the agent also ships
   `walinuxagent-network-setup.service`, which installs the table before
   `network-pre.target`.
5. Measuring that cost again means putting both back and rebooting.

       sudo rm /etc/modprobe.d/90-loadtest.conf
       sudo apt -y install walinuxagent
       sudo reboot

   Read `nft list ruleset` and `lsmod` before trusting a row either way.

## 5. VM sizes and what changes with them

1. One VM size is served from more than one host generation, and sessions per core is
   not comparable across them; `counters` emits the CPU model.
2. On `Standard_F4s_v2`, four vCPUs are two physical cores with SMT, so a per-core
   figure is not a per-physical-core one.
