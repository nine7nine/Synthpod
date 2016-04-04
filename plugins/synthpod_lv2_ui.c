/*
 * Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#include <synthpod_lv2.h>
#include <sandbox_ui.h>

const LV2UI_Descriptor synthpod_common_1_ui = {
	.URI						= SYNTHPOD_COMMON_UI_URI,
	.instantiate		= sandbox_ui_instantiate,
	.cleanup				= sandbox_ui_cleanup,
	.port_event			= sandbox_ui_port_event,
	.extension_data	= sandbox_ui_extension_data
};

const LV2UI_Descriptor synthpod_common_2_kx = {
	.URI						= SYNTHPOD_COMMON_KX_URI,
	.instantiate		= sandbox_ui_instantiate,
	.cleanup				= sandbox_ui_cleanup,
	.port_event			= sandbox_ui_port_event,
	.extension_data	= NULL
};

const LV2UI_Descriptor synthpod_keyboard_1_ui = {
	.URI						= SYNTHPOD_KEYBOARD_UI_URI,
	.instantiate		= sandbox_ui_instantiate,
	.cleanup				= sandbox_ui_cleanup,
	.port_event			= sandbox_ui_port_event,
	.extension_data	= sandbox_ui_extension_data
};

const LV2UI_Descriptor synthpod_keyboard_2_kx = {
	.URI						= SYNTHPOD_KEYBOARD_KX_URI,
	.instantiate		= sandbox_ui_instantiate,
	.cleanup				= sandbox_ui_cleanup,
	.port_event			= sandbox_ui_port_event,
	.extension_data	= NULL
};

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const LV2UI_Descriptor*
lv2ui_descriptor(uint32_t index)
{
	switch(index)
	{
		case 0:
			return &synthpod_common_1_ui;
		case 1:
			return &synthpod_common_2_kx;

		case 2:
			return &synthpod_keyboard_1_ui;
		case 3:
			return &synthpod_keyboard_2_kx;

		default:
			return NULL;
	}
}
