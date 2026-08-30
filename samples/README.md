# samples

UDP relay sample applications: `pal::async` API exercise and scalability/profiling
harness. Servers model a TURN-style relay (packet in, session lookup, packet out);
`loadtest` drives them and measures. All knobs and defaults are defined in each
binary's `config` struct (printed at startup).

## Protocol

Wire structures in `common/relay.hpp` (host byte order; measurement assumes
loadtester-side processes share one machine, servers may be remote):

1. Client sends `relay::registration` to the server's client port; the server maps
   session id to the datagram's source endpoint and responds with `relay::allocation`
   naming the peer-side port for this session.
2. Peer sends `relay::header`-prefixed datagrams to the allocated port; the server
   forwards each datagram unchanged to the registered client endpoint.

## Binaries

- `udp_relay`: the relay under test, its shape selected by `--mode`.
- `--mode=mturn`: MTURN-like relay -- all sessions share one client-side and one
  peer-side port. Thread-per-loop with `SO_REUSEPORT` sharding; on Linux, incoming
  load is steered with `SO_ATTACH_REUSEPORT_CBPF` (CPU id modulo thread count, which
  assumes NIC RSS queue-to-CPU mapping and pinned threads). Session lookup is a
  two-tier map: mutex-protected global for registration and first peer packet,
  lock-free thread-local afterwards.
- `loadtest`: synchronous traffic generator hosting both ends of every session:
  per-stream sender sockets (peer role) and one shared receiver socket (client
  role) drained by one receiver thread. Traffic is one-way peer-to-client; sender
  and receiver share a clock, so reported `rtt` is exact one-way latency, not a
  round trip. Pacing is a self-correcting 1 ms token bucket; loss is detected by
  send-window eviction, so in-flight counts beyond the window misreport as loss;
  the reported `jitter` is an RFC 3550-style EWMA over every stream at once, so
  it is the spread of the arrival stream as a whole rather than per-session jitter,
  and is reported rather than gated. `--max-loss` / `--max-rtt-ms` turn threshold
  violations into a nonzero exit code, and `--warmup` seconds of startup transient
  are reported but not judged, on top of the `--runtime` that is. Scale by
  launching more loadtest processes (per machine and across machines); degraded
  stats in any of them signal the server-side limit. Processes are unpinned by
  design so concurrent instances spread across cores.

## Running

    udp_relay --mode=mturn --threads=4
    loadtest --server=127.0.0.1 --streams=100 --runtime=60

Platform notes:

- Measurements are meaningful on Linux; macOS runs functionally (no thread pinning,
  no CBPF steering, `SO_REUSEPORT` distribution differs). Windows waits on the IOCP
  socket backend.
- On loopback the ingress distribution collapses to one listener per loadtest
  process: softirq processing runs on the sending CPU, so CPU-keyed CBPF steering
  sees a single CPU. Cross-listener distribution requires a real NIC with RSS.
- fd limits, NIC/IRQ tuning and cluster deployment are external concerns; see
  `scripts/loadtest/` for host tuning, deployment and measurement.
