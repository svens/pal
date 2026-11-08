# samples

UDP relay sample applications: `pal::async` API exercise and scalability/profiling
harness. Servers model a TURN-style relay (packet in, session lookup, packet out);
`loadtest` drives them and measures. All knobs and defaults are defined in each
binary's `config` struct (printed at startup).

## Protocol

Wire structures in `common/relay.hpp` (host byte order; measurement assumes
loadtester-side processes share one machine, servers may be remote):

1. Client sends `relay::registration` to the server's client port; the server maps
   session id to the datagram's source address paired with the port the registration
   names, and responds with `relay::allocation` naming the peer-side port for this
   session. Only the port is taken from the wire, so a registration can redirect
   delivery to another port on its own sender's address and nowhere else.
2. Peer sends `relay::header`-prefixed datagrams to the allocated port; the server
   forwards each datagram unchanged to the registered client endpoint.

## Binaries

- `udp_relay --mode=mturn`: MTURN-like relay, every session on one shared
  client-side and one shared peer-side port.
  1. `--peer.ports` names that single peer-side port.
  2. Thread-per-loop, sharded with `SO_REUSEPORT`.
  3. On Linux ingress is steered by `SO_ATTACH_REUSEPORT_CBPF`, CPU id modulo
     thread count, which assumes NIC RSS queue-to-CPU mapping and pinned
     threads.
  4. Session lookup is a two-tier map: mutex-protected global for registration
     and the first peer packet, lock-free thread-local afterwards.
- `udp_relay --mode=turn`: IETF TURN-like relay, a peer socket per session.
  1. `--peer.ports` is a range and is required; each registration binds one port
     from it, on the thread that took the registration.
  2. The client port stays shared and sharded, as in mturn.
  3. The session's own socket is the session key, so the peer path has no
     lookup.
  4. The range is split into a contiguous block per thread.
  5. Forwards leave on the shared client socket, since the client expects them
     from the client port.
  6. Allocations are held until exit and each peer socket takes a 256 KiB
     receive buffer, so the range and the fd limit bound the session count.
- `loadtest`: synchronous traffic generator hosting both ends of every session.
  1. Per-stream sender sockets (peer role) and one shared receiver socket
     (client role) drained by one receiver thread.
  2. Each stream registers from its own sender socket, naming the shared
     receiver's port, so the relay sees one registration flow per session
     instead of one per process. `--mode=turn` places a session on the thread
     that took its registration, so with one flow per process the placement is
     as coarse as the process count.
  3. Traffic is one-way peer-to-client; sender and receiver share a clock, so
     reported `rtt` is exact one-way latency, not a round trip.
  4. Pacing is a self-correcting 1 ms token bucket.
  5. Loss is detected by send-window eviction, so in-flight counts beyond the
     window misreport as loss.
  6. `jitter` is an RFC 3550-style EWMA over every stream at once, the spread of
     the arrival stream as a whole rather than per-session, and is reported
     rather than gated.
  7. `--max-loss` / `--max-rtt-ms` turn threshold violations into a nonzero exit
     code; `--warmup` seconds of startup transient are reported but not judged,
     on top of the `--runtime` that is.
  8. Scale by launching more loadtest processes, per machine and across
     machines; degraded stats in any of them signal the server-side limit.
  9. Processes are unpinned by design so concurrent instances spread across
     cores.

## Running

    udp_relay --mode=mturn --threads=4
    udp_relay --mode=turn --threads=4 --peer.ports=40000-65535
    loadtest --server=127.0.0.1 --streams=100 --runtime=60

Platform notes:

- Measurements are meaningful on Linux; macOS runs functionally (no thread pinning,
  no CBPF steering, `SO_REUSEPORT` distribution differs). Windows waits on the IOCP
  socket backend.
- On loopback the ingress distribution collapses to one listener per loadtest
  process: softirq processing runs on the sending CPU, so CPU-keyed CBPF steering
  sees a single CPU. Cross-listener distribution requires a real NIC with RSS.
- `--mode=turn` binds explicit ports, which may overlap
  `net.ipv4.ip_local_port_range`: a port an outbound socket holds is skipped and
  the next one tried, and an outbound socket that wants an allocated port gets
  another. What keeps the host supplied is the part of the ephemeral range below
  the allocation range staying exclusive to it.
- An exhausted range refuses the registration, and the client sees the refusal as an
  unanswered handshake.
- fd limits, NIC/IRQ tuning and cluster deployment are external concerns; see
  `scripts/loadtest/` for host tuning, deployment and measurement.
