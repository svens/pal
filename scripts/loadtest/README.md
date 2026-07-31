# loadtest

Host tuning, cluster deployment and measurement for a UDP relay under synthetic
load. A relay runs on one VM, load generators on several others, and the relay's
own kernel counters are read alongside what the generators report.

## 1. Documents

| document | holds |
|---|---|
| [cluster](docs/cluster.md) | getting machines, access, cost and lifecycle |
| [measure](docs/measure.md) | running a ladder and reading what comes back |
| [tune_linux](docs/tune_linux.md) | every host knob, what it does, and what it was worth |
| [tune_linux_azure](docs/tune_linux_azure.md) | what an Azure guest changes about the above |
| [results](docs/results.md) | measurements, and what no ladder has settled yet |

## 2. Quickstart

Every action has a ninja target, so `ninja <action>` runs one and `ninja help`
lists them all with the one-line description each script carries. Ninja cannot
forward variables, so an action that needs an override is invoked as `cmake
-D<name>=<value> -P azure/<action>.cmake`.

A session starts with `az login`, then `deploy`, `ship`, `tune`, and `run` a
point or `sweep` a ladder, ending with `destroy`. The actions, their ninja
targets and the form that takes overrides are
[cluster shape](docs/cluster.md#2-cluster-shape).

Two things that are easy to get wrong:

1. `destroy` is the normal end of a session, not a rare event, per
   [cost and lifecycle](docs/cluster.md#6-cost-and-lifecycle).
2. Tuning does not survive a reboot, a deallocate/start cycle or a redeploy, per
   [using these scripts](docs/tune_linux.md#1-using-these-scripts).

## 3. Layout

Two kinds of script, split by where they run.

1. `azure/*.cmake` runs on your workstation and drives `az`. CMake rather than
   shell because the workstation's OS varies.
2. `host/*` and `perf/*` run on a VM, as root, against a Linux kernel. Shell,
   because there is no cross-platform version of writing to `/sys`.

The rest of the tree:

    cluster.conf     cluster shape and run defaults, all overridable
    build.ninja      convenience wrapper over the cmake actions
    docs/            the documents above

## 4. References

1. <https://www.kernel.org/doc/Documentation/networking/scaling.txt>
2. <https://blog.cloudflare.com/how-to-receive-a-million-packets/>
3. <https://blog.packagecloud.io/eng/2016/06/22/monitoring-tuning-linux-networking-stack-receiving-data/>
4. <https://blog.packagecloud.io/eng/2017/02/06/monitoring-tuning-linux-networking-stack-sending-data/>
