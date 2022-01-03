# UDP throughput tuning


## Assumptions
* different applications' typical packet size differs. When designing
  protocol, try to lower UDP packet header vs payload ratio to improve
  bandwidth utilization (bytes per second, BPS). Tuning this is application
  domain
* increasing number of packets (packets per second, PPS) is harder because it
  requires more frequent syscalls with it's overhead
* here we focus on typical VoIP usage: 59kbps with 200B packets


## Testing
For testing we use UDP relay that mimics [TURN](https://tools.ietf.org/html/rfc5766) server:
* client connects to relay port 3478 and registers itself using 8-byte random integer
* peer sends payload to port 3479. Payload must be prefixed with same 8B value
  that client used to register session earlier
* relay forwards received peer data to client endpoint

In samples/ directory there are multiple different implementations that
experiment with different locking/threading models to compare PPS throughput

For testing we use virtual machines [cluster](https://github.com/svens/bicep)
in Azure. It contains 6 client/peer machines and single relay. All machines
are configured with [accelerated networking](https://azure.microsoft.com/en-us/blog/maximize-your-vm-s-performance-with-accelerated-networking-now-generally-available-for-both-windows-and-linux/).
Chosen relay VM sizes
[Fsv2](https://docs.microsoft.com/en-us/azure/virtual-machines/fsv2-series)
support different networking bandwidths. In tests we use `F4s_v2` for relay
and `DS2_v2` for client/peer VM.


## OS level tuning
See:
* https://www.kernel.org/doc/Documentation/networking/scaling.txt
* https://blog.cloudflare.com/how-to-receive-a-million-packets/
* https://events.static.linuxfound.org/sites/events/files/slides/LinuxConJapan2016_makita_160714.pdf
* https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/
* https://blog.packagecloud.io/eng/2017/02/06/monitoring-tuning-linux-networking-stack-sending-data/

Implementations of used configurations can be found in scripts in current directory
* `01_irq`: configure IRQ balancer (RSS <-> CPU mapping)
* `02_sysctl`: configure kernel networking stack (queue sizes, etc)
* `03_ethtool`: configure NIC (assigning packets to queues, etc)
* `04_rps`: configure RPS (Receive Packet Steering)
* `05_xps`: configure XPS (Transmit Packet Steering)


## Application level tuning
* Create per thread listener socket with `SO_REUSEPORT`
* Assign specific remote endpoint to specific CPU/core (`SO_ATTACH_REUSEPORT_CBPF` or `SO_ATTACH_REUSEPORT_EBPF`)
* Increase socket buffer sizes (`SO_RCVBUF` and `SO_SNDBUF`)
* Pin thread to specific CPU/core (`sched_setaffinity`)
* Minimize inter-thread synchronization (avoid all or use `std::shared_mutex`)


## Results

### `udp_relay_server_global_map`
|VM		|OS		|Sessions	|CPU load	|Mbps	|Kpps	|Notes|
|---------------|---------------|---------------|---------------|-------|-------|-----|
|`F4s_v2`	|Windows	|6000		|58%		|354	|218	|2 cores at 100%|
|`F4s_v2`	|Linux		|9600		|91%		|564	|354	|Mellanox v4|
|`F4s_v2`	|Linux		|12000		|90%		|708	|442	|Mellanox v5|
|`DS2_v2`	|Windows	|4700		|100%		|276	|173	||
|`DS2_v2`	|Linux		|7000		|76%		|412	|258	|Mellanox v5|


### `udp_relay_server_local_map`
|VM		|OS		|Sessions	|CPU load	|Mbps	|Kpps	|Notes|
|---------------|---------------|---------------|---------------|-------|-------|-----|
|`F4s_v2`	|Windows	|6000		|54%		|354	|218	|2 cores at 100%|
|`F4s_v2`	|Linux		|9600		|90%		|564	|354	|Mellanox v4|
|`F4s_v2`	|Linux		|12000		|90%		|708	|442	|Mellanox v5|
|`DS2_v2`	|Windows	|4700		|97%		|276	|173	||
|`DS2_v2`	|Linux		|7000		|74%		|412	|258	|Mellanox v5|


### `udp_relay_server_multi_send`
|VM		|OS		|Sessions	|CPU load	|Mbps	|Kpps	|Notes|
|---------------|---------------|---------------|---------------|-------|-------|-----|
|`F4s_v2`	|Linux		|15000		|92%		|882	|553	|Mellanox v5|
|`DS2_v2`	|Linux		|8700		|85%		|513	|320	|Mellanox v5|


### `NA/relay_server_udp_mt`
(internal)
|VM		|OS		|Sessions	|CPU load	|Mbps	|Kpps	|Notes|
|---------------|---------------|---------------|---------------|-------|-------|-----|
|`F4s_v2`	|Linux		|10200		|75%		|600	|376	|Mellanox v5|
