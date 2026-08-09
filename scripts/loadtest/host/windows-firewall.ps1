# Inbound rules for the Windows relay, to be run on the VM before traffic
# arrives. The NSG opens these ranges at the network level and this opens them on
# the host; they are cluster.conf's client_ports and peer_ports.
#
# LocalOnlyMapping keeps the mapping per local port for UDP. Without it Windows
# treats the traffic as a connection and rewrites the mapping per remote peer.

$Rules = @(
	@{ Name = 'pal_relay_client'; Protocol = 'UDP'; LocalPort = '3478-3479'; LocalOnlyMapping = $True }
	@{ Name = 'pal_relay_peer'; Protocol = 'UDP'; LocalPort = '50000-60000'; LocalOnlyMapping = $True }
)

foreach ($Rule in $Rules)
{
	Remove-NetFirewallRule -Name $Rule.Name `
		-ErrorAction SilentlyContinue

	New-NetFirewallRule -Name $Rule.Name `
		-DisplayName $Rule.Name `
		-Direction Inbound `
		-Action Allow `
		-Protocol $Rule.Protocol `
		-LocalPort $Rule.LocalPort `
		-LocalOnlyMapping $Rule.LocalOnlyMapping `
		-ErrorAction Stop
}
