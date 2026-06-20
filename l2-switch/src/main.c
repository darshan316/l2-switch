/* main.c - driver for the L2 switch.
 *   l2sw learn     scripted MAC-learning + flood/forward demo
 *   l2sw vlan      VLAN isolation + trunk tagging demo
 *   l2sw stp       multi-switch Spanning Tree convergence (loop breaking)
 *   l2sw tap p0 p1 [..]   live: bind each TAP iface to a switch port
 */
#include "switch.h"
#include "stp.h"
#include "tap.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- printing tx backend used by the scripted demos ---- */
static void print_tx(void *ctx,int port,const unsigned char *f,size_t len){
    (void)ctx; char d[18],s[18];
    printf("      -> egress port %d  [%s -> %s, %s%u bytes]\n",
           port, mac_str(eth_src(f),s), mac_str(eth_dst(f),d),
           eth_is_tagged(f)?"tagged ":"", (unsigned)len);
}
static mac_t M(const char *s){ mac_t m; mac_parse(s,&m); return m; }

static void inject(switch_t *sw,int in,const char *dst,const char *src,uint16_t vlan){
    uint8_t f[64]; const char *pl="hello"; uint8_t body[8]; memcpy(body,pl,5);
    size_t n=eth_build(f,M(dst),M(src),ETHERTYPE_IPV4,body,5);
    if(vlan){ uint8_t t[64]; n=eth_add_tag(t,f,n,vlan); memcpy(f,t,n); }
    char s[18],d[18];
    printf("  IN  port %d  vlan %u  %s -> %s\n", in, vlan?vlan:1,
           mac_str(M(src),s), mac_str(M(dst),d));
    switch_rx(sw,in,f,n,100);
}

static int demo_learn(void){
    printf("=== MAC learning demo (4 access ports, all VLAN 1) ===\n");
    switch_t sw; switch_init(&sw,4,print_tx,NULL);

    printf("\n[1] A(port0) -> B: B unknown, frame is FLOODED to all other ports\n");
    inject(&sw,0,"00:00:00:00:00:bb","00:00:00:00:00:aa",0);
    printf("\n[2] B(port2) -> A: A already learned, frame is FORWARDED to port0 only\n");
    inject(&sw,2,"00:00:00:00:00:aa","00:00:00:00:00:bb",0);
    printf("\n[3] A(port0) -> B: now B is learned too, FORWARDED to port2 only\n");
    inject(&sw,0,"00:00:00:00:00:bb","00:00:00:00:00:aa",0);
    printf("\n[4] A(port0) -> broadcast: always FLOODED\n");
    inject(&sw,0,"ff:ff:ff:ff:ff:ff","00:00:00:00:00:aa",0);

    printf("\nForwarding database:\n"); fdb_dump(&sw.fdb,100);
    printf("\nStats: "); switch_stats(&sw);
    switch_free(&sw); return 0;
}

static int demo_vlan(void){
    printf("=== VLAN demo ===\n");
    printf("port0=access vlan10  port1=access vlan20  port2=access vlan10  port3=trunk(10,20)\n");
    switch_t sw; switch_init(&sw,4,print_tx,NULL);
    switch_set_access(&sw,0,10);
    switch_set_access(&sw,1,20);
    switch_set_access(&sw,2,10);
    switch_set_trunk (&sw,3,1); switch_trunk_allow(&sw,3,10); switch_trunk_allow(&sw,3,20);

    printf("\n[1] vlan10 host on port0 broadcasts: reaches port2 (vlan10) and trunk port3\n");
    printf("    -- it must NOT reach port1 (vlan20). Tagged 10 on the trunk.\n");
    inject(&sw,0,"ff:ff:ff:ff:ff:ff","00:00:00:00:00:0a",0);

    printf("\n[2] vlan20 host on port1 broadcasts: reaches ONLY trunk port3 (tagged 20)\n");
    inject(&sw,1,"ff:ff:ff:ff:ff:ff","00:00:00:00:00:14",0);

    printf("\n[3] tagged vlan10 frame arrives on trunk port3 -> delivered untagged to vlan10 access ports\n");
    inject(&sw,3,"ff:ff:ff:ff:ff:ff","00:00:00:00:00:99",10);

    printf("\nStats: "); switch_stats(&sw);
    switch_free(&sw); return 0;
}

static int demo_stp(void){
    printf("=== Spanning Tree demo: 3 switches wired in a triangle (a loop) ===\n");
    printf("Without STP this loop would broadcast-storm. STP must block exactly one port.\n\n");
    stp_topo_t t; stp_topo_init(&t);
    int s0=stp_add_switch(&t,32768,M("00:00:00:00:00:01")); /* lowest MAC -> root */
    int s1=stp_add_switch(&t,32768,M("00:00:00:00:00:02"));
    int s2=stp_add_switch(&t,32768,M("00:00:00:00:00:03"));
    stp_add_link(&t,s0,1,s1,1,4);
    stp_add_link(&t,s1,2,s2,2,4);
    stp_add_link(&t,s2,3,s0,3,4);
    stp_converge(&t);
    stp_print(&t);

    int blocked=0;
    for(int i=0;i<t.nsw;i++) for(int p=0;p<STP_MAX_PORTS;p++)
        if(t.state[i][p]==STP_BLOCKING) blocked++;
    printf("\nResult: %d port(s) BLOCKING -> the triangle is now a loop-free tree.\n", blocked);
    return 0;
}

#ifdef __linux__
#include <sys/select.h>
#include <unistd.h>
static int g_fds[SW_MAX_PORTS];
static void tap_tx(void *ctx,int port,const unsigned char *f,size_t len){
    (void)ctx; if(g_fds[port]>=0) tap_write(g_fds[port],f,len);
}
static int run_tap(int argc,char **argv){
    int n=argc-2; if(n<2||n>SW_MAX_PORTS){ fprintf(stderr,"give 2..%d tap names\n",SW_MAX_PORTS); return 1; }
    switch_t sw; switch_init(&sw,n,tap_tx,NULL);
    int maxfd=0;
    for(int i=0;i<n;i++){
        char name[32]; strncpy(name,argv[i+2],31); name[31]=0;
        g_fds[i]=tap_open(name);
        if(g_fds[i]<0){ perror("tap_open"); fprintf(stderr,"(needs root / CAP_NET_ADMIN)\n"); return 1; }
        printf("port %d <-> %s (fd %d)\n",i,name,g_fds[i]);
        if(g_fds[i]>maxfd) maxfd=g_fds[i];
    }
    printf("switching... Ctrl-C to stop\n");
    for(;;){
        fd_set rs; FD_ZERO(&rs);
        for(int i=0;i<n;i++) FD_SET(g_fds[i],&rs);
        if(select(maxfd+1,&rs,NULL,NULL,NULL)<0) break;
        for(int i=0;i<n;i++) if(FD_ISSET(g_fds[i],&rs)){
            unsigned char b[ETH_MAX_FRAME];
            int r=tap_read(g_fds[i],b,sizeof b);
            if(r>0) switch_rx(&sw,i,b,(size_t)r,0);
        }
    }
    switch_free(&sw); return 0;
}
#endif

int main(int argc,char **argv){
    if(argc<2){
        printf("usage: %s {learn|vlan|stp|tap <if0> <if1> ...}\n",argv[0]); return 1;
    }
    if(!strcmp(argv[1],"learn")) return demo_learn();
    if(!strcmp(argv[1],"vlan"))  return demo_vlan();
    if(!strcmp(argv[1],"stp"))   return demo_stp();
#ifdef __linux__
    if(!strcmp(argv[1],"tap"))   return run_tap(argc,argv);
#endif
    fprintf(stderr,"unknown command '%s'\n",argv[1]); return 1;
}
