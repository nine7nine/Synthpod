#ifndef _SYNTHPOD_APP_H
#define _SYNTHPOD_APP_H

#include <lilv/lilv.h>

#include <Eina.h>

#include <ext_urid.h>

#define APP_NUM_FEATURES 2

typedef struct _app_t app_t;

struct _app_t {
	LilvWorld *world;
	const LilvPlugins *plugs;

	ext_urid_t *ext_urid;

	struct {
		LilvNode *audio_uri;
		LilvNode *control_uri;
		LilvNode *cv_uri;
		LilvNode *atom_uri;

		LilvNode *input_uri;
		LilvNode *output_uri;
		//LilvNode *duplex_uri;

		LilvNode *midi_uri;
		LilvNode *osc_uri;
	} uris;

	struct {
		LV2_URID audio_urid;
		LV2_URID control_urid;
		LV2_URID cv_urid;
		LV2_URID atom_urid;

		LV2_URID input_urid;
		LV2_URID output_urid;
		//LV2_URID duplex_urid;

		LV2_URID midi_urid;
		LV2_URID osc_urid;
	} urids;

	Eina_Inlist *mods;

	LV2_Feature feature_list [APP_NUM_FEATURES];
	const LV2_Feature *features [APP_NUM_FEATURES + 1];
};

app_t *
app_new();

void
app_free(app_t *app);

void
app_run(app_t *app);

#endif // _SYNTHPOD_APP_H
