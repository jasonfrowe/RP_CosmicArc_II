/* Starport Tabbed Menus: Commodities, Bio-Bounty, Shipyard, Safe-Space Bank */

#ifndef PORT_H
#define PORT_H

#include <stdbool.h>

void port_enter(void);
bool port_update(void); /* returns true when player leaves port */

#endif /* PORT_H */
