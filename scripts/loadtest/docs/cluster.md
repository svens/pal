# CLUSTER

How to get machines and how to stop paying for them. Running a ladder on them is
[measure.md](measure.md); tuning them is [tune_linux.md](tune_linux.md).

Everything here drives `az` from your workstation through `azure/*.cmake`. It
assumes an Azure subscription you control.

## 1. Subscription, login and quota

1. Log in first: `az login`, or `az login --use-device-code` from a machine
   without a browser.
2. Confirm which subscription `az` is pointed at before deploying. Every
   subscription holds its resource group under the same name, so pointing at the
   wrong one produces a second cluster rather than an error.
3. Check the regional vCPU quota for the VM families in `cluster.conf` before the
   first deploy, against the sizes and `client_count` it sets. A full cluster can
   sit at the cap, leaving no room for anything else in that region.
4. Quota is per family and per region. A size that is unrestricted on one
   subscription may be zone-restricted or unavailable on another, which surfaces
   at deploy time rather than at validate time.

## 2. Cluster shape

1. `cluster.conf` holds the cluster shape and the run defaults, and
   [README.md](../README.md) has the two invocation forms. (XXX: no outdateable 'two')
2. `relay-linux` is the relay under test and `loadtest-1..N` the load generators.
   Their private addresses are fixed, so `--server` values and shell history
   survive a stop/start cycle; `azure/template/cluster.bicep` assigns them.
3. `validate` and `show` read the cluster and leave it alone; `config` and
   `profile-clear` work from local files alone.

## 3. Deploy

1. `deploy` is several minutes, dominated by the image upgrade and its reboot. It
   then polls `cloud-init status` on each Linux VM until all report done, or gives
   up at its cap and names why each one is still pending. No individual failure
   aborts the wait early, so a genuinely broken deploy costs the full cap.
2. Azure keeps the subscription-scoped deployment record after `destroy`, so
   querying it by name returns the *last* deploy's outputs whether or not that
   cluster still exists. Use `ninja show`, which reads the resources.
3. Host tuning does not survive a reboot, a deallocate/start cycle or a redeploy.
   Re-apply it after any of them, per
   [using these scripts](tune_linux.md#1-using-these-scripts).

## 4. Building and shipping

1. Nothing is built on the cluster; `ship` copies binaries onto it. The template
   hardcodes the image, so the build host's glibc and libstdc++ must be no newer
   than that image's. Building on the same release is one way, linking statically
   another.
2. The relay and the load generator are built by the containing project in a
   release configuration, and are not part of a default build. `ship` looks in a
   default build directory, names the exact preset and build command when the
   binaries are not there, and takes `-Dbuild_dir=<path>` to look elsewhere.
3. `ship` copies the relay binary, `host/` and `perf/` to the relay, and the
   generator plus `host/` to each client. It also copies `~/.tmux.conf` when you
   have one.
4. Re-shipping while the relay is running is safe. The binary is unlinked rather
   than overwritten, so the running process keeps the old inode and carries on,
   and the next start picks up the new file.
5. Clients are left untuned, so a client that becomes the bottleneck is visible
   rather than tuned away.

## 5. Access

1. `deploy` generates a passphraseless keypair per cluster in `.ssh/` beside these
   scripts, not in `~/.ssh`, and installs the public half on the Linux VMs. No
   personal key reaches a throwaway VM, and a leaked one grants nothing once the
   VMs are gone. There is no password login.
2. `deploy` also writes `.ssh/local.conf`, which points `IdentityFile` at that
   key, so reach a VM with `ssh -F .ssh/local.conf relay-linux`.
3. `-Dssh_public_key_file=<path>` substitutes your own key instead, in which case
   ssh falls back to its usual identity search. `-Dssh_public_key_text=<key>`
   passes one inline.
4. Control sockets live under `$TMPDIR/<resource group>` rather than beside the
   checkout, with `-Dcontrol_dir=<path>` as the escape hatch. A unix socket path is
   capped at 104 bytes and ssh binds `ControlPath` under a temporary name that
   consumes 17 of them, so a path beside a deep checkout silently fails to
   multiplex.

## 6. Cost and lifecycle

1. There is no auto-shutdown. A cluster left running bills until something stops
   it.
2. `deallocate` stops compute, but disks and static public addresses keep
   charging, so a stopped cluster still costs something every month.
3. Only deleting the resource group returns to zero, which makes `destroy` the
   normal end of a session. `deallocate` is for the gap between two runs on the
   same day.
4. `destroy` takes a confirmation naming the group, which doubles as the
   subscription check from
   [subscription, login and quota](#1-subscription-login-and-quota).

        cmake -Dconfirm=<group name> -P azure/destroy.cmake

5. `destroy` also removes the generated `.ssh/` material and the control sockets.
6. Cost scales with vCPU-hours, so the client count is the main lever. A ladder
   needs enough clients that the relay rather than a client is the limit, which
   [measure.md](measure.md) covers; below that, fewer clients is cheaper and no
   less valid.

## 7. Windows relay

`relay-windows` is in the template and waits on the IOCP socket backend.
`deploy_relay_windows` stays off until then, and this is the one place that says
so. Its inbound rules are `host/windows-firewall.ps1`, to be run on the VM before
traffic arrives.

1. Deploying it is `cmake -Ddeploy_relay_windows=ON -P azure/deploy.cmake`, and the
   relay actions reach it as `cmake -Dhost=relay-windows -P azure/start-relay.cmake`.
   The ninja targets address `relay_host`, which is the Linux relay.
2. It has no SSH-key equivalent, so `deploy` generates a password and prints it
   once. Rotate it with `az vm user update`.
3. Reaching it over SSH rather than RDP is possible in principle, but transport is
   the smaller half of the problem: the remote command vocabulary used throughout
   is POSIX, and the host counters have no Windows counterpart that counts the same
   things. The VM needs its own command set regardless.
