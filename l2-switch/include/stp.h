/* stp.h - Spanning Tree Protocol (IEEE 802.1D, simplified).
 *
 * Two things live here:
 *   1. A per-port STP state (BLOCKING/FORWARDING) that the live switch consults
 *      before it forwards or learns on a port -- this is how STP prevents loops.
 *   2. A small synchronous solver, stp_converge(), that takes a multi-switch
 *      topology (which may contain physical loops), runs the standard BPDU
 *      "best vector" comparison to a fixpoint, elects a root bridge, and assigns
 *      every port a role (ROOT / DESIGNATED / BLOCKED). Exactly enough ports end
 *      up BLOCKING to turn the mesh into a loop-free tree.
 *
 * Bridge ID = 16-bit priority followed by the bridge MAC (lower = better).
 */
#ifndef STP_H
#define STP_H
#include "ethernet.h"

typedef enum { STP_DISABLED, STP_BLOCKING, STP_FORWARDING } stp_state_t;
typedef enum { ROLE_NONE, ROLE_ROOT, ROLE_DESIGNATED, ROLE_BLOCKED } stp_role_t;

typedef struct { uint16_t prio; mac_t mac; } bridge_id_t;
int bridge_id_cmp(bridge_id_t a, bridge_id_t b);   /* <0 if a is better */

/* A BPDU "priority vector": {root, cost-to-root, sender, sender-port}. */
typedef struct {
    bridge_id_t root;
    uint32_t    cost;
    bridge_id_t sender;
    uint16_t    port;
} bpdu_t;
int bpdu_cmp(bpdu_t a, bpdu_t b);                  /* <0 if a is better */

#define STP_MAX_SW    16
#define STP_MAX_PORTS 16
#define STP_MAX_LINKS 64

typedef struct { int a, pa, b, pb; uint32_t cost; } stp_link_t;

typedef struct {
    bridge_id_t id[STP_MAX_SW];
    int         nsw;
    stp_link_t  link[STP_MAX_LINKS];
    int         nlinks;
    /* outputs (filled by stp_converge) */
    bridge_id_t root;
    int         root_port[STP_MAX_SW];
    stp_role_t  role[STP_MAX_SW][STP_MAX_PORTS];
    stp_state_t state[STP_MAX_SW][STP_MAX_PORTS];
} stp_topo_t;

void stp_topo_init(stp_topo_t *t);
int  stp_add_switch(stp_topo_t *t, uint16_t prio, mac_t mac);  /* returns index */
void stp_add_link(stp_topo_t *t, int a, int pa, int b, int pb, uint32_t cost);
void stp_converge(stp_topo_t *t);
void stp_print(const stp_topo_t *t);
const char *stp_role_name(stp_role_t r);
const char *stp_state_name(stp_state_t s);
#endif
