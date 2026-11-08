# RESULTS

Rows follow [what makes a valid row](measure.md#7-what-makes-a-valid-row).

## 1. Setup

1. One deployment: `Standard_F4s_v2` relay on Xeon Platinum 8168 @ 2.70GHz, kernel 7.0,
   8 `Standard_F2s_v2` clients.
2. Tuned, netfilter and conntrack absent by provisioning, audio profile, default
   `--threads`, epoll backend.
3. Each row is the median of three ladders.

## 2. Results

| mode | sessions | sessions/core | Kpps | machine busy | ns/packet |
|---|---|---|---|---|---|
| mturn | 14400 | 3600 | 720 | 91.2 | 5058 |
| turn | 8800 | 2200 | 440 | 95.4 | 8660 |

Each failing rung ran at 91 to 100% machine busy.

## 3. Costs

turn / mturn ns/packet: 1.7.
