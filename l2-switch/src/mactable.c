#include "mactable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fdb_init(fdb_t *t){
    memset(t,0,sizeof(*t));
    t->aging_secs = FDB_AGING_SECS;
}
void fdb_free(fdb_t *t){
    for(int i=0;i<FDB_BUCKETS;i++){
        fdb_entry_t *e=t->buckets[i];
        while(e){ fdb_entry_t *n=e->next; free(e); e=n; }
        t->buckets[i]=NULL;
    }
    t->count=0;
}
static fdb_entry_t *find(fdb_t *t, uint16_t vlan, mac_t mac, uint32_t *bkt){
    uint32_t b = mac_hash(vlan,mac) % FDB_BUCKETS;
    if(bkt) *bkt=b;
    for(fdb_entry_t *e=t->buckets[b]; e; e=e->next)
        if(e->vlan==vlan && mac_equal(e->mac,mac)) return e;
    return NULL;
}
bool fdb_learn(fdb_t *t, uint16_t vlan, mac_t mac, int port, time_t now){
    if(mac_is_multicast(mac)) return false;   /* never learn from group addrs */
    uint32_t b; fdb_entry_t *e=find(t,vlan,mac,&b);
    if(e){
        e->last_seen=now;
        if(e->port!=port){ e->port=port; return true; }   /* station moved */
        return false;
    }
    e=calloc(1,sizeof(*e));
    e->vlan=vlan; e->mac=mac; e->port=port; e->last_seen=now;
    e->next=t->buckets[b]; t->buckets[b]=e; t->count++;
    return true;
}
int fdb_lookup(fdb_t *t, uint16_t vlan, mac_t mac, time_t now){
    fdb_entry_t *e=find(t,vlan,mac,NULL);
    if(!e) return -1;
    if(now - e->last_seen > t->aging_secs) return -1;     /* aged */
    return e->port;
}
int fdb_age(fdb_t *t, time_t now){
    int removed=0;
    for(int i=0;i<FDB_BUCKETS;i++){
        fdb_entry_t **pp=&t->buckets[i];
        while(*pp){
            if(now - (*pp)->last_seen > t->aging_secs){
                fdb_entry_t *dead=*pp; *pp=dead->next; free(dead);
                t->count--; removed++;
            } else pp=&(*pp)->next;
        }
    }
    return removed;
}
void fdb_flush_port(fdb_t *t, int port){
    for(int i=0;i<FDB_BUCKETS;i++){
        fdb_entry_t **pp=&t->buckets[i];
        while(*pp){
            if((*pp)->port==port){
                fdb_entry_t *d=*pp; *pp=d->next; free(d); t->count--;
            } else pp=&(*pp)->next;
        }
    }
}
void fdb_dump(fdb_t *t, time_t now){
    char b[18];
    printf("  VLAN  MAC                 PORT  AGE(s)\n");
    for(int i=0;i<FDB_BUCKETS;i++)
        for(fdb_entry_t *e=t->buckets[i]; e; e=e->next)
            printf("  %-4u  %-17s  %4d  %6ld\n",
                   e->vlan, mac_str(e->mac,b), e->port, (long)(now-e->last_seen));
}
