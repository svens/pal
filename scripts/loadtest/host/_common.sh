# Shared preflight and NIC resolution for the host tuning scripts.
#
# Sourced, not run.

verb=${1:-show}

require_verb()
{
	for accepted in "$@"; do
		if [ "$verb" = "$accepted" ]; then
			return
		fi
	done
	echo "usage: $(basename "$0") [$(echo "$*" | tr ' ' '|')]" >&2
	exit 2
}

require_root()
{
	if [ "$(id -u)" -ne 0 ]; then
		echo "$(basename "$0"): must run as root" >&2
		exit 1
	fi
}

# The interface to tune. With Azure accelerated networking there are two: a
# synthetic netvsc device (eth0), which is what the application binds to, and a
# Mellanox VF, which is where the hardware queues actually live. Ring sizes,
# coalescing, RSS hashing and XPS have to go to the VF; sending them to eth0
# succeeds and does nothing. Override with NIC=<name> when the heuristic is
# wrong -- on bare metal there is only one interface and it is the right one.
get_nic()
{
	if [ -n "$NIC" ]; then
		echo "$NIC"
		return
	fi
	for candidate in /sys/class/net/*; do
		name=$(basename "$candidate")
		case $name in
			lo|eth0|docker*|veth*)
				continue
				;;
		esac
		echo "$name"
		return
	done
	echo eth0
}

# IRQ numbers belonging to the tuned interface. Empty for a synthetic-only
# device, which is itself the answer to "why did IRQ affinity not help".
#
# sysfs first, because /proc/interrupts is named by whoever requested the
# interrupt and not every driver uses the interface name: mlx5 -- the Azure VF --
# names them after the PCI device, so a name search finds nothing on the one NIC
# this suite exists to tune. The search stays as a fallback for a device that
# exposes no msi_irqs.
get_irq()
{
	nic=$(get_nic)
	msi_irqs="/sys/class/net/${nic}/device/msi_irqs"
	if [ -d "$msi_irqs" ]; then
		ls "$msi_irqs" | sort -n
		return
	fi
	grep -E "[[:space:]]${nic}(-|$|[^[:alnum:]])" /proc/interrupts | cut -d: -f1 | tr -d ' '
}

# What requested an interrupt, e.g. mlx5_comp0@pci:... -- sysfs where it exists,
# since /proc/interrupts puts the name after a column per CPU and the count of
# those varies by machine.
irq_name()
{
	if [ -r "/sys/kernel/irq/$1/actions" ]; then
		cat "/sys/kernel/irq/$1/actions"
	else
		awk -v irq="$1:" '$1 == irq { print $NF }' /proc/interrupts
	fi
}

# Whether an interrupt carries packets, as opposed to the device's own async,
# command or error notifications. Excluding by role rather than listing the queue
# spellings of every driver: an unrecognised name is treated as a queue, which is
# what the caller would have assumed anyway.
irq_is_queue()
{
	case $(irq_name "$1") in
		*async*|*mgmnt*|*mgmt*|*cmd*|*event*|*config*|*ctrl*|*error*)
			return 1
			;;
	esac
	return 0
}

# Driver counters that mean the NIC threw a packet away, as "name value" pairs.
# Names differ per driver, so match loosely rather than assuming mlx5 spellings.
nic_discards()
{
	ethtool -S "$(get_nic)" 2>/dev/null | awk '
		/discard|dropped|out_of_buffer|no_buffer|error/ {
			gsub(/:/, "", $1)
			print $1, $2
		}
	'
}

# One CPU as the hex mask that /proc/irq/<n>/smp_affinity and xps_cpus take.
cpu_mask()
{
	printf '%x' $((1 << $1))
}

# A mask read back from sysfs, which comes zero-padded and comma-grouped, so
# comparisons are on the significant digits rather than on the text. Empty for a
# device the kernel keeps no mask for, which is a third outcome and not a zero.
mask_value()
{
	tr -d ',' < "$1" 2>/dev/null | sed 's/^0*//;s/^$/0/'
}

report()
{
	printf '%-32s %s\n' "$1" "$2"
}

# Set by verify() when a write did not take, so a stage can exit nonzero rather
# than leave the caller to read the output.
verify_failed=""

# What a write actually produced, against what it asked for. Every knob here can
# refuse or ignore a write and still return success, so apply re-reads instead of
# assuming. An absent knob is a third outcome and not a failure: a guest with no
# cpufreq is a different machine from one whose governor was rejected.
verify()
{
	if [ -z "$3" ]; then
		report "verify $1" "unavailable: this machine does not report it"
	elif [ "$2" = "$3" ]; then
		report "verify $1" "ok, $3"
	else
		report "verify $1" "REFUSED: asked $2, holds $3"
		verify_failed="yes"
	fi
}

verify_exit()
{
	if [ -n "$verify_failed" ]; then
		exit 1
	fi
}
