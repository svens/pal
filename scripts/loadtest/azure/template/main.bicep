targetScope = 'subscription'

@description('Region for the resource group and everything in it.')
param location string

@description('Address permitted to reach SSH.')
param login_ip string

@description('Admin account created on every VM, and the resource group prefix.')
param username string

@description('Public key installed for the admin account.')
param ssh_public_key string

param relay_size string
param client_size string

@minValue(1)
@maxValue(50)
param client_count int

@description('Well-known UDP ports the relay binds.')
param client_ports string

@description('Per-session peer port range a TURN-style relay allocates from.')
param peer_ports string

resource rg 'Microsoft.Resources/resourceGroups@2021-04-01' = {
	name: '${username}-loadtest'
	location: location
}

module cluster './cluster.bicep' = {
	name: 'cluster'
	scope: rg
	params: {
		location: location
		login_ip: login_ip
		username: username
		ssh_public_key: ssh_public_key
		relay_size: relay_size
		client_size: client_size
		client_count: client_count
		client_ports: client_ports
		peer_ports: peer_ports
	}
}

output names array = cluster.outputs.names
output private_addresses array = cluster.outputs.private_addresses
output public_addresses array = cluster.outputs.public_addresses
