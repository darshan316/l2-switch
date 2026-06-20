/* ethernet.h - Ethernet II / 802.1Q frame helpers.
 *
 * Frames are handled as raw byte buffers exactly as they appear on the wire,
 * so the same code works for the in-memory simulator and for real frames read
 * from a Linux TAP device.
 *
 * Untagged Ethernet II:  [dst(6)][src(6)][ethertype(2)][payload...]
 * 802.1Q tagged:         [dst(6)][src(6)][0x8100(2)][TCI(2)][ethertype(2)][payload]
 *   TCI = PCP(3) | DEI(1) | VID(12)
 */
#ifndef ETHERNET_H
#define ETHERNET_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define ETH_ALEN 6
#define ETH_HLEN 14
#define ETH_VLAN_HLEN 18
#define ETH_MAX_FRAME 1600

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_VLAN 0x8100

typedef struct { uint8_t addr[ETH_ALEN]; } mac_t;

bool   mac_parse(const char *s, mac_t *out);
char  *mac_str(mac_t m, char *buf);          /* buf >= 18 bytes */
bool   mac_equal(mac_t a, mac_t b);
bool   mac_is_broadcast(mac_t m);
bool   mac_is_multicast(mac_t m);            /* includes broadcast */
bool   mac_is_bpdu(mac_t m);                 /* 01:80:C2:00:00:00 */
uint32_t mac_hash(uint16_t vlan, mac_t m);

mac_t    eth_dst(const uint8_t *f);
mac_t    eth_src(const uint8_t *f);
bool     eth_is_tagged(const uint8_t *f);
uint16_t eth_vlan(const uint8_t *f);         /* valid only if tagged */
uint16_t eth_ethertype(const uint8_t *f);    /* inner ethertype */

size_t eth_build(uint8_t *out, mac_t dst, mac_t src, uint16_t ethertype,
                 const uint8_t *payload, size_t payload_len);
size_t eth_add_tag(uint8_t *out, const uint8_t *in, size_t in_len, uint16_t vlan);
size_t eth_strip_tag(uint8_t *out, const uint8_t *in, size_t in_len);
#endif
