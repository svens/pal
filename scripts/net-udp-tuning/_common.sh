get_net_device()
{
	lspci | grep Mellanox | cut -f1 -d' '
}

get_irq()
{
	cat /proc/interrupts | grep $(get_net_device) | grep -v async | cut -f1 -d:
}

get_nic()
{
	ls /sys/class/net | grep -v 'eth0\|lo'
}
