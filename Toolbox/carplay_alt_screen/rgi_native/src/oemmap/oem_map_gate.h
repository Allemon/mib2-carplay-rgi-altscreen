/*
 * OEM cluster map startup gate - bus bridge
 *
 * Java polls MapKombi.isInitialized() on the OEM NavigationJobs thread and
 * sends CMD_OEM_MAP_READY once it is true.  This module forwards it to the
 * AltScreen preload (libcarplay_altscreen.so), which holds showUI /
 * forceKeyFrame and the display start until then.
 */

#ifndef OEM_MAP_GATE_H
#define OEM_MAP_GATE_H

#include "../framework/hook_framework.h"

/* Registered from hook_module_table (hook/main.c).  It stays enabled in the
 * INSTALL NO RGI mode: the second screen needs it either way. */
extern const hook_module_def_t oem_map_gate_module_def;

#endif /* OEM_MAP_GATE_H */
