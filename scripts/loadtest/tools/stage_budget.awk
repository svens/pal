# Turn folded stacks into the per-packet time budget.
#
# usage: awk -f stage_budget.awk -v ns_per_packet=<n> [-v format=markdown|tsv] <name>.folded
#
# format=markdown is the table for reading; format=tsv is the same numbers for
# tools/profile_diff.cmake, one row per line, first field naming the row kind.
#
# Input is FlameGraph's stackcollapse-perf.pl output: one line per unique stack,
# frames root-first separated by ';', sample count last.
#
# Leaf-first: walk from the leaf towards the root, first frame matching a stage
# pattern decides. A leaf of copy_user_enhanced_fast_string matches nothing, walks
# up to udp_recvmsg, and lands in the receive body. Root-first would put every
# sample in the syscall entry stub.
#
# Table order matters where one frame matches two stages. mlx5e_xmit is tested
# before the generic mlx5 receive pattern, or transmit samples land in softirq.
# The relay's handler is tested before pal, because the completion thunk names
# both: pal::...single_shot_thunk<..., listener::on_peer_receive>.
#
# Idle samples are excluded, because the shares are compared against machine busy
# from host/counters, which does not count idle. Idle is decided by the leaf frame
# alone, never by the process name: softirq runs in the idle task's context, so a
# swapper stack ending in nf_conntrack_in is receive-path work.

BEGIN {
	stages[1] = "netfilter and conntrack"
	match_re[1] = "nf_hook|nf_conntrack|nft_|ipt_do_table|iptable_|nf_nat|__nf_ct"
	scope[1] = "context"

	stages[2] = "io_uring machinery"
	match_re[2] = "io_uring|io_issue_sqe|io_submit_sqes|io_cqring|io_recv|io_send|__io_"
	scope[2] = "in_scope"

	stages[3] = "kernel send path"
	match_re[3] = "sendmmsg|sendmsg|udp_send|ip_send|ip_output|ip_finish_output|ip_route_output|dev_queue_xmit|dev_hard_start_xmit|__qdisc|sch_direct|mlx5e_xmit|mlx5e_sq"
	scope[3] = "in_scope"

	stages[4] = "kernel receive path"
	match_re[4] = "recvmmsg|recvmsg|udp_recvmsg|skb_recv_udp|skb_consume_udp|__skb_try_recv"
	scope[4] = "in_scope"

	stages[5] = "softirq receive"
	match_re[5] = "net_rx_action|napi|netif_receive|ip_rcv|ip_local_deliver|udp4_lib_rcv|udp_queue_rcv|gro_|mlx5|__do_softirq|irq_exit"
	scope[5] = "context"

	stages[6] = "syscall entry and exit"
	match_re[6] = "entry_SYSCALL|do_syscall_64|syscall_exit|syscall_return|exit_to_user|__x64_sys|entry_SYSRETQ"
	scope[6] = "in_scope"

	stages[7] = "relay application"
	match_re[7] = "udp_relay|client_map|listener|relay::|on_client_receive|on_peer_receive|rearm_"
	scope[7] = "in_scope"

	stages[8] = "pal event loop"
	match_re[8] = "pal::"
	scope[8] = "in_scope"

	count = 8
	idle_re = "native_safe_halt|mwait_idle|acpi_idle|poll_idle|cpuidle_enter|do_idle"

	if (format == "") {
		format = "markdown"
	}
	if (format != "markdown" && format != "tsv") {
		print "unknown format '" format "': markdown or tsv" > "/dev/stderr"
		aborted = 1
		exit 1
	}
}

{
	samples = $NF
	stack = $0
	sub(/ [0-9]+$/, "", stack)

	depth = split(stack, frames, ";")
	if (frames[depth] ~ idle_re) {
		idle += samples
		next
	}

	stage = "other"
	for (i = depth; i >= 1; i--) {
		for (rule = 1; rule <= count; rule++) {
			if (frames[i] ~ match_re[rule]) {
				stage = stages[rule]
				i = 0
				break
			}
		}
	}
	total[stage] += samples
	busy += samples
}

END {
	if (aborted) {
		exit 1
	}
	if (busy == 0) {
		print "no non-idle samples: the capture saw nothing, or every frame is unresolved" > "/dev/stderr"
		exit 1
	}

	if (format == "markdown") {
		print "| stage | share of busy | ns/packet |"
		print "|---|---|---|"
	} else {
		printf("total\t%d\t%d\t%d\n", ns_per_packet, busy, idle)
	}
	for (rule = 1; rule <= count; rule++) {
		emit(stages[rule], scope[rule], total[stages[rule]])
	}
	emit("other", "context", total["other"])
	if (format == "markdown") {
		printf("\nnon-idle samples %d, idle samples %d\n", busy, idle)
	}
	if (ns_per_packet == 0) {
		print "no ns_per_packet given: shares only" > "/dev/stderr"
	}
}

function emit(name, in_scope, samples,    share, ns) {
	share = 100 * samples / busy
	ns = ns_per_packet * samples / busy
	if (format == "tsv") {
		printf("stage\t%s\t%s\t%d\t%.2f\t%.1f\n", name, in_scope, samples, share, ns)
	} else if (ns_per_packet > 0) {
		printf("| %s | %.1f | %.0f |\n", name, share, ns)
	} else {
		printf("| %s | %.1f | - |\n", name, share)
	}
}
