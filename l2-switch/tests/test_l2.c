/* Minimal self-contained test harness (no external framework). */
#include "switch.h"
#include "stp.h"
#include <stdio.h>
#include <string.h>

static int g_pass=0, g_fail=0;
#define CHECK(cond,msg) do{ if(cond){g_pass++;} \
    else{g_fail++; printf("  FAIL: %s (%s:%d)\n",msg,__FILE__,__LINE__);} }while(0)

static mac_t M(const char*s){ mac_t m; mac_parse(s,&m); return m; }

/* capture tx: record which ports a frame went out, and whether tagged */
#define CAP_MAX 16
static int  cap_ports[CAP_MAX], cap_tag[CAP_MAX], cap_n;
static void cap_reset(void){ cap_n=0; }
static void cap_tx(void*ctx,int port,const unsigned char*f,size_t len){
    (void)ctx;(void)len; if(cap_n<CAP_MAX){ cap_ports[cap_n]=port; cap_tag[cap_n]=eth_is_tagged(f); cap_n++; }
}
static int went_to(int port){ for(int i=0;i<cap_n;i++) if(cap_ports[i]==port) return 1; return 0; }

static void send(switch_t*sw,int in,const char*dst,const char*src,uint16_t vlan){
    uint8_t f[64]; uint8_t body[4]={1,2,3,4};
    size_t n=eth_build(f,M(dst),M(src),ETHERTYPE_IPV4,body,4);
    if(vlan){ uint8_t t[64]; n=eth_add_tag(t,f,n,vlan); memcpy(f,t,n); }
    cap_reset(); switch_rx(sw,in,f,n,100);
}

static void test_fdb(void){
    printf("[fdb]\n");
    fdb_t t; fdb_init(&t);
    CHECK(fdb_lookup(&t,1,M("00:00:00:00:00:aa"),100)==-1,"unknown -> miss");
    fdb_learn(&t,1,M("00:00:00:00:00:aa"),3,100);
    CHECK(fdb_lookup(&t,1,M("00:00:00:00:00:aa"),100)==3,"learned -> port 3");
    CHECK(fdb_lookup(&t,2,M("00:00:00:00:00:aa"),100)==-1,"other vlan isolated");
    fdb_learn(&t,1,M("00:00:00:00:00:aa"),5,200);   /* station moved */
    CHECK(fdb_lookup(&t,1,M("00:00:00:00:00:aa"),200)==5,"move updates port");
    CHECK(fdb_lookup(&t,1,M("00:00:00:00:00:aa"),200+FDB_AGING_SECS+1)==-1,"entry ages out");
    fdb_free(&t);
}

static void test_forwarding(void){
    printf("[forwarding]\n");
    switch_t sw; switch_init(&sw,4,cap_tx,NULL);
    send(&sw,0,"00:00:00:00:00:bb","00:00:00:00:00:aa",0);   /* unknown -> flood */
    CHECK(cap_n==3 && went_to(1)&&went_to(2)&&went_to(3) && !went_to(0),"unknown unicast floods to all but ingress");
    send(&sw,2,"00:00:00:00:00:aa","00:00:00:00:00:bb",0);   /* A known on port0 */
    CHECK(cap_n==1 && went_to(0),"known unicast forwarded to single port");
    send(&sw,0,"ff:ff:ff:ff:ff:ff","00:00:00:00:00:aa",0);
    CHECK(cap_n==3,"broadcast floods");
    switch_free(&sw);
}

static void test_vlan(void){
    printf("[vlan]\n");
    switch_t sw; switch_init(&sw,4,cap_tx,NULL);
    switch_set_access(&sw,0,10);
    switch_set_access(&sw,1,20);
    switch_set_access(&sw,2,10);
    switch_set_trunk (&sw,3,1); switch_trunk_allow(&sw,3,10); switch_trunk_allow(&sw,3,20);
    send(&sw,0,"ff:ff:ff:ff:ff:ff","00:00:00:00:00:0a",0);   /* vlan10 broadcast */
    CHECK(went_to(2),"vlan10 reaches other vlan10 access port");
    CHECK(!went_to(1),"vlan10 does NOT leak to vlan20 port");
    CHECK(went_to(3),"vlan10 reaches trunk");
    int trunk_tagged=0; for(int i=0;i<cap_n;i++) if(cap_ports[i]==3) trunk_tagged=cap_tag[i];
    CHECK(trunk_tagged,"frame is tagged on the trunk");
    int access_untagged=1; for(int i=0;i<cap_n;i++) if(cap_ports[i]==2 && cap_tag[i]) access_untagged=0;
    CHECK(access_untagged,"frame is untagged on access port");
    switch_free(&sw);
}

static void test_stp(void){
    printf("[stp]\n");
    stp_topo_t t; stp_topo_init(&t);
    stp_add_switch(&t,32768,M("00:00:00:00:00:01"));
    stp_add_switch(&t,32768,M("00:00:00:00:00:02"));
    stp_add_switch(&t,32768,M("00:00:00:00:00:03"));
    stp_add_link(&t,0,1,1,1,4);
    stp_add_link(&t,1,2,2,2,4);
    stp_add_link(&t,2,3,0,3,4);
    stp_converge(&t);
    CHECK(bridge_id_cmp(t.root,t.id[0])==0,"lowest bridge id is elected root");
    int blocked=0,fwd=0;
    for(int i=0;i<t.nsw;i++) for(int p=0;p<STP_MAX_PORTS;p++){
        if(t.state[i][p]==STP_BLOCKING) blocked++;
        if(t.state[i][p]==STP_FORWARDING) fwd++;
    }
    CHECK(blocked==1,"exactly one port blocks to break the single loop");
    CHECK(fwd==5,"remaining 5 port-ends forward (tree has n-1 active links here)");
}

int main(void){
    test_fdb(); test_forwarding(); test_vlan(); test_stp();
    printf("\n%d passed, %d failed\n",g_pass,g_fail);
    return g_fail?1:0;
}
