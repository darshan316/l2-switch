# L2 Ethernet Switch

A Layer 2 Ethernet learning switch written in C. It implements the core behavior
of a real network switch: learning where devices are, forwarding frames to the
correct port, isolating traffic with VLANs, and preventing loops with Spanning
Tree.

## Overview

A switch receives Ethernet frames on its ports and decides which port to send
each frame out of. This project implements that decision process from scratch.
It can run as a self-contained simulator that needs no special privileges, and
as a real software switch attached to Linux TAP interfaces.

## What it implements

- MAC address learning and aging (the forwarding database)
- Forwarding of known traffic to a single port
- Flooding of unknown and broadcast traffic within a VLAN
- 802.1Q VLANs with access ports and trunk ports (tagging and untagging)
- Spanning Tree Protocol: root bridge election and blocking ports to break loops

## How it works

For each frame the switch receives, it follows four steps. First, it learns the
source address and records the port it came from. Second, it determines which
VLAN the frame belongs to. Third, it looks up the destination address: if the
destination is known, the frame is forwarded to that one port; if it is unknown
or a broadcast, the frame is flooded to all other ports in the same VLAN.
Finally, the frame is tagged or untagged as required by the outgoing port.

Spanning Tree runs separately to keep the network loop free. Each switch has a
priority and an address that together form its bridge ID. The switches exchange
messages, elect the switch with the lowest ID as the root, and block the
smallest number of ports needed to turn the network into a loop-free tree.

## Project structure

- ethernet: frame parsing and building, address helpers, VLAN tag handling
- mactable: the forwarding database, a hash table with time-based aging
- switch: the learn-and-forward logic and the VLAN rules
- stp: Spanning Tree, including root election and port roles
- tap: connects the switch to Linux TAP interfaces
- main: the demonstrations and command line interface
- tests: unit tests for the database, forwarding, VLANs, and Spanning Tree

## Building and running

Build the project and run the tests:

    make
    make test

Run the built-in demonstrations (no special privileges needed):

- ./l2sw learn : MAC learning, where the first frame floods and the reply forwards
- ./l2sw vlan  : VLAN isolation and trunk tagging
- ./l2sw stp   : Spanning Tree blocking one port to break a loop

## Running with real traffic (Linux)

Attach the switch to two TAP interfaces and forward real frames between them:

    sudo ip tuntap add dev tap0 mode tap
    sudo ip tuntap add dev tap1 mode tap
    sudo ip link set tap0 up
    sudo ip link set tap1 up
    sudo ./l2sw tap tap0 tap1

## Concepts demonstrated

MAC learning and aging, the flood versus forward decision, 802.1Q VLAN tagging,
access and trunk ports, Spanning Tree root election, and port states.

## Limitations

To keep the code readable, this project does not implement Rapid Spanning Tree
timers, per-VLAN spanning tree, or link aggregation.
