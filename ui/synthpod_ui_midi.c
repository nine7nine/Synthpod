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

#include <synthpod_ui_private.h>

static const char *midi_keys [12] = {
	"C", "#C",
	"D", "#D",
	"E",
	"F", "#F",
	"G", "#G",
	"A", "#A",
	"H"
};

// ORDERED list of midi controller symbols
static const midi_controller_t midi_controllers [72] = {
	{ .controller = LV2_MIDI_CTL_MSB_BANK             , .symbol = "Bank Selection (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_MODWHEEL         , .symbol = "Modulation (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_BREATH           , .symbol = "Breath (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_FOOT             , .symbol = "Foot (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_PORTAMENTO_TIME  , .symbol = "Portamento Time (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_DATA_ENTRY       , .symbol = "Data Entry (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_MAIN_VOLUME      , .symbol = "Main Volume (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_BALANCE          , .symbol = "Balance (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_PAN              , .symbol = "Panpot (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_EXPRESSION       , .symbol = "Expression (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_EFFECT1          , .symbol = "Effect1 (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_EFFECT2          , .symbol = "Effect2 (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_GENERAL_PURPOSE1 , .symbol = "General Purpose 1 (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_GENERAL_PURPOSE2 , .symbol = "General Purpose 2 (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_GENERAL_PURPOSE3 , .symbol = "General Purpose 3 (MSB)" },
	{ .controller = LV2_MIDI_CTL_MSB_GENERAL_PURPOSE4 , .symbol = "General Purpose 4 (MSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_BANK             , .symbol = "Bank Selection (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_MODWHEEL         , .symbol = "Modulation (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_BREATH           , .symbol = "Breath (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_FOOT             , .symbol = "Foot (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_PORTAMENTO_TIME  , .symbol = "Portamento Time (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_DATA_ENTRY       , .symbol = "Data Entry (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_MAIN_VOLUME      , .symbol = "Main Volume (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_BALANCE          , .symbol = "Balance (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_PAN              , .symbol = "Panpot (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_EXPRESSION       , .symbol = "Expression (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_EFFECT1          , .symbol = "Effect1 (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_EFFECT2          , .symbol = "Effect2 (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_GENERAL_PURPOSE1 , .symbol = "General Purpose 1 (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_GENERAL_PURPOSE2 , .symbol = "General Purpose 2 (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_GENERAL_PURPOSE3 , .symbol = "General Purpose 3 (LSB)" },
	{ .controller = LV2_MIDI_CTL_LSB_GENERAL_PURPOSE4 , .symbol = "General Purpose 4 (LSB)" },
	{ .controller = LV2_MIDI_CTL_SUSTAIN              , .symbol = "Sustain Pedal" },
	{ .controller = LV2_MIDI_CTL_PORTAMENTO           , .symbol = "Portamento" },
	{ .controller = LV2_MIDI_CTL_SOSTENUTO            , .symbol = "Sostenuto" },
	{ .controller = LV2_MIDI_CTL_SOFT_PEDAL           , .symbol = "Soft Pedal" },
	{ .controller = LV2_MIDI_CTL_LEGATO_FOOTSWITCH    , .symbol = "Legato Foot Switch" },
	{ .controller = LV2_MIDI_CTL_HOLD2                , .symbol = "Hold2" },
	{ .controller = LV2_MIDI_CTL_SC1_SOUND_VARIATION  , .symbol = "SC1 Sound Variation" },
	{ .controller = LV2_MIDI_CTL_SC2_TIMBRE           , .symbol = "SC2 Timbre" },
	{ .controller = LV2_MIDI_CTL_SC3_RELEASE_TIME     , .symbol = "SC3 Release Time" },
	{ .controller = LV2_MIDI_CTL_SC4_ATTACK_TIME      , .symbol = "SC4 Attack Time" },
	{ .controller = LV2_MIDI_CTL_SC5_BRIGHTNESS       , .symbol = "SC5 Brightness" },
	{ .controller = LV2_MIDI_CTL_SC6                  , .symbol = "SC6" },
	{ .controller = LV2_MIDI_CTL_SC7                  , .symbol = "SC7" },
	{ .controller = LV2_MIDI_CTL_SC8                  , .symbol = "SC8" },
	{ .controller = LV2_MIDI_CTL_SC9                  , .symbol = "SC9" },
	{ .controller = LV2_MIDI_CTL_SC10                 , .symbol = "SC10" },
	{ .controller = LV2_MIDI_CTL_GENERAL_PURPOSE5     , .symbol = "General Purpose 5" },
	{ .controller = LV2_MIDI_CTL_GENERAL_PURPOSE6     , .symbol = "General Purpose 6" },
	{ .controller = LV2_MIDI_CTL_GENERAL_PURPOSE7     , .symbol = "General Purpose 7" },
	{ .controller = LV2_MIDI_CTL_GENERAL_PURPOSE8     , .symbol = "General Purpose 8" },
	{ .controller = LV2_MIDI_CTL_PORTAMENTO_CONTROL   , .symbol = "Portamento Control" },
	{ .controller = LV2_MIDI_CTL_E1_REVERB_DEPTH      , .symbol = "E1 Reverb Depth" },
	{ .controller = LV2_MIDI_CTL_E2_TREMOLO_DEPTH     , .symbol = "E2 Tremolo Depth" },
	{ .controller = LV2_MIDI_CTL_E3_CHORUS_DEPTH      , .symbol = "E3 Chorus Depth" },
	{ .controller = LV2_MIDI_CTL_E4_DETUNE_DEPTH      , .symbol = "E4 Detune Depth" },
	{ .controller = LV2_MIDI_CTL_E5_PHASER_DEPTH      , .symbol = "E5 Phaser Depth" },
	{ .controller = LV2_MIDI_CTL_DATA_INCREMENT       , .symbol = "Data Increment" },
	{ .controller = LV2_MIDI_CTL_DATA_DECREMENT       , .symbol = "Data Decrement" },
	{ .controller = LV2_MIDI_CTL_NRPN_LSB             , .symbol = "Non-registered Parameter Number (LSB)" },
	{ .controller = LV2_MIDI_CTL_NRPN_MSB             , .symbol = "Non-registered Parameter Number (MSB)" },
	{ .controller = LV2_MIDI_CTL_RPN_LSB              , .symbol = "Registered Parameter Number (LSB)" },
	{ .controller = LV2_MIDI_CTL_RPN_MSB              , .symbol = "Registered Parameter Number (MSB)" },
	{ .controller = LV2_MIDI_CTL_ALL_SOUNDS_OFF       , .symbol = "All Sounds Off" },
	{ .controller = LV2_MIDI_CTL_RESET_CONTROLLERS    , .symbol = "Reset Controllers" },
	{ .controller = LV2_MIDI_CTL_LOCAL_CONTROL_SWITCH , .symbol = "Local Control Switch" },
	{ .controller = LV2_MIDI_CTL_ALL_NOTES_OFF        , .symbol = "All Notes Off" },
	{ .controller = LV2_MIDI_CTL_OMNI_OFF             , .symbol = "Omni Off" },
	{ .controller = LV2_MIDI_CTL_OMNI_ON              , .symbol = "Omni On" },
	{ .controller = LV2_MIDI_CTL_MONO1                , .symbol = "Mono1" },
	{ .controller = LV2_MIDI_CTL_MONO2                , .symbol = "Mono2" }
};

const char *
_midi_note_lookup(float value)
{
	const uint8_t note = floor(value);

	const uint8_t octave = note / 12;
	const uint8_t offset = note % 12;

	static char stat_str [8];
	snprintf(stat_str, 8, "%s%u", midi_keys[offset], octave);

	return stat_str;
}

static inline const midi_controller_t *
_midi_controller_bsearch(uint8_t p, const midi_controller_t *a, unsigned n)
{
	unsigned start = 0;
	unsigned end = n;

	while(start < end)
	{
		const unsigned mid = start + (end - start)/2;
		const midi_controller_t *dst = &a[mid];

		if(p < dst->controller)
			end = mid;
		else if(p > dst->controller)
			start = mid + 1;
		else
			return dst;
	}

	return NULL;
}

const char *
_midi_controller_lookup(float value)
{
	const uint8_t cntrl = floor(value);

	const midi_controller_t *controller = _midi_controller_bsearch(cntrl, midi_controllers, 72);
	if(controller)
		return controller->symbol;

	static char stat_str [16];
	snprintf(stat_str, 16, "Controller #%"PRIu8, cntrl);

	return stat_str;
}
