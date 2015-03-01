#ifndef _SYNTHPOD_EXT_URID_H
#define _SYNTHPOD_EXT_URID_H

#include <lv2/lv2plug.in/ns/ext/urid/urid.h>

typedef struct _ext_urid_t ext_urid_t;

ext_urid_t *
ext_urid_new();

void
ext_urid_free(ext_urid_t *ext_urid);

LV2_URID_Map *
ext_urid_map_get(ext_urid_t *ext_urid);

LV2_URID_Unmap *
ext_urid_unmap_get(ext_urid_t *ext_urid);

LV2_URID
ext_urid_map(ext_urid_t *ext_urid, const char *uri);

const char *
ext_urid_unmap(ext_urid_t *ext_urid, const LV2_URID urid);

#endif // _SYNTHPOD_EXT_URID_H
