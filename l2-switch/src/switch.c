#include "switch.h"
#include <stdio.h>
#include <string.h>

static void vlan_set(uint8_t *bm, uint16_t v){ bm[v>>3]|=(uint8_t)(1<<(v&7)); }
static bool vlan_get(const uint8_t *bm, uint16_t v){ return bm[v>>3]&(1<<(v&7)); }

void switch_init(switch_t *sw, int nports, sw_tx_fn tx, void *ctx){
    memset(sw,0,sizeof(*sw));
    sw->nports = nports>SW_MAX_PORTS?SW_MAX_PORTS:nports;
    sw->tx=tx; sw->tx_ctx=ctx;
    fdb_init(&sw->fdb);
    for(int i=0;i<sw->nports;i++){
        sw->port[i].up=true; sw->port[i].mode=PORT_ACCESS;
        sw->port[i].pvid=1; sw->port[i].stp=STP_FORWARDING;
    }
}
void switch_free(switch_t *sw){ fdb_free(&sw->fdb); }

void switch_set_access(switch_t *sw,int p,uint16_t vlan){
    sw->port[p].mode=PORT_ACCESS; sw->port[p].pvid=vlan;
}
void switch_set_trunk(switch_t *sw,int p,uint16_t native){
    sw->port[p].mode=PORT_TRUNK; sw->port[p].pvid=native;
    vlan_set(sw->port[p].allowed,native);
}
void switch_trunk_allow(switch_t *sw,int p,uint16_t vlan){ vlan_set(sw->port[p].allowed,vlan); }
void switch_set_stp(switch_t *sw,int p,stp_state_t s){ sw->port[p].stp=s; }

/* Is this port carrying this VLAN? An access port carries exactly one VLAN
 * (its PVID); a trunk carries every VLAN whose bit is set in its allowed map.
 * This is the check that makes "VLAN 10 never reaches the VLAN 20 port" true. */
bool switch_vlan_member(const switch_t *sw,int p,uint16_t vlan){
    const sw_port_t *pt=&sw->port[p];
    if(!pt->up) return false;
    return pt->mode==PORT_ACCESS ? (pt->pvid==vlan) : vlan_get(pt->allowed,vlan);
}

/* Resolve the VLAN a frame belongs to on ingress; returns false if it must be
 * dropped by VLAN policy. *tagged reports whether the wire frame carried a tag. */
static bool ingress_vlan(const switch_t *sw,int in,const uint8_t *f,
                         uint16_t *vlan,bool *tagged){
    const sw_port_t *p=&sw->port[in];
    *tagged=eth_is_tagged(f);
    if(p->mode==PORT_ACCESS){
        if(*tagged) return false;            /* access ports drop tagged frames */
        *vlan=p->pvid; return true;          /* untagged frame inherits the port's VLAN */
    }
    if(*tagged){                             /* trunk + tagged */
        *vlan=eth_vlan(f);                   /* believe the 802.1Q tag... */
        return vlan_get(p->allowed,*vlan);   /* ...but only if the trunk allows that VLAN */
    }
    *vlan=p->pvid; return true;              /* trunk + untagged -> native VLAN */
}

/* Build the right egress frame for one port from an untagged "core" frame.
 * Trunks re-add the 802.1Q tag (this is the "tagged 23 bytes" you saw);
 * access ports and the native VLAN go out untagged ("19 bytes"). */
static void emit(switch_t *sw,int out,const uint8_t *core,size_t clen,uint16_t vlan){
    uint8_t buf[ETH_MAX_FRAME]; size_t n;
    if(sw->port[out].mode==PORT_TRUNK && vlan!=sw->port[out].pvid)
        n=eth_add_tag(buf,core,clen,vlan);   /* tag on trunk (native stays untagged) */
    else
        n=eth_strip_tag(buf,core,clen);      /* access / native -> untagged */
    sw->tx(sw->tx_ctx,out,buf,n);            /* hand the bytes to the port (sim print / TAP write) */
    sw->txc++;
}

/* ============================================================================
 * switch_rx() -- the learn-and-forward heart of the switch.
 * Called once per received frame. This single function produces everything you
 * saw in `./l2sw learn` and `./l2sw vlan`. Read it top to bottom as the life of
 * one frame: validate -> pick VLAN -> LEARN the source -> LOOK UP the dest ->
 * forward to one port OR flood the VLAN.
 * ==========================================================================*/
void switch_rx(switch_t *sw,int in,const uint8_t *frame,size_t len,time_t now){
    /* (0) Sanity: real port index, and at least a full Ethernet header present. */
    if(in<0||in>=sw->nports||len<ETH_HLEN) return;
    sw->rx++;                                 /* counts toward "rx=" in the stats line */
    sw_port_t *ip=&sw->port[in];
    if(!ip->up){ sw->dropped++; return; }     /* ignore frames on an administratively-down port */

    /* (1) Pull the two addresses out of the frame.
     *     dst decides WHERE it goes; src is WHO we learn from. */
    mac_t dst=eth_dst(frame), src=eth_src(frame);

    /* (2) Control-traffic guards.
     *     BPDUs (Spanning Tree's own messages, 01:80:C2:00:00:00) are link-local:
     *     a bridge consumes them, it must never forward them like data. */
    if(mac_is_bpdu(dst)){ sw->dropped++; return; }
    /*     If Spanning Tree put this port in BLOCKING, it neither learns nor
     *     forwards -- that is literally how STP kills the loop in `./l2sw stp`. */
    if(ip->stp==STP_BLOCKING || ip->stp==STP_DISABLED){ sw->dropped++; return; }

    /* (3) Decide which VLAN this frame belongs to, and enforce VLAN policy.
     *     On an access port an untagged frame inherits the port's VLAN (10/20 in
     *     the vlan demo). A wrong/disallowed tag is dropped here. */
    uint16_t vlan; bool tagged;
    if(!ingress_vlan(sw,in,frame,&vlan,&tagged)){ sw->dropped++; return; }

    /* (4) Normalise to an untagged "core" frame so the internal logic never has
     *     to care whether the wire frame was tagged. We re-apply tags on egress
     *     per-port in emit(). */
    uint8_t core[ETH_MAX_FRAME];
    size_t clen=eth_strip_tag(core,frame,len);

    /* (5) LEARN. Record "src lives on port `in`, in this VLAN" in the forwarding
     *     database. This is the silent step in the learn demo: sending A->B also
     *     taught the switch where A is, so B's reply could be forwarded precisely.
     *     We never learn from a multicast/broadcast source (those aren't real
     *     single stations). fdb_learn() returns true only when something new was
     *     added/moved, which is what bumps the "learned=" counter. */
    if(!mac_is_multicast(src) && fdb_learn(&sw->fdb,vlan,src,in,now)) sw->learned++;

    /* (6) LOOK UP the destination within this VLAN.
     *     Broadcast/multicast can never be in the table, so we force a miss (-1)
     *     and let it flood. A unicast hit returns the egress port. */
    int out=mac_is_multicast(dst) ? -1 : fdb_lookup(&sw->fdb,vlan,dst,now);

    /* (7a) KNOWN UNICAST -> forward to exactly one port.
     *      This is the efficient path: frames [2] and [3] in the learn demo. */
    if(out>=0){
        if(out==in){ sw->dropped++; return; }            /* dest is on the same port it came in -> filter */
        if(switch_vlan_member(sw,out,vlan) && sw->port[out].stp==STP_FORWARDING){
            emit(sw,out,core,clen,vlan); sw->forwarded++; /* bumps "forwarded=" */
        } else sw->dropped++;                             /* dest port not in this VLAN / blocked */
        return;
    }

    /* (7b) UNKNOWN UNICAST or BROADCAST/MULTICAST -> FLOOD within the VLAN.
     *      Send out every OTHER port that (a) carries this VLAN and (b) is
     *      forwarding. This is frame [1] and [4] in the learn demo, and it is
     *      exactly why VLAN 10 traffic reaches the VLAN 10 port + trunk but
     *      NOT the VLAN 20 port: the membership test below excludes it. */
    int sent=0;
    for(int p=0;p<sw->nports;p++){
        if(p==in) continue;                              /* never echo back out the ingress port */
        if(!switch_vlan_member(sw,p,vlan)) continue;     /* VLAN isolation happens right here */
        if(sw->port[p].stp!=STP_FORWARDING) continue;    /* skip blocked ports (loop safety) */
        emit(sw,p,core,clen,vlan); sent++;
    }
    if(sent) sw->flooded++; else sw->dropped++;          /* bumps "flooded=" (or drop if nowhere to go) */
}

void switch_stats(const switch_t *sw){
    printf("rx=%lu tx=%lu forwarded=%lu flooded=%lu dropped=%lu learned=%lu fdb=%d\n",
           sw->rx,sw->txc,sw->forwarded,sw->flooded,sw->dropped,sw->learned,sw->fdb.count);
}
