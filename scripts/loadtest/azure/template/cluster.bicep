@description('Region for every resource.')
param location string

@description('Address permitted to reach SSH.')
param login_ip string

@description('Well-known UDP ports the relay binds.')
param client_ports string

@description('Per-session peer port range a TURN-style relay allocates from.')
param peer_ports string

@description('Admin account created on every VM.')
param username string

@description('Public key installed for the admin account.')
param ssh_public_key string

param relay_size string
param client_size string

@minValue(1)
@maxValue(50)
param client_count int

var subnet_name = 'default'

var image = {
	publisher: 'Canonical'
	offer: 'ubuntu-26_04-lts'
	sku: 'server'
	version: 'latest'
}

// index fixes the last octet of the private address, so that --server values
// and shell history survive a stop/start cycle:
//   10.0.0.4  relay-linux
//   10.0.0.5+ loadtest-1..N
var relay = {
	name: 'relay-linux'
	index: 0
	size: relay_size
}

var clients = [for i in range(1, client_count): {
	name: 'loadtest-${i}'
	index: i
	size: client_size
}]

var vms = concat([relay], clients)

resource nsg 'Microsoft.Network/networkSecurityGroups@2021-03-01' = {
	name: 'nsg'
	location: location
	properties: {
		securityRules: [
			{
				name: 'allow-login'
				properties: {
					priority: 1001
					access: 'Allow'
					direction: 'Inbound'
					protocol: 'Tcp'
					destinationAddressPrefix: '*'
					destinationPortRange: '22'
					sourceAddressPrefixes: [
						login_ip
					]
					sourcePortRange: '*'
				}
			}
			{
				name: 'allow-traffic'
				properties: {
					priority: 1010
					access: 'Allow'
					direction: 'Inbound'
					protocol: 'UDP'
					destinationAddressPrefix: '*'
					destinationPortRanges: [
						client_ports
						peer_ports
					]
					sourceAddressPrefix: '*'
					sourcePortRange: '*'
				}
			}
		]
	}
}

// The subnet is declared inline rather than as a separate child resource: when
// one template owns both, a standalone subnet resource can be dropped as the
// vnet is updated.
resource vnet 'Microsoft.Network/virtualNetworks@2021-03-01' = {
	name: 'vnet'
	location: location
	properties: {
		addressSpace: {
			addressPrefixes: [
				'10.0.0.0/16'
			]
		}
		subnets: [
			{
				name: subnet_name
				properties: {
					addressPrefix: '10.0.0.0/24'
					networkSecurityGroup: {
						id: nsg.id
					}
				}
			}
		]
	}
}

resource pip 'Microsoft.Network/publicIPAddresses@2021-03-01' = [for vm in vms: {
	name: '${vm.name}-pip'
	location: location
	sku: {
		name: 'Standard'
	}
	properties: {
		publicIPAllocationMethod: 'Static'
	}
}]

resource nic 'Microsoft.Network/networkInterfaces@2021-03-01' = [for (vm, i) in vms: {
	name: '${vm.name}-nic'
	location: location
	properties: {
		// Accelerated networking attaches a Mellanox VF alongside the synthetic
		// interface. The application binds to the synthetic one; NIC tuning has
		// to be applied to the VF. See docs/tune_linux_azure.md, 1 Accelerated
		// networking presents two interfaces.
		enableAcceleratedNetworking: true
		enableIPForwarding: false
		ipConfigurations: [
			{
				name: '${vm.name}-ipconfig'
				properties: {
					privateIPAddress: '10.0.0.${vm.index + 4}'
					privateIPAllocationMethod: 'Static'
					publicIPAddress: {
						id: pip[i].id
					}
					subnet: {
						id: '${vnet.id}/subnets/${subnet_name}'
					}
				}
			}
		]
		networkSecurityGroup: {
			id: nsg.id
		}
	}
}]

resource machine 'Microsoft.Compute/virtualMachines@2021-07-01' = [for (vm, i) in vms: {
	name: vm.name
	location: location
	properties: {
		hardwareProfile: {
			vmSize: vm.size
		}
		osProfile: {
			computerName: vm.name
			adminUsername: username
			allowExtensionOperations: false
			customData: loadFileAsBase64('cloud-init.yaml')
			linuxConfiguration: {
				disablePasswordAuthentication: true
				provisionVMAgent: false
				ssh: {
					publicKeys: [
						{
							path: '/home/${username}/.ssh/authorized_keys'
							keyData: ssh_public_key
						}
					]
				}
			}
		}
		storageProfile: {
			imageReference: image
			osDisk: {
				name: '${vm.name}-disk'
				createOption: 'FromImage'
			}
		}
		networkProfile: {
			networkInterfaces: [
				{
					id: nic[i].id
				}
			]
		}
	}
}]

output names array = [for vm in vms: vm.name]
output private_addresses array = [for vm in vms: '10.0.0.${vm.index + 4}']
output public_addresses array = [for (vm, i) in vms: pip[i].properties.ipAddress]
