#include "ethernet.h"
#include <stdio.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p){ return (uint16_t)((p[0]<<8)|p[1]); }
static void     wr16(uint8_t *p, uint16_t v){ p[0]=v>>8; p[1]=v&0xff; }

bool mac_parse(const char *s, mac_t *out){
    unsigned v[6];
    if(sscanf(s,"%x:%x:%x:%x:%x:%x",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5])!=6)
        return false;
    for(int i=0;i<6;i++){ if(v[i]>0xff) return false; out->addr[i]=(uint8_t)v[i]; }
    return true;
}
char *mac_str(mac_t m, char *buf){
    sprintf(buf,"%02x:%02x:%02x:%02x:%02x:%02x",
            m.addr[0],m.addr[1],m.addr[2],m.addr[3],m.addr[4],m.addr[5]);
    return buf;
}
bool mac_equal(mac_t a, mac_t b){ return memcmp(a.addr,b.addr,ETH_ALEN)==0; }
bool mac_is_broadcast(mac_t m){
    for(int i=0;i<6;i++) if(m.addr[i]!=0xff) return false;
    return true;
}
bool mac_is_multicast(mac_t m){ return m.addr[0]&0x01; }
bool mac_is_bpdu(mac_t m){
    static const uint8_t b[6]={0x01,0x80,0xc2,0x00,0x00,0x00};
    return memcmp(m.addr,b,6)==0;
}
uint32_t mac_hash(uint16_t vlan, mac_t m){           /* FNV-1a */
    uint32_t h=2166136261u;
    h=(h^(vlan&0xff))*16777619u; h=(h^(vlan>>8))*16777619u;
    for(int i=0;i<6;i++) h=(h^m.addr[i])*16777619u;
    return h;
}

mac_t eth_dst(const uint8_t *f){ mac_t m; memcpy(m.addr,f,6); return m; }
mac_t eth_src(const uint8_t *f){ mac_t m; memcpy(m.addr,f+6,6); return m; }
bool  eth_is_tagged(const uint8_t *f){ return rd16(f+12)==ETHERTYPE_VLAN; }
uint16_t eth_vlan(const uint8_t *f){ return rd16(f+14)&0x0fff; }
uint16_t eth_ethertype(const uint8_t *f){
    return eth_is_tagged(f) ? rd16(f+16) : rd16(f+12);
}

size_t eth_build(uint8_t *out, mac_t dst, mac_t src, uint16_t et,
                 const uint8_t *payload, size_t plen){
    memcpy(out,dst.addr,6); memcpy(out+6,src.addr,6); wr16(out+12,et);
    if(payload && plen) memcpy(out+14,payload,plen);
    return ETH_HLEN+plen;
}
size_t eth_add_tag(uint8_t *out, const uint8_t *in, size_t in_len, uint16_t vlan){
    if(eth_is_tagged(in)){ memcpy(out,in,in_len); wr16(out+14,(rd16(in+14)&0xf000)|(vlan&0x0fff)); return in_len; }
    memcpy(out,in,12);                 /* dst+src */
    wr16(out+12,ETHERTYPE_VLAN);
    wr16(out+14,vlan&0x0fff);          /* PCP/DEI 0 */
    memcpy(out+16,in+12,in_len-12);    /* ethertype + payload */
    return in_len+4;
}
size_t eth_strip_tag(uint8_t *out, const uint8_t *in, size_t in_len){
    if(!eth_is_tagged(in)){ memcpy(out,in,in_len); return in_len; }
    memcpy(out,in,12);
    memcpy(out+12,in+16,in_len-16);
    return in_len-4;
}
