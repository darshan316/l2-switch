/* mactable.h - the switch forwarding database (FDB / CAM table).
 *
 * Maps (vlan, source-MAC) -> egress port, with time-based aging. Implemented
 * as a chained hash table so lookups and learns are O(1) on average -- the
 * same structure real switch silicon emulates with a CAM. */
#ifndef MACTABLE_H
#define MACTABLE_H
#include "ethernet.h"
#include <time.h>

#define FDB_BUCKETS 1024
#define FDB_AGING_SECS 300        /* IEEE default 802.1D aging time */

typedef struct fdb_entry {
    uint16_t vlan;
    mac_t    mac;
    int      port;
    time_t   last_seen;
    struct fdb_entry *next;
} fdb_entry_t;

typedef struct {
    fdb_entry_t *buckets[FDB_BUCKETS];
    int aging_secs;
    int count;
} fdb_t;

void fdb_init(fdb_t *t);
void fdb_free(fdb_t *t);
/* Insert/refresh (vlan,mac)->port. Returns true if the location moved/added. */
bool fdb_learn(fdb_t *t, uint16_t vlan, mac_t mac, int port, time_t now);
/* Returns egress port for (vlan,mac), or -1 if unknown/aged out. */
int  fdb_lookup(fdb_t *t, uint16_t vlan, mac_t mac, time_t now);
/* Drop entries older than aging_secs. Returns number removed. */
int  fdb_age(fdb_t *t, time_t now);
/* Remove every entry pointing at a port (used when a port goes down). */
void fdb_flush_port(fdb_t *t, int port);
void fdb_dump(fdb_t *t, time_t now);
#endif
