# CLUSTER

## 1. Subscription, login and quota

1. Log in first: `az login`, or `az login --use-device-code` from a machine
   without a browser.
2. Confirm which subscription `az` is pointed at before deploying. Every
   subscription holds its resource group under the same name, so pointing at the
   wrong one produces a second cluster rather than an error.
3. Check the regional vCPU quota for the VM families in `cluster.conf` before the
   first deploy, against the sizes and `client_count` it sets. A full cluster can
   sit at the cap, leaving no room for anything else in that region.
4. A size that is unrestricted on one
   subscription may be zone-restricted or unavailable on another, which surfaces
   at deploy time rather than at validate time.

## 2. Cluster shape

1. `cluster.conf` holds the cluster shape and the run defaults, and
   [README.md](../README.md) has the invocation forms.
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
4. Re-shipping while the relay is running is safe; the next start picks up the new
   binary.
5. Clients are left untuned.

## 5. Access

1. `deploy` generates a passphraseless keypair per cluster in `.ssh/` beside these
   scripts, not in `~/.ssh`, and installs the public half on every VM. There is no
   password login.
2. `deploy` also writes `.ssh/local.conf`, which points `IdentityFile` at that
   key, so reach a VM with `ssh -F .ssh/local.conf relay-linux`.
3. `-Dssh_public_key_file=<path>` substitutes your own key instead, in which case
   ssh falls back to its usual identity search. `-Dssh_public_key_text=<key>`
   passes one inline.
4. Control sockets live under `$TMPDIR/<resource group>`; `-Dcontrol_dir=<path>`
   overrides it.

## 6. Cost and lifecycle

1. There is no auto-shutdown. A cluster left running bills until something stops
   it.
2. `deallocate` stops compute, but disks and static public addresses keep
   charging, so a stopped cluster still costs something every month.
3. Only deleting the resource group returns to zero. `deallocate` is for the gap
   between two runs on the same day.
4. `destroy` takes a confirmation naming the group, which doubles as the
   subscription check from
   [subscription, login and quota](#1-subscription-login-and-quota).

        cmake -Dconfirm=<group name> -P azure/destroy.cmake

5. `destroy` also removes the generated `.ssh/` material and the control sockets.
