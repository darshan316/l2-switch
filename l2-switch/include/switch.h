/* switch.h - a VLAN-aware Ethernet learning switch.
 *
 * The switch owns N ports and a forwarding database. switch_rx() runs the
 * classic learn-then-forward datapath for one received frame:
 *   1. enforce port/STP state and the ingress VLAN policy (access vs trunk),
 *   2. learn the source MAC against (vlan, ingress-port),
 *   3. flood broadcast/unknown-unicast within the VLAN, or forward a known
 *      unicast to its single egress port,
 *   4. apply per-egress-port VLAN tagging (tag on trunks, strip on access).
 *
 * Frames leave through a tx callback, so the simulator and the TAP backend
 * plug into the exact same datapath. */
#ifndef SWITCH_H
#define SWITCH_H
#include "ethernet.h"
#include "mactable.h"
#include "stp.h"

#define SW_MAX_PORTS 16
#define VLAN_BITMAP_BYTES (4096/8)

typedef enum { PORT_ACCESS, PORT_TRUNK } port_mode_t;

typedef struct {
    bool        up;
    port_mode_t mode;
    uint16_t    pvid;                         /* access VLAN / trunk native VLAN */
    uint8_t     allowed[VLAN_BITMAP_BYTES];   /* trunk: VLANs carried */
    stp_state_t stp;
} sw_port_t;

typedef void (*sw_tx_fn)(void *ctx, int port, const uint8_t *frame, size_t len);

typedef struct {
    sw_port_t port[SW_MAX_PORTS];
    int       nports;
    fdb_t     fdb;
    sw_tx_fn  tx;
    void     *tx_ctx;
    /* counters */
    unsigned long rx, txc, flooded, forwarded, dropped, learned;
} switch_t;

void switch_init(switch_t *sw, int nports, sw_tx_fn tx, void *ctx);
void switch_free(switch_t *sw);
void switch_set_access(switch_t *sw, int port, uint16_t vlan);
void switch_set_trunk (switch_t *sw, int port, uint16_t native);
void switch_trunk_allow(switch_t *sw, int port, uint16_t vlan);
void switch_set_stp(switch_t *sw, int port, stp_state_t s);
bool switch_vlan_member(const switch_t *sw, int port, uint16_t vlan);

/* Feed one received frame in on in_port. Drives the full datapath. */
void switch_rx(switch_t *sw, int in_port, const uint8_t *frame, size_t len, time_t now);
void switch_stats(const switch_t *sw);
#endif
