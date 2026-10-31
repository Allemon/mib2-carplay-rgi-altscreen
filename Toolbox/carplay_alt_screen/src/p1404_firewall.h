/* p1404_firewall.h - runtime-scoped CarPlay type-111 PF aperture. */
#ifndef P1404_FIREWALL_H
#define P1404_FIREWALL_H
#include <stddef.h>
#include <stdint.h>
/* Value-only cleanup ownership: no receiver/ScreenSession pointer survives. */
struct p1404_pf_lease { uint16_t port; uint32_t generation; };
void p1404_alt111_firewall_maintenance_start(void);
int p1404_alt111_firewall_open(uint16_t port);
int p1404_alt111_firewall_close(uint16_t port);
int p1404_alt111_firewall_open_owned(uint16_t port, struct p1404_pf_lease *lease);
/* As open_owned. *port_conflict=1 means the exact-port UDP loopback owner token
 * was already bound by another socket; no PF command ran, so the caller may
 * retry on a different listener port. */
int p1404_alt111_firewall_open_owned_ex(uint16_t port, struct p1404_pf_lease *lease,
                                        int *port_conflict);
int p1404_alt111_firewall_close_owned(const struct p1404_pf_lease *lease);
/* Fixed type-111 data port. INSTALL writes the static rule below into
 * /mnt/system/etc/pf.conf ahead of the carplay0 terminal block; startup.sh
 * copies it to /etc and loads it, so no PF command runs during SETUP. */
#define P1404_ALT111_FIXED_DATA_PORT 6031u
/* 1 when `conf` contains the exact static pass rule for `port` before the
 * first terminal "block ... in ... on carplay0 all" line. */
int p1404_alt111_static_rule_present(const char *conf, uint16_t port);
/* Same check against /etc/pf.conf, the copy loaded at this boot. */
int p1404_alt111_static_rule_loaded(uint16_t port);
int p1404_alt111_firewall_rewrite(const char *rules, uint16_t port, int enable,
                                  char *out, size_t out_cap, int *changed_out);
#endif
