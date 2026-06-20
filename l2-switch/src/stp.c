#include "stp.h"
#include <stdio.h>
#include <string.h>

int bridge_id_cmp(bridge_id_t a, bridge_id_t b){
    if(a.prio!=b.prio) return (int)a.prio-(int)b.prio;
    return memcmp(a.mac.addr,b.mac.addr,ETH_ALEN);
}
int bpdu_cmp(bpdu_t a, bpdu_t b){
    int c=bridge_id_cmp(a.root,b.root); if(c) return c;
    if(a.cost!=b.cost) return (a.cost<b.cost)?-1:1;
    c=bridge_id_cmp(a.sender,b.sender); if(c) return c;
    return (int)a.port-(int)b.port;
}
const char *stp_role_name(stp_role_t r){
    switch(r){case ROLE_ROOT:return "ROOT";case ROLE_DESIGNATED:return "DESG";
              case ROLE_BLOCKED:return "BLOCK";default:return "-";}
}
const char *stp_state_name(stp_state_t s){
    switch(s){case STP_FORWARDING:return "FORWARDING";case STP_BLOCKING:return "BLOCKING";
              default:return "DISABLED";}
}
void stp_topo_init(stp_topo_t *t){ memset(t,0,sizeof(*t)); }
int stp_add_switch(stp_topo_t *t, uint16_t prio, mac_t mac){
    int i=t->nsw++; t->id[i].prio=prio; t->id[i].mac=mac; return i;
}
void stp_add_link(stp_topo_t *t,int a,int pa,int b,int pb,uint32_t cost){
    stp_link_t *l=&t->link[t->nlinks++];
    l->a=a; l->pa=pa; l->b=b; l->pb=pb; l->cost=cost?cost:4;
}

/* The advertisement a switch currently emits: its view of root + cost. */
static bpdu_t advert(const stp_topo_t *t, const bpdu_t *V, int sw, int port){
    bpdu_t o; o.root=V[sw].root; o.cost=V[sw].cost; o.sender=t->id[sw]; o.port=(uint16_t)port;
    return o;
}

void stp_converge(stp_topo_t *t){
    bpdu_t V[STP_MAX_SW]={0};                 /* each switch's best priority vector */
    for(int i=0;i<t->nsw;i++){
        V[i].root=t->id[i]; V[i].cost=0; V[i].sender=t->id[i]; V[i].port=0;
        t->root_port[i]=-1;
    }
    /* Relax until no switch improves its vector (Bellman-Ford style). */
    for(int round=0; round<t->nsw+2; round++){
        int changed=0;
        for(int li=0; li<t->nlinks; li++){
            stp_link_t *l=&t->link[li];
            /* a -> b */
            bpdu_t c=advert(t,V,l->a,l->pa); c.cost+=l->cost;
            if(bpdu_cmp(c,V[l->b])<0){ V[l->b].root=c.root; V[l->b].cost=c.cost;
                V[l->b].sender=c.sender; V[l->b].port=c.port; t->root_port[l->b]=l->pb; changed=1; }
            /* b -> a */
            bpdu_t d=advert(t,V,l->b,l->pb); d.cost+=l->cost;
            if(bpdu_cmp(d,V[l->a])<0){ V[l->a].root=d.root; V[l->a].cost=d.cost;
                V[l->a].sender=d.sender; V[l->a].port=d.port; t->root_port[l->a]=l->pa; changed=1; }
        }
        if(!changed) break;
    }
    t->root = V[0].root;                  /* converged switches agree on root */
    for(int i=0;i<t->nsw;i++)
        for(int p=0;p<STP_MAX_PORTS;p++){ t->role[i][p]=ROLE_NONE; t->state[i][p]=STP_DISABLED; }

    /* Decide the designated end of every segment, then assign roles. */
    for(int li=0; li<t->nlinks; li++){
        stp_link_t *l=&t->link[li];
        bpdu_t adv_a=advert(t,V,l->a,l->pa);
        bpdu_t adv_b=advert(t,V,l->b,l->pb);
        int a_is_designated = bpdu_cmp(adv_a,adv_b) < 0;
        if(a_is_designated){
            t->role[l->a][l->pa]=ROLE_DESIGNATED;
            t->role[l->b][l->pb]=(t->root_port[l->b]==l->pb)?ROLE_ROOT:ROLE_BLOCKED;
        } else {
            t->role[l->b][l->pb]=ROLE_DESIGNATED;
            t->role[l->a][l->pa]=(t->root_port[l->a]==l->pa)?ROLE_ROOT:ROLE_BLOCKED;
        }
    }
    for(int i=0;i<t->nsw;i++)
        for(int p=0;p<STP_MAX_PORTS;p++)
            if(t->role[i][p]!=ROLE_NONE)
                t->state[i][p]=(t->role[i][p]==ROLE_BLOCKED)?STP_BLOCKING:STP_FORWARDING;
}

void stp_print(const stp_topo_t *t){
    char mb[18];
    printf("Root bridge: prio=%u mac=%s\n", t->root.prio, mac_str(t->root.mac,mb));
    for(int i=0;i<t->nsw;i++){
        printf("  SW%d (prio=%u mac=%s)%s\n", i, t->id[i].prio, mac_str(t->id[i].mac,mb),
               bridge_id_cmp(t->id[i],t->root)==0?"  <-- ROOT":"");
        for(int li=0; li<t->nlinks; li++){
            const stp_link_t *l=&t->link[li];
            if(l->a==i) printf("    port %d -> SW%d:  %-5s %s\n", l->pa, l->b,
                               stp_role_name(t->role[i][l->pa]), stp_state_name(t->state[i][l->pa]));
            if(l->b==i) printf("    port %d -> SW%d:  %-5s %s\n", l->pb, l->a,
                               stp_role_name(t->role[i][l->pb]), stp_state_name(t->state[i][l->pb]));
        }
    }
}
