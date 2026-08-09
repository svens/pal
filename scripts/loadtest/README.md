# loadtest

Host tuning, cluster deployment and measurement for a UDP relay under synthetic
load. A relay runs on one VM, load generators on several others, and the relay's
own kernel counters are read alongside what the generators report.

## 1. Documents

| document | holds |
|---|---|
| [cluster](docs/cluster.md) | getting machines, access, cost and lifecycle |
| [measure](docs/measure.md) | running a ladder and reading what comes back |
| [profile](docs/profile.md) | where the time inside a packet goes |
| [tune_linux](docs/tune_linux.md) | every host knob, what it does, and what it was worth |
| [tune_linux_azure](docs/tune_linux_azure.md) | what an Azure guest changes about the above |
| [results](docs/results.md) | measurements, and what no ladder has settled yet |

## 2. Quickstart

A session starts with `az login`, then `deploy`, `ship`, `tune`, and `run` a point
or `sweep` a ladder, ending with `destroy`.

Every action has a ninja target, so `ninja <action>` runs one and `ninja help`
lists them all with the one-line description each script carries.

    ninja deploy
    cmake -Dclient_count=2 -P azure/deploy.cmake

Both forms exist because ninja cannot forward variables: `ninja <action>` always
uses what `cluster.conf` holds, and the `cmake -P` form is the one that takes
overrides.

`destroy` is the normal end of a session rather than a rare event, per
[cost and lifecycle](docs/cluster.md#6-cost-and-lifecycle).

## 3. Layout

Three kinds of script, split by where they run.

1. `azure/*.cmake` runs on your workstation and drives `az`. CMake rather than
   shell because the workstation's OS varies.
2. `host/*` and `perf/*` run on a VM, as root, against a Linux kernel. Shell,
   because there is no cross-platform version of writing to `/sys`.
3. `tools/*` runs on your workstation over what a VM sent back.

The rest of the tree:

    cluster.conf     cluster shape and run defaults, all overridable
    build.ninja      convenience wrapper over the cmake actions
    docs/            the documents above
    .profile/        capture artifacts, gitignored

## 4. References

1. <https://www.kernel.org/doc/Documentation/networking/scaling.txt>
2. <https://blog.cloudflare.com/how-to-receive-a-million-packets/>
3. <https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/>
4. <https://blog.packagecloud.io/eng/2017/02/06/monitoring-tuning-linux-networking-stack-sending-data/>
