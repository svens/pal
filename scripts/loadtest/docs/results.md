# RESULTS

Rows follow [what makes a valid row](measure.md#7-what-makes-a-valid-row).

## 1. Setup

1. One deployment: `Standard_F4s_v2` relay on Xeon Platinum 8168 @ 2.70GHz, kernel 7.0,
   8 `Standard_F2s_v2` clients.
2. Tuned, netfilter and conntrack absent by provisioning, audio profile, default
   `--threads`.
3. Backend chosen with `kernel.io_uring_disabled`.
4. io_uring runs with `max_armed` 64 and `--poll_batch=1024`, epoll with the default
   `poll_batch`.

## 2. Results

| mode | backend | sessions | sessions/core | Kpps | machine busy | ns/packet |
|---|---|---|---|---|---|---|
| mturn | epoll | 12800 | 3200 | 639 | 94.0 | 5884 |
| mturn | io_uring | 12000 | 3000 | 600 | 88.4 | 5815 |
| turn | epoll | 6400 | 1600 | 320 | 87.4 | 10793 |
| turn | io_uring | 7200 | 1800 | 360 | 88.7 | 9763 |

Each failing rung ran at 97 to 100% machine busy.

## 3. Costs

### 3.1 Mode

turn / mturn ns/packet: 1.8 on epoll, 1.7 on io_uring.

### 3.2 Backend

| mode | sessions | ns/packet, epoll | ns/packet, io_uring | io_uring / epoll |
|---|---|---|---|---|
| mturn | 12000 | 5746 | 5815 | 1.01 |
| turn | 6400 | 10793 | 9849 | 0.91 |

### 3.3 io_uring knobs

Each row lists what differs from `max_armed` 8 with default `poll_batch`,
`submission_depth` and `completion_depth`; cells are the last passing rung.

| change | mturn | turn |
|---|---|---|
| none | 11200 | 6400 |
| `poll_batch` 1024 | 11200 | |
| `max_armed` 64 | 12000 | |
| `max_armed` 256 | 12000 | |
| `max_armed` 256, `submission_depth` 2048, `completion_depth` 4096 | 12000 | 6400 |
| `max_armed` 64, `poll_batch` 1024 | 12000 | 7200 |

At `max_armed` 8 the mturn ladder failed below CPU saturation.

    Measured: at max_armed 8 the mturn failing rung held relay.cpu 303% at 88.5%
    machine busy, with UdpRcvbufErrors +21337.
