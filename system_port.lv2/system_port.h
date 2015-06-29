/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#ifndef SYSTEM_PORT_H
#define SYSTEM_PORT_H

#include <stdint.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define SYSTEM_PORT_URI "http://open-music-kontrollers.ch/lv2/system-port"
#define SYSTEM_PORT_PREFIX SYSTEM_PORT_URI "#"

#define SYSTEM_PORT__interface		SYSTEM_PORT_PREFIX "interface"
#define SYSTEM_PORT__dynamicPorts	SYSTEM_PORT_PREFIX "dynamicPorts"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _System_Port_Type {
	SYSTEM_PORT_NONE,

	SYSTEM_PORT_CONTROL,

	SYSTEM_PORT_AUDIO,
	SYSTEM_PORT_CV,

	SYSTEM_PORT_MIDI,
	SYSTEM_PORT_OSC
} System_Port_Type;

// instantiation thread
typedef System_Port_Type
(*System_Port_Query_Function)(LV2_Handle instance, uint32_t port);

typedef struct _System_Port_Interface {
	System_Port_Query_Function query;	
} System_Port_Interface;

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* SYSTEM_PORT_H */
