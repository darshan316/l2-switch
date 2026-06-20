/* tap.h - thin wrapper over Linux TAP devices (Ethernet-level virtual NICs).
 * Each TAP interface becomes one physical port of the switch, so you can plug
 * real hosts / network namespaces into the software switch and ping across it. */
#ifndef TAP_H
#define TAP_H
#include <stddef.h>
/* Allocate a TAP device. If *name is non-empty it is requested; the actual
 * kernel-assigned name is written back. Returns fd or -1 on error. */
int  tap_open(char *name);
int  tap_read(int fd, unsigned char *buf, size_t cap);
int  tap_write(int fd, const unsigned char *buf, size_t len);
#endif
