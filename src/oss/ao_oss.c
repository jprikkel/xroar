/*

OSS sound module

Copyright 2003-2016 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <unistd.h>

#include "xalloc.h"

#include "ao.h"
#include "logging.h"
#include "module.h"
#include "sound.h"
#include "xroar.h"

static void *new(void *cfg);

struct module ao_oss_module = {
	.name = "oss", .description = "OSS audio",
	.new = new,
};

struct ao_oss_interface {
	struct ao_interface public;

	int sound_fd;
	int fragment_nbytes;
	void *audio_buffer;
};

static void ao_oss_free(void *sptr);
static void *ao_oss_write_buffer(void *sptr, void *buffer);

static void *new(void *cfg) {
	(void)cfg;
	struct ao_oss_interface *aooss = xmalloc(sizeof(*aooss));
	*aooss = (struct ao_oss_interface){0};
	struct ao_interface *ao = &aooss->public;

	ao->free = DELEGATE_AS0(void, ao_oss_free, ao);

	const char *device = "/dev/dsp";
	if (xroar_cfg.ao_device)
		device = xroar_cfg.ao_device;

	aooss->sound_fd = open(device, O_WRONLY);
	if (aooss->sound_fd == -1) {
		LOG_ERROR("OSS: failed to open device\n");
		goto failed;
	}

	/* The order these ioctls are tried (format, channels, rate) is
	 * important: OSS docs say setting format can affect channels and
	 * sample rate, and setting channels can affect rate. */

	// Find a supported format
	int desired_format;
	switch (xroar_cfg.ao_format) {
	case SOUND_FMT_U8:
		desired_format = AFMT_U8;
		break;
	case SOUND_FMT_S8:
		desired_format = AFMT_S8;
		break;
	case SOUND_FMT_S16_BE:
		desired_format = AFMT_S16_BE;
		break;
	case SOUND_FMT_S16_LE:
		desired_format = AFMT_S16_LE;
		break;
	case SOUND_FMT_S16_HE:
	default:
		desired_format = AFMT_S16_NE;
		break;
	case SOUND_FMT_S16_SE:
		if (AFMT_S16_NE == AFMT_S16_LE)
			desired_format = AFMT_S16_BE;
		else
			desired_format = AFMT_S16_LE;
		break;
	}
	enum sound_fmt buffer_fmt;
	int format;
	int bytes_per_sample;
	if (ioctl(aooss->sound_fd, SNDCTL_DSP_GETFMTS, &format) == -1) {
		LOG_ERROR("OSS: SNDCTL_DSP_GETFMTS failed\n");
		goto failed;
	}
	if ((format & (AFMT_U8 | AFMT_S8 | AFMT_S16_LE | AFMT_S16_BE)) == 0) {
		LOG_ERROR("No desired audio formats supported by device\n");
		goto failed;
	}
	// if desired_format is one of those returned, use it:
	if (format & desired_format) {
		format = desired_format;
	}
	if (format & AFMT_S16_BE) {
		format = AFMT_S16_BE;
		buffer_fmt = SOUND_FMT_S16_BE;
		bytes_per_sample = 2;
	} else if (format & AFMT_S16_LE) {
		format = AFMT_S16_LE;
		buffer_fmt = SOUND_FMT_S16_LE;
		bytes_per_sample = 2;
	} else if (format & AFMT_S8) {
		format = AFMT_S8;
		buffer_fmt = SOUND_FMT_S8;
		bytes_per_sample = 1;
	} else {
		format = AFMT_U8;
		buffer_fmt = SOUND_FMT_U8;
		bytes_per_sample = 1;
	}
	if (ioctl(aooss->sound_fd, SNDCTL_DSP_SETFMT, &format) == -1) {
		LOG_ERROR("OSS: SNDCTL_DSP_SETFMT failed\n");
		goto failed;
	}

	// Set stereo if desired
	int nchannels = xroar_cfg.ao_channels - 1;
	if (nchannels < 0 || nchannels > 1)
		nchannels = 1;
	if (ioctl(aooss->sound_fd, SNDCTL_DSP_STEREO, &nchannels) == -1) {
		LOG_ERROR("OSS: SNDCTL_DSP_STEREO failed\n");
		goto failed;
	}
	nchannels++;

	// Set rate
	unsigned rate = 48000;
	if (xroar_cfg.ao_rate > 0)
		rate = xroar_cfg.ao_rate;
	if (ioctl(aooss->sound_fd, SNDCTL_DSP_SPEED, &rate) == -1) {
		LOG_ERROR("OSS: SNDCTL_DSP_SPEED failed\n");
		goto failed;
	}

	/* Now calculate and set the fragment parameter.  If fragment size args
	 * not specified, calculate based on buffer size args. */

	int nfragments = 2;
	int buffer_nframes = 0;
	int fragment_nframes = 0;

	if (xroar_cfg.ao_fragments >= 2 && xroar_cfg.ao_fragments < 0x8000) {
		nfragments = xroar_cfg.ao_fragments;
	}

	if (xroar_cfg.ao_fragment_ms > 0) {
		fragment_nframes = (rate * xroar_cfg.ao_fragment_ms) / 1000;
	} else if (xroar_cfg.ao_fragment_nframes > 0) {
		fragment_nframes = xroar_cfg.ao_fragment_nframes;
	} else {
		if (xroar_cfg.ao_buffer_ms > 0) {
			buffer_nframes = (rate * xroar_cfg.ao_buffer_ms) / 1000;
		} else if (xroar_cfg.ao_buffer_nframes > 0) {
			buffer_nframes = xroar_cfg.ao_buffer_nframes;
		} else {
			buffer_nframes = 1024;
		}
		fragment_nframes = buffer_nframes / nfragments;
	}

	// frag size selector is 2^N:
	int fbytes = fragment_nframes * bytes_per_sample * nchannels;
	aooss->fragment_nbytes = 16;
	int frag_size_sel = 4;
	while (aooss->fragment_nbytes < fbytes && frag_size_sel < 30) {
		frag_size_sel++;
		aooss->fragment_nbytes <<= 1;
	}

	// now piece together the ioctl:
	int frag = (nfragments << 16) | frag_size_sel;
	if (ioctl(aooss->sound_fd, SNDCTL_DSP_SETFRAGMENT, &frag) == -1) {
		LOG_ERROR("OSS: SNDCTL_DSP_SETFRAGMENT failed\n");
		goto failed;
	}
	// ioctl may have modified frag, so extract new values:
	nfragments = (frag >> 16) & 0x7fff;
	frag_size_sel = frag & 0xffff;
	if (frag_size_sel > 30) {
		LOG_ERROR("OSS: returned fragment size too large\n");
		goto failed;
	}
	aooss->fragment_nbytes = 1 << frag_size_sel;
	fragment_nframes = aooss->fragment_nbytes / (bytes_per_sample * nchannels);
	buffer_nframes = fragment_nframes * nfragments;

	aooss->audio_buffer = xmalloc(aooss->fragment_nbytes);
	ao->sound_interface = sound_interface_new(aooss->audio_buffer, buffer_fmt, rate, nchannels, fragment_nframes);
	if (!ao->sound_interface) {
		LOG_ERROR("Failed to initialise OSS: XRoar internal error\n");
		goto failed;
	}
	ao->sound_interface->write_buffer = DELEGATE_AS1(voidp, voidp, ao_oss_write_buffer, ao);
	LOG_DEBUG(1, "\t%d frags * %d frames/frag = %d frames buffer (%.1fms)\n", nfragments, fragment_nframes, buffer_nframes, (float)(buffer_nframes * 1000) / rate);

	ioctl(aooss->sound_fd, SNDCTL_DSP_RESET, 0);
	return aooss;

failed:
	if (aooss) {
		if (aooss->audio_buffer)
			free(aooss->audio_buffer);
		if (aooss->sound_fd != -1)
			close(aooss->sound_fd);
		free(aooss);
	}
	return NULL;
}

static void ao_oss_free(void *sptr) {
	struct ao_oss_interface *aooss = sptr;

	ioctl(aooss->sound_fd, SNDCTL_DSP_RESET, 0);
	close(aooss->sound_fd);
	sound_interface_free(aooss->public.sound_interface);
	free(aooss->audio_buffer);
	free(aooss);
}

static void *ao_oss_write_buffer(void *sptr, void *buffer) {
	struct ao_oss_interface *aooss = sptr;

	if (!aooss->public.sound_interface->ratelimit)
		return buffer;
	int r = write(aooss->sound_fd, buffer, aooss->fragment_nbytes);
	(void)r;
	return buffer;
}
