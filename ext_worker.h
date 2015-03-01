#ifndef _SYNTHPOD_EXT_WORKER_H
#define _SYNTHPOD_EXT_WORKER_H

#include <lv2/lv2plug.in/ns/ext/worker/worker.h>

typedef struct _ext_worker_t ext_worker_t;

ext_worker_t *
ext_worker_new();

void
ext_worker_free(ext_worker_t *worker);

#endif // _SYNTHPOD_EXT_WORKER_H
