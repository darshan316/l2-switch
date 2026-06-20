# l2-switch a VLAN-aware Ethernet learning switch (with Spanning Tree)

A Layer 2 switch written in C from the frame up. It implements the same datapath
real switches run: source-MAC learning, flood / forward decisions,
802.1Q VLANs (access + trunk ports) and Spanning Tree (802.1D) to keep a
looped topology loop-free. It runs as a self-contained simulator (no privileges)
and as a real software switch over Linux TAP interfaces.

## Why this exists
Switching is the Layer 2 half of "Layer 2/3" that every networking vendor screens
for. This codebase shows the concepts working end-to-end rather than as bullet
points: a CAM/forwarding table with aging, the unknown-unicast flood vs
known-unicast forward distinction, VLAN tagging on trunks and the BPDU priority
vector comparison that elects a root bridge and breaks loops.

## Datapath (one received frame)

        ┌─────────────┐
 frame ─▶ port + STP   │  drop on down/blocked port; consume BPDUs
        │  state check │
        ├─────────────┤
        │ ingress VLAN │  access: untagged→PVID | trunk: read 802.1Q / native
        ├─────────────┤
        │   LEARN      │  fdb[(vlan, src_mac)] = ingress_port   (+ aging)
        ├─────────────┤
        │  LOOKUP dst  │  hit → forward to one port | miss/bcast → flood VLAN
        ├─────────────┤
        │ egress VLAN  │  trunk: tag (native untagged) | access: strip
        └─────────────┘


## Layout
| file | what it is |
|------|------------|
| ethernet.[ch] | frame parsing/building, MAC helpers, 802.1Q tag add/strip |
| mactable.[ch] | forwarding database: chained hash table + time-based aging |
| switch.[ch]   | the learn-and-forward datapath, VLAN access/trunk logic |
| stp.[ch]      | Spanning Tree: BPDU vector compare, root election, port roles |
| tap.[ch]      | Linux TAP backend so real hosts can plug into the switch |
| main.c        | learn / vlan / stp demos + live tap mode |
| tests/        | dependency-free unit tests (FDB, forwarding, VLAN, STP) |

## Build & test
sh
make          # builds ./l2sw
make test     # builds and runs the unit tests (16 checks)


## Try it (no root needed)
sh
./l2sw learn  # watch the FDB learn: first frame floods, the reply forwards
./l2sw vlan   # VLAN10 stays isolated from VLAN20, trunk frames get tagged
./l2sw stp    # 3 switches in a triangle, STP blocks exactly one port


./l2sw stp output a physical loop converted into a loop free tree:

Root bridge: prio=32768 mac=00:00:00:00:00:01
  SW2 port 2 -> SW1:  BLOCK BLOCKING   <- loop broken here
Result: 1 port(s) BLOCKING -> the triangle is now a loop free tree.


## Run it for real (Linux, root)
Bind two TAP interfaces to two switch ports and bridge real traffic between them:
sh
sudo ip tuntap add dev tap0 mode tap
sudo ip tuntap add dev tap1 mode tap
sudo ip link set tap0 up && sudo ip link set tap1 up
sudo ./l2sw tap tap0 tap1     # now frames are learned & switched between them

(Put each TAP in its own network namespace with IPs in the same subnet to ping
across the switch.)

## Concepts demonstrated
MAC learning & aging  unknown unicast flooding 802.1Q VLAN tagging
access vs trunk ports native VLAN Spanning Tree root election port
roles (root/designated/blocked) BPDU priority vectors TAP/L2 I/O on Linux.

## Deliberately out of scope
Per-VLAN spanning tree (PVST), RSTP fast transitions, LACP/port-channels and
hardware offload concerns kept out to keep the core readable.
