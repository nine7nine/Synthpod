#include <ext_worker.h>

#include <Eina.h>

struct _ext_worker_t {
	LV2_Worker_Schedule schedule;
	LV2_Worker_Interface *interface;
};

static LV2_Worker_Status
_worker_schedule(LV2_Worker_Schedule_Handle handle, uint32_t size, const void *data)
{
	//TODO ringbuffer etc.

	return LV2_WORKER_SUCCESS;
}

ext_worker_t *
ext_worker_new()
{
	ext_worker_t *ext_worker = malloc(sizeof(ext_worker_t));

	//TODO initialize
	ext_worker->schedule.handle = ext_worker;
	ext_worker->schedule.schedule_work = _worker_schedule;

	return ext_worker;
}

void
ext_worker_free(ext_worker_t *ext_worker)
{
	free(ext_worker);
}
