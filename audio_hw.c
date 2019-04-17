/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_primary"
/*#define LOG_NDEBUG 0*/
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <fcntl.h>

#include <cutils/log.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <cutils/list.h>

#include <hardware/hardware.h>
#include <hardware/audio.h>
#include <hardware/audio_alsaops.h>
#include <system/audio.h>

#include <tinyalsa/asoundlib.h>
#include <audio_route/audio_route.h>

#include <pthread.h>

#ifdef USES_NXVOICE
#include <nx-smartvoice.h>
#include <ecnr_wrapper.h>
#endif

#define MIXER_CARD 0
#define MIXER_XML_PATH "/system/etc/mixer_paths.xml"

#define MAX_SUPPORTED_CHANNEL_MASKS 2

#define DEFAULT_OUTPUT_SAMPLING_RATE 48000

#define DEFAULT_OUTPUT_PERIOD_SIZE 1024
#define DEFAULT_OUTPUT_PERIOD_COUNT 4

#define DEEP_BUFFER_OUTPUT_PERIOD_SIZE 1920
#define DEEP_BUFFER_OUTPUT_PERIOD_COUNT 4
#define LOW_LATENCY_OUTPUT_PERIOD_SIZE 240
#define LOW_LATENCY_OUTPUT_PERIOD_COUNT 2

#define AUDIO_CAPTURE_PERIOD_DURATION_MSEC 20
#define AUDIO_CAPTURE_PERIOD_COUNT	2

#define LOW_LATENCY_CAPTURE_SAMPLE_RATE 48000
#define LOW_LATENCY_CAPTURE_PERIOD_SIZE 240

#define MIN_CHANNEL_COUNT		1
#define MAX_CHANNEL_COUNT		2
#define DEFAULT_CHANNEL_COUNT	2

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifdef USES_NXVOICE
#define NXAUDIO_INPUT_BUFFER_SIZE		(256 * 1 * 2 * 4)

static const char *USE_NXVOICE_PROP_KEY = "persist.nv.use_nxvoice";
static const char *VOICE_VENDOR_PROP_KEY = "persist.nv.voice_vendor";
static const char *USE_FEEDBACK_PROP_KEY = "persist.nv.use_feedback";
static const char *PDM_DEVNUM_PROP_KEY = "persist.nv.pdm_devnum";
static const char *REF_DEVNUM_PROP_KEY = "persist.nv.ref_devnum";
static const char *FEEDBACK_DEVNUM_PROP_KEY = "persist.nv.feedback_devnum";
static const char *PDM_CHNUM_PROP_KEY = "persist.nv.pdm_chnum";
static const char *PDM_GAIN_PROP_KEY = "persist.nv.pdm_gain";
static const char *RESAMPLE_OUT_CHNUM_PROP_KEY = "persist.nv.resample_out_chnum";
static const char *SAMPLE_COUNT_PROP_KEY = "persist.nv.sample_count";
static const char *CHECK_TRIGGER_PROP_KEY = "persist.nv.check_trigger";
static const char *TRIGGER_DONE_RET_VALUE_PROP_KEY = "persist.nv.trigger_done_ret";
static const char *PASS_AFTER_TRIGGER_PROP_KEY = "persist.nv.pass_after_trigger";
static const char *NXVOICE_VERBOSE_PROP_KEY = "persist.nv.nxvoice_verbose";
#endif

/*
 * tinyAlsa library interprets period size as number of frames
 * one frame = channel_count * sizeof (pcm sample)
 * so if format = 16-bit PCM and channels = Stereo, frame size = 2 ch * 2 = 4 bytes
 * DEEP_BUFFER_OUTPUT_PERIOD_SIZE = 1024 means 1024 * 4 = 4096 bytes
 * We should take care of returning proper size when AudioFlinger queries for
 * the buffer size of an input/output stream
 */

struct stream_out {
	struct audio_stream_out stream;
	pthread_mutex_t lock; /* see note below on mutex acquisition order */
	pthread_mutex_t pre_lock; /* acquire before lock to avoid DOS by playback thread */
	struct pcm_config config;
	struct pcm *pcm;

	int standby;
	int pcm_device_id;
	int spdif_id;

	unsigned int sample_rate;

	audio_channel_mask_t channel_mask;
	audio_format_t format;
	audio_devices_t devices;
	audio_output_flags_t flags;

	/* Array of supported channel mask configurations. +1 so that the last entry is always 0 */
	audio_channel_mask_t supported_channel_masks[MAX_SUPPORTED_CHANNEL_MASKS + 1];
	bool muted;

	uint64_t written; /* total frames written, not cleared when entering standby */
	audio_io_handle_t handle;

	struct audio_device *dev;

	int64_t last_write_time_us;
};

struct stream_in {
	struct audio_stream_in stream;
	pthread_mutex_t lock; /* see note below on mutex acquisition order */
	pthread_mutex_t pre_lock; /* acquire before lock to avoid DOS by capture thread */
	struct pcm_config config;
	struct pcm *pcm;

	int standby;
	int pcm_device_id;

	audio_channel_mask_t channel_mask;
	audio_format_t format;
	audio_devices_t device;
	audio_input_flags_t flags;

	int64_t frames_read; /* total frames read, not cleared when entering standby */
	audio_io_handle_t capture_handle;

	int source;

	struct audio_device *dev;

	int64_t last_read_time_us;
};

bool hfp_enable = false;
bool output_streaming = false;

#ifdef USES_NXVOICE
struct nxvoice_stream_in {
	struct audio_stream_in stream;
	pthread_mutex_t lock; /* see note below on mutex acquisition order */
	pthread_mutex_t pre_lock; /* acquire before lock to avoid DOS by capture thread */

	bool standby;

	audio_channel_mask_t channel_mask;
	audio_format_t format;
	audio_devices_t device;

	uint32_t sample_rate;
	audio_io_handle_t io_handle;

	int source;

	struct audio_device *dev;
};
#endif

struct audio_device {
	struct audio_hw_device device;
	audio_devices_t out_device;
	audio_devices_t in_device;

	pthread_mutex_t lock; /* see note below on mutex acquisition order */

	struct mixer *mixer;

	audio_mode_t mode;
	struct stream_in *active_input;
	struct stream_out *primary_output;

	int *snd_dev_ref_cnt;

	struct audio_route *audio_route;
	audio_source_t input_source;
	int cur_route_id;

	float voice_volume;
	bool voice_mic_mute;
	bool mic_muted;

	int snd_card;

#ifdef USES_NXVOICE
	bool use_nxvoice;
	struct nx_smartvoice_config nxvoice_config;
	void *nxvoice_handle;
#endif
};

static unsigned int configured_low_latency_capture_period_size =
	LOW_LATENCY_CAPTURE_PERIOD_SIZE;

struct pcm_config pcm_config_deep_buffer = {
	.channels = DEFAULT_CHANNEL_COUNT,
	.rate = DEFAULT_OUTPUT_SAMPLING_RATE,
	.period_size = DEEP_BUFFER_OUTPUT_PERIOD_SIZE,
	.period_count = DEEP_BUFFER_OUTPUT_PERIOD_COUNT,
	.format = PCM_FORMAT_S16_LE,
	.start_threshold = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
	.stop_threshold = INT_MAX,
	.avail_min = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_low_latency = {
	.channels = DEFAULT_CHANNEL_COUNT,
	.rate = DEFAULT_OUTPUT_SAMPLING_RATE,
	.period_size = LOW_LATENCY_OUTPUT_PERIOD_SIZE,
	.period_count = LOW_LATENCY_OUTPUT_PERIOD_COUNT,
	.format = PCM_FORMAT_S16_LE,
	.start_threshold = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
	.stop_threshold = INT_MAX,
	.avail_min = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_hdmi = {
	.channels = DEFAULT_CHANNEL_COUNT,
	.rate = DEFAULT_OUTPUT_SAMPLING_RATE,
	.period_size = DEFAULT_OUTPUT_PERIOD_SIZE,
	.period_count = DEFAULT_OUTPUT_PERIOD_COUNT,
	.format = PCM_FORMAT_S16_LE,
	.start_threshold = DEFAULT_OUTPUT_PERIOD_SIZE / 4,
	.stop_threshold = INT_MAX,
	.avail_min = DEFAULT_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_audio_capture = {
	.channels = DEFAULT_CHANNEL_COUNT,
	.period_count = AUDIO_CAPTURE_PERIOD_COUNT,
	.format = PCM_FORMAT_S16_LE,
	.stop_threshold = INT_MAX,
	.avail_min = 0,
};

struct pcm_config pcm_config_bt_sco = {
    .channels = 1,
    .rate = 16000,
    .period_size = 128,
    .period_count = 8,
    .format = PCM_FORMAT_S16_LE,
};

#define STRING_TO_ENUM(string) { #string, string }

struct string_to_enum {
	const char *name;
	uint32_t value;
};

static const struct string_to_enum out_channels_name_to_enum_table[] = {
	STRING_TO_ENUM(AUDIO_CHANNEL_OUT_STEREO),
	STRING_TO_ENUM(AUDIO_CHANNEL_OUT_5POINT1),
	STRING_TO_ENUM(AUDIO_CHANNEL_OUT_7POINT1),
};

enum {
	OUT_DEVICE_SPEAKER,
	OUT_DEVICE_HEADSET,
	OUT_DEVICE_HEADPHONES,
	OUT_DEVICE_BT_SCO,
	OUT_DEVICE_SPEAKER_AND_HEADSET,
	OUT_DEVICE_TAB_SIZE,           /* number of rows in route_configs[][] */
	OUT_DEVICE_NONE,
	OUT_DEVICE_CNT
};

enum {
	IN_SOURCE_MIC,
	IN_SOURCE_CAMCORDER,
	IN_SOURCE_VOICE_RECOGNITION,
	IN_SOURCE_VOICE_COMMUNICATION,
	IN_SOURCE_TAB_SIZE,            /* number of lines in route_configs[][] */
	IN_SOURCE_NONE,
	IN_SOURCE_CNT
};

int get_output_device_id(audio_devices_t device)
{
	if (device == AUDIO_DEVICE_NONE)
		return OUT_DEVICE_NONE;

	if (popcount(device) == 2) {
		if ((device == (AUDIO_DEVICE_OUT_SPEAKER |
						AUDIO_DEVICE_OUT_WIRED_HEADSET)) ||
			(device == (AUDIO_DEVICE_OUT_SPEAKER |
						AUDIO_DEVICE_OUT_WIRED_HEADPHONE)))
			return OUT_DEVICE_SPEAKER_AND_HEADSET;
		else
			return OUT_DEVICE_NONE;
	}

	if (popcount(device) != 1)
		return OUT_DEVICE_NONE;

	switch (device) {
	case AUDIO_DEVICE_OUT_SPEAKER:
		return OUT_DEVICE_SPEAKER;
	case AUDIO_DEVICE_OUT_WIRED_HEADSET:
		return OUT_DEVICE_HEADSET;
	case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
		return OUT_DEVICE_HEADPHONES;
    case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
    case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
    case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
		return OUT_DEVICE_BT_SCO;
	default:
		return OUT_DEVICE_NONE;
	}
}

int get_input_source_id(audio_source_t source)
{
	switch (source) {
	case AUDIO_SOURCE_DEFAULT:
		return IN_SOURCE_NONE;
	case AUDIO_SOURCE_MIC:
		return IN_SOURCE_MIC;
	case AUDIO_SOURCE_CAMCORDER:
		return IN_SOURCE_CAMCORDER;
	case AUDIO_SOURCE_VOICE_RECOGNITION:
		return IN_SOURCE_VOICE_RECOGNITION;
	case AUDIO_SOURCE_VOICE_COMMUNICATION:
		return IN_SOURCE_VOICE_COMMUNICATION;
	default:
		return IN_SOURCE_NONE;
	}
}

struct route_config {
	const char * const output_route;
	const char * const input_route;
};

const struct route_config media_speaker = {
	"media-speaker",
	"media-main-mic",
};

const struct route_config media_headphones = {
	"media-headphones",
	"media-main-mic",
};

const struct route_config media_headset = {
	"media-headphones",
	"media-headset-mic",
};

const struct route_config camcorder_speaker = {
	"media-speaker",
	"media-second-mic",
};

const struct route_config camcorder_headphones = {
	"media-headphones",
	"media-second-mic",
};

const struct route_config voice_rec_speaker = {
	"voice-rec-speaker",
	"voice-rec-main-mic",
};

const struct route_config voice_rec_headphones = {
	"voice-rec-headphones",
	"voice-rec-main-mic",
};

const struct route_config voice_rec_headset = {
	"voice-rec-headphones",
	"voice-rec-headset-mic",
};

const struct route_config communication_speaker = {
	"communication-speaker",
	"communication-main-mic",
};

const struct route_config communication_headphones = {
	"communication-headphones",
	"communication-main-mic",
};

const struct route_config communication_headset = {
	"communication-headphones",
	"communication-headset-mic",
};

const struct route_config speaker_and_headphones = {
	"speaker-and-headphones",
	"main-mic",
};

const struct route_config bluetooth_sco = {
    "bt-sco-headset",
    "bt-sco-mic",
};

const struct route_config * const route_configs[IN_SOURCE_TAB_SIZE]
[OUT_DEVICE_TAB_SIZE] = {
	{   /* IN_SOURCE_MIC */
		&media_speaker,             /* OUT_DEVICE_SPEAKER */
		&media_headset,             /* OUT_DEVICE_HEADSET */
		&media_headphones,          /* OUT_DEVICE_HEADPHONES */
		&bluetooth_sco,             /* OUT_DEVICE_BT_SCO */
		&speaker_and_headphones     /* OUT_DEVICE_SPEAKER_AND_HEADSET */
	},
	{   /* IN_SOURCE_CAMCORDER */
		&camcorder_speaker,         /* OUT_DEVICE_SPEAKER */
		&camcorder_headphones,      /* OUT_DEVICE_HEADSET */
		&camcorder_headphones,      /* OUT_DEVICE_HEADPHONES */
		&bluetooth_sco,             /* OUT_DEVICE_BT_SCO */
		&speaker_and_headphones     /* OUT_DEVICE_SPEAKER_AND_HEADSET */
	},
	{   /* IN_SOURCE_VOICE_RECOGNITION */
		&voice_rec_speaker,         /* OUT_DEVICE_SPEAKER */
		&voice_rec_headset,         /* OUT_DEVICE_HEADSET */
		&voice_rec_headphones,      /* OUT_DEVICE_HEADPHONES */
		&bluetooth_sco,             /* OUT_DEVICE_BT_SCO */
		&speaker_and_headphones     /* OUT_DEVICE_SPEAKER_AND_HEADSET */
	},
	{   /* IN_SOURCE_VOICE_COMMUNICATION */
		&communication_speaker,     /* OUT_DEVICE_SPEAKER */
		&communication_headset,     /* OUT_DEVICE_HEADSET */
		&communication_headphones,  /* OUT_DEVICE_HEADPHONES */
		&bluetooth_sco,             /* OUT_DEVICE_BT_SCO */
		&speaker_and_headphones     /* OUT_DEVICE_SPEAKER_AND_HEADSET */
	}
};

/**
 * NOTE: when multiple mutexes have to be acquired, always respect the
 * following order: hw device > in stream > out stream
 */

/* Helper functions */

static void select_devices(struct audio_device *adev)
{
	int output_device_id = get_output_device_id(adev->out_device);
	int input_source_id = get_input_source_id(adev->input_source);
	const char *output_route = NULL;
	const char *input_route = NULL;
	int new_route_id;

	ALOGV("%s: enter >> %d : %d", __func__, output_device_id, input_source_id);

	if (!adev->audio_route)
		goto err_route;

	audio_route_reset(adev->audio_route);

	new_route_id = (1 << (input_source_id + OUT_DEVICE_CNT)) +
		(1 << output_device_id);
	if (new_route_id == adev->cur_route_id)
		return;
	adev->cur_route_id = new_route_id;

	if (input_source_id != IN_SOURCE_NONE) {
		if (output_device_id != OUT_DEVICE_NONE) {
			input_route =
				route_configs[input_source_id][output_device_id]->input_route;
			output_route =
				route_configs[input_source_id][output_device_id]->output_route;
		} else {
			switch (adev->in_device) {
			case AUDIO_DEVICE_IN_WIRED_HEADSET & ~AUDIO_DEVICE_BIT_IN:
				output_device_id = OUT_DEVICE_HEADSET;
				break;
			case AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET & ~AUDIO_DEVICE_BIT_IN:
				output_device_id = OUT_DEVICE_BT_SCO;
				break;
			default:
				output_device_id = OUT_DEVICE_SPEAKER;
				break;
			}
			input_route =
				route_configs[input_source_id][output_device_id]->input_route;
		}
	} else {
		if (output_device_id != OUT_DEVICE_NONE) {
			output_route =
				route_configs[IN_SOURCE_MIC][output_device_id]->output_route;
		}
	}

	ALOGV("select_devices() devices %#x input src %d output route %s input route %s",
		  adev->out_device, adev->input_source,
		  output_route ? output_route : "none",
		  input_route ? input_route : "none");

	if (output_route)
		audio_route_apply_path(adev->audio_route, output_route);
	if (input_route)
		audio_route_apply_path(adev->audio_route, input_route);

	audio_route_update_mixer(adev->audio_route);

err_route:
	ALOGV("%s: exit >> %d : %d", __func__, output_device_id, input_source_id);
}

#include "audio_hfp_client_hw.c"

static struct audio_device *adev = NULL;
static pthread_mutex_t adev_init_lock;
static unsigned int audio_device_ref_count;

int stop_input_stream(struct stream_in *in)
{
	int ret = 0;
	struct audio_device *adev = in->dev;

	ALOGV("%s: (%d:%d)", __func__, in->flags, in->device);

	adev->active_input = NULL;

	adev->input_source = AUDIO_SOURCE_DEFAULT;
	adev->in_device = AUDIO_DEVICE_NONE;
	select_devices(adev);

	ALOGV("%s: exit: status(%d)", __func__, ret);
	return ret;
}

int start_input_stream(struct stream_in *in)
{
	int ret = 0;
	struct audio_device *adev = in->dev;
	unsigned int flags = PCM_IN | PCM_MONOTONIC;

	ALOGV("%s: (%d:%d)", __func__, in->flags, in->device);

	in->pcm_device_id = 0;

	adev->active_input = in;

	adev->input_source = in->source;
	adev->in_device = in->device;
	select_devices(adev);

	ALOGV("%s config: channel(%d), rate(%d), period_size(%d), period_count(%d), format(%d), ",
	      __func__, in->config.channels, in->config.rate,
	      in->config.period_size, in->config.period_count, in->config.format);

	while (1) {
		in->pcm = pcm_open(adev->snd_card, in->pcm_device_id,
				   flags, &in->config);
		if (in->pcm == NULL || !pcm_is_ready(in->pcm)) {
			ALOGE("%s: %s", __func__, pcm_get_error(in->pcm));
			if (in->pcm != NULL) {
				pcm_close(in->pcm);
				in->pcm = NULL;
			}
			ret = -EIO;
			goto error_open;
		}
		break;
	}
	ret = pcm_prepare(in->pcm);
	if (ret < 0) {
		ALOGE("%s: pcm_prepare returned %d", __func__, ret);
		pcm_close(in->pcm);
		in->pcm = NULL;
		goto error_open;
	}
	ALOGV("%s: exit", __func__);
	return ret;

error_open:
	stop_input_stream(in);

error_config:
	adev->active_input = NULL;
	ALOGW("%s: exit: status(%d)", __func__, ret);

	return ret;
}

int stop_output_stream(struct stream_out *out)
{
	int ret = 0;
	struct audio_device *adev = out->dev;

	ALOGV("%s: (%d:%d)", __func__, out->flags, out->devices);

	output_streaming = false;

	if (adev->out_device)
		adev->out_device = AUDIO_DEVICE_NONE;
		select_devices(adev);

	if (hfp_enable)
		start_bt_sco();

	return ret;
}

int start_output_stream(struct stream_out *out)
{
	int ret = 0;
	struct audio_device *adev = out->dev;
	unsigned int flags = PCM_OUT;
	flags |= PCM_MONOTONIC;

	ALOGV("%s: (%d:%d)", __func__, out->flags, out->devices);

	 if (out->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
		out->pcm_device_id = out->spdif_id;
	} else
		out->pcm_device_id = 0;

	adev->out_device |= out->devices;
	select_devices(adev);

	ALOGV("%s config: channel(%d), rate(%d), period_size(%d), period_count(%d), format(%d), ",
	      __func__, out->config.channels, out->config.rate,
	      out->config.period_size, out->config.period_count, out->config.format);

	if (out->pcm != NULL) {
		pcm_close(out->pcm);
		out->pcm = NULL;
	}

	while (1) {
		out->pcm = pcm_open(adev->snd_card, out->pcm_device_id,
				    flags, &out->config);
		if (out->pcm == NULL || !pcm_is_ready(out->pcm)) {
			ALOGE("%s: %s", __func__, pcm_get_error(out->pcm));
			if (out->pcm != NULL) {
				pcm_close(out->pcm);
				out->pcm = NULL;
			}
			ret = -EIO;
			goto error_open;
		}
		break;
	}
	if (pcm_is_ready(out->pcm)) {
		ret = pcm_prepare(out->pcm);
		if (ret < 0) {
			ALOGE("%s: pcm_prepare returned %d", __func__, ret);
			pcm_close(out->pcm);
			out->pcm = NULL;
			goto error_open;
		}
	}

	output_streaming = true;

	ALOGV("%s: exit", __func__);
	return ret;
error_open:
	stop_output_stream(out);
error_config:
	return ret;
}

void lock_input_stream(struct stream_in *in)
{
	pthread_mutex_lock(&in->pre_lock);
	pthread_mutex_lock(&in->lock);
	pthread_mutex_unlock(&in->pre_lock);
}

void lock_output_stream(struct stream_out *out)
{
	pthread_mutex_lock(&out->pre_lock);
	pthread_mutex_lock(&out->lock);
	pthread_mutex_unlock(&out->pre_lock);
}

static int check_input_parameters(uint32_t sample_rate,
				  audio_format_t format,
				  int channel_count)
{
	if ((format != AUDIO_FORMAT_PCM_16_BIT) && (format != AUDIO_FORMAT_PCM_8_24_BIT)) {
		ALOGE("%s: unsupported AUDIO FORMAT (%d) ", __func__, format);
		return -EINVAL;
	}

	if ((channel_count < MIN_CHANNEL_COUNT) || (channel_count > MAX_CHANNEL_COUNT)) {
		ALOGE("%s: unsupported channel count (%d) ", __func__, channel_count);
		return -EINVAL;
	}

	switch (sample_rate) {
	case 8000:
	case 11025:
	case 12000:
	case 16000:
	case 22050:
	case 32000:
	case 44100:
	case 48000:
		break;
	default:
		ALOGE("%s: unsupported (%d) samplerate passed ", __func__, sample_rate);
		return -EINVAL;
	}

	return 0;
}

static size_t get_input_buffer_size(uint32_t sample_rate,
				    audio_format_t format __unused,
				    int channel_count,
				    bool is_low_latency)
{
	size_t size = 0;

	if (check_input_parameters(sample_rate, format, channel_count) != 0)
		return 0;

	size = (sample_rate * AUDIO_CAPTURE_PERIOD_DURATION_MSEC) / 1000;
	if (is_low_latency)
		size = configured_low_latency_capture_period_size;

	size *= channel_count * audio_bytes_per_sample(format);

	/* make sure the size is multiple of 32 bytes
	 *      * At 48 kHz mono 16-bit PCM:
	 *  5.000 ms = 240 frames = 15*16*1*2 = 480, a whole multiple of 32 (15)
	 *  3.333 ms = 160 frames = 10*16*1*2 = 320, a whole multiple of 32 (10)
         */
	size += 0x1f;
	size &= ~0x1f;

	return size;
}

/* audio_stream common out api*/

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
	struct stream_out *out = (struct stream_out *)stream;

	return out->sample_rate;
}

static int out_set_sample_rate(struct audio_stream *stream __unused,
			       uint32_t rate __unused)
{
	return -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
	struct stream_out *out = (struct stream_out *)stream;

	return out->config.period_size *
		audio_stream_out_frame_size((const struct audio_stream_out *)
					    stream);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
	struct stream_out *out = (struct stream_out *)stream;

	return out->channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
	struct stream_out *out = (struct stream_out *)stream;

	return out->format;
}

static int out_set_format(struct audio_stream *stream __unused,
			  audio_format_t format __unused)
{
	return -ENOSYS;
}

static int out_standby(struct audio_stream *stream)
{
	struct stream_out *out = (struct stream_out *)stream;
	struct audio_device *adev = out->dev;

	ALOGV("%s: enter", __func__);
	lock_output_stream(out);
	if (!out->standby) {
		pthread_mutex_lock(&adev->lock);
		out->standby = true;
		if (out->pcm) {
			pcm_close(out->pcm);
			out->pcm = NULL;
		}
		stop_output_stream(out);
		pthread_mutex_unlock(&adev->lock);
	}
	pthread_mutex_unlock(&out->lock);
	ALOGV("%s: exit", __func__);
	return 0;
}

static int out_dump(const struct audio_stream *stream __unused, int fd __unused)
{
	ALOGV("%s", __func__);

	return 0;
}

static audio_devices_t out_get_device(const struct audio_stream *stream)
{
	struct stream_out *out = (struct stream_out *)stream;

	return out->devices;
}


static int out_set_device(struct audio_stream *stream __unused,
			  audio_devices_t device __unused)
{
	return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char
			      *kv_pairs)
{
	struct stream_out *out = (struct stream_out *)stream;
	struct audio_device *adev = out->dev;
	struct str_parms *parms;
	char value[32];
	int ret, val = 0;
	int status = 0;

	ALOGV("%s: enter: kv_pairs: %s", __func__, kv_pairs);

	parms = str_parms_create_str(kv_pairs);
	ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value,
				sizeof(value));
	if (ret >= 0) {
		val = atoi(value);
		lock_output_stream(out);
		pthread_mutex_lock(&adev->lock);
		if (out->devices == AUDIO_DEVICE_OUT_AUX_DIGITAL &&
		    val == AUDIO_DEVICE_NONE) {
			val = AUDIO_DEVICE_OUT_SPEAKER;
		}

		audio_devices_t new_dev = val;
		if (new_dev != AUDIO_DEVICE_NONE) {
			out->devices = new_dev;

			if (!out->standby) {
				adev->out_device = out->devices;
				select_devices(adev);
			}
		}

		pthread_mutex_unlock(&adev->lock);
		pthread_mutex_unlock(&out->lock);

	}

	str_parms_destroy(parms);
	ALOGV("%s: exit: code(%d)", __func__, status);
	return status;
}

static char* out_get_parameters(const struct audio_stream *stream, const char
				*keys)
{
	struct stream_out *out = (struct stream_out *)stream;
	struct str_parms *query = str_parms_create_str(keys);
	char *str;
	char value[256];
	struct str_parms *reply = str_parms_create();
	size_t i, j;
	int ret;
	bool first = true;

	ALOGV("%s: enter: keys - %s", __func__, keys);
	ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value,
				sizeof(value));
	if (ret >= 0) {
		value[0] = '\0';
		i = 0;
		while (out->supported_channel_masks[i] != 0) {
			for (j = 0; j <
			     ARRAY_SIZE(out_channels_name_to_enum_table); j++) {
				if (out_channels_name_to_enum_table[j].value ==
				    out->supported_channel_masks[i]) {
					if (!first) {
						strcat(value, "|");
					}
					strcat(value,
					       out_channels_name_to_enum_table[j].name);
					first = false;
					break;
				}
			}
			i++;
		}
		str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value);
		str = str_parms_to_str(reply);
	} else {
		str = strdup(keys);
	}
	str_parms_destroy(query);
	str_parms_destroy(reply);
	ALOGV("%s: exit: returns - %s", __func__, str);
	return str;
}

static int out_add_audio_effect(const struct audio_stream *stream __unused,
				effect_handle_t effect __unused)
{
	return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream __unused,
				effect_handle_t effect __unused)
{
	return 0;
}

/* audio_stream_out api*/

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
	struct stream_out *out = (struct stream_out *)stream;

	return (out->config.period_count * out->config.period_size * 1000) /
		(out->config.rate);
}

static int out_set_volume(struct audio_stream_out *stream,
			  float left, float right __unused)
{
	struct stream_out *out = (struct stream_out *)stream;

	if (out->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
		/* only take left channel into account: the API is for stereo anyway */
		out->muted = (left == 0.0f);
		return 0;
	}
	return -ENOSYS;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
			 size_t bytes)
{
	struct stream_out *out = (struct stream_out *)stream;
	struct audio_device *adev = out->dev;
	ssize_t ret = 0;

	if (hfp_enable) {
		ALOGV("%s: Skip output streaming for operating hfp sco", __func__);
		return bytes;
	}

	lock_output_stream(out);
	if (out->standby) {
		out->standby = false;
		pthread_mutex_lock(&adev->lock);
		ret = start_output_stream(out);
		pthread_mutex_unlock(&adev->lock);
		if (ret != 0) {
			out->standby = true;
			goto exit;
		}

	}

	if (out->pcm) {
		if (out->muted)
			memset((void *)buffer, 0, bytes);

		ALOGVV("%s: writing buffer (%d bytes) to pcm device", __func__,
		       bytes);

		ret = pcm_write(out->pcm, (void *)buffer, bytes);
	} else {
		LOG_ALWAYS_FATAL("out->pcm is NULL after starting output stream");
	}

exit:
	// For PCM we always consume the buffer and return #bytes regardless of ret.
	out->written += bytes / (out->config.channels * sizeof(short));

	long long sleeptime_us = 0;
	if (ret != 0) {
		ALOGE_IF(out->pcm != NULL,
			 "%s: error %zd - %s", __func__, ret,
			 pcm_get_error(out->pcm));
		sleeptime_us = usleep(bytes * 1000000LL /
		       audio_stream_out_frame_size(&out->stream) /
		       out_get_sample_rate(&out->stream.common));
		// usleep not guaranteed for values over 1 second but we don't limit here.
	}

	pthread_mutex_unlock(&out->lock);

	if (ret != 0) {
		out_standby(&stream->common);
		if (sleeptime_us != 0)
		usleep(sleeptime_us);
	}
	return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream __unused,
				   uint32_t *dsp_frames __unused)
{
	return -EINVAL;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream __unused,
					int64_t *timestamp __unused)
{
	return -EINVAL;
}

static int out_set_callback(struct audio_stream_out *stream __unused,
			    stream_callback_t callback __unused,
			    void *cookie __unused)
{
	return 0;
}

static int out_pause(struct audio_stream_out* stream __unused)
{
	return -ENOSYS;
}

static int out_resume(struct audio_stream_out* stream __unused)
{
	return -ENOSYS;
}

static int out_drain(struct audio_stream_out* stream __unused,
		     audio_drain_type_t type __unused)
{
	return -ENOSYS;
}

static int out_flush(struct audio_stream_out* stream __unused)
{
	return -ENOSYS;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
					 uint64_t *frames, struct timespec
					 *timestamp)
{
	struct stream_out *out = (struct stream_out *)stream;
	int ret = -EINVAL;

	lock_output_stream(out);

	if (out->pcm) {
		unsigned int avail;
		if (pcm_get_htimestamp(out->pcm, &avail, timestamp) == 0) {
			size_t kernel_buffer_size = out->config.period_size *
				out->config.period_count;
			// This adjustment accounts for buffering after app processor.
			int64_t signed_frames = out->written -
				kernel_buffer_size + avail;
			// It would be unusual for this value to be negative, but check just in case ...
			if (signed_frames >= 0) {
				*frames = signed_frames;
				ret = 0;
			}
		}
	}

	pthread_mutex_unlock(&out->lock);

	return ret;
}

static int out_start(const struct audio_stream_out* stream __unused)
{

	return -ENOSYS;
}

static int out_stop(const struct audio_stream_out* stream __unused)
{
	return -ENOSYS;
}

/* audio_stream common in api*/

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
	struct stream_in *in = (struct stream_in *)stream;

	return in->config.rate;
}

static int in_set_sample_rate(struct audio_stream *stream __unused,
			      uint32_t rate __unused)
{
	return -ENOSYS;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
	struct stream_in *in = (struct stream_in *)stream;

	return in->config.period_size *
		audio_stream_in_frame_size((const struct audio_stream_in *)
					    stream);
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
	struct stream_in *in = (struct stream_in *)stream;

	return in->channel_mask;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
	struct stream_in *in = (struct stream_in *)stream;

	return in->format;
}

static int in_set_format(struct audio_stream *stream __unused,
			 audio_format_t format __unused)
{
	return -ENOSYS;
}

static int in_standby(struct audio_stream *stream)
{
	struct stream_in *in = (struct stream_in *)stream;
	struct audio_device *adev = in->dev;
	int status = 0;

	ALOGV("%s: enter", __func__);

	lock_input_stream(in);

	if (!in->standby) {
		pthread_mutex_lock(&adev->lock);
		in->standby = true;
		if (in->pcm) {
			pcm_close(in->pcm);
			in->pcm = NULL;
		}
		status = stop_input_stream(in);
		pthread_mutex_unlock(&adev->lock);
	}
	pthread_mutex_unlock(&in->lock);
	ALOGV("%s: exit: status(%d)", __func__, status);
	return status;
}

static int in_dump(const struct audio_stream *stream __unused, int fd __unused)
{
	ALOGV("%s", __func__);

	return 0;
}

static audio_devices_t in_get_device(const struct audio_stream *stream)
{
	struct stream_in *in = (struct stream_in *)stream;

	return in->device;
}


static int in_set_device(struct audio_stream *stream __unused,
			 audio_devices_t device __unused)
{
	return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char
			      *kv_pairs)
{
	struct stream_in *in = (struct stream_in *)stream;
	struct audio_device *adev = in->dev;
	struct str_parms *parms;
	char value[32];
	int ret, val = 0;
	int status = 0;

	ALOGV("%s: enter: kv_pairs: %s", __func__, kv_pairs);
	parms = str_parms_create_str(kv_pairs);

	ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, value,
				sizeof(value));

	lock_input_stream(in);

	pthread_mutex_lock(&adev->lock);
	if (ret >= 0) {
		val = atoi(value);
		/* no audio source uses val == 0 */
		if (((int)in->source != val) && (val != 0)) {
			in->source = val;
		}
	}

	ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value,
				sizeof(value));

	if (ret >= 0) {
		val = atoi(value);
		if (((int)in->device != val) && (val != 0)) {
			in->device = val;
			/* If recording is in progress, change the tx device to new device */
			if (!in->standby) {
				ALOGV("update input routing change");
				adev->input_source = in->source;
				adev->in_device = in->device;
				select_devices(adev);
			}
		}
	}

	pthread_mutex_unlock(&adev->lock);
	pthread_mutex_unlock(&in->lock);

	str_parms_destroy(parms);
	ALOGV("%s: exit: code(%d)", __func__, status);
	return status;
}

static char* in_get_parameters(const struct audio_stream *stream __unused,
			       const char *keys __unused)
{
	return strdup("");
}

static int in_add_audio_effect(const struct audio_stream *stream,
				effect_handle_t effect)
{
	struct stream_in *in = (struct stream_in *)stream;
	effect_descriptor_t descr;

	ALOGV("%s: effect %p", __func__, effect);

	if ((*effect)->get_descriptor(effect, &descr) == 0) {
		lock_input_stream(in);
		pthread_mutex_lock(&in->dev->lock);

		pthread_mutex_unlock(&in->dev->lock);
		pthread_mutex_unlock(&in->lock);
	}

	return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream __unused,
				effect_handle_t effect __unused)
{
	struct stream_in *in = (struct stream_in *)stream;
	effect_descriptor_t descr;

	ALOGV("%s: effect %p", __func__, effect);

	if ((*effect)->get_descriptor(effect, &descr) == 0) {
		lock_input_stream(in);
		pthread_mutex_lock(&in->dev->lock);

		pthread_mutex_unlock(&in->dev->lock);
		pthread_mutex_unlock(&in->lock);
	}

	return 0;
}

/* audio_stream_in api*/

static int in_set_gain(struct audio_stream_in *stream __unused, float gain __unused)
{
	return 0;
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
		       size_t bytes)
{
	struct stream_in *in = (struct stream_in *)stream;
	struct audio_device *adev = in->dev;
	int i, ret = -1;

	lock_input_stream(in);

	if (in->standby) {
		pthread_mutex_lock(&adev->lock);
		ret = start_input_stream(in);
		pthread_mutex_unlock(&adev->lock);
		if (ret != 0) {
			goto exit;
		}
		in->standby = false;
	}

	if (in->pcm) {
		ret = pcm_read(in->pcm, buffer, bytes);
	}

	if (ret < 0) {
		ALOGE("Failed to read w/err %s", strerror(errno));
		ret = -errno;
	}

	if (ret == 0 && adev->mic_muted)
		memset(buffer, 0, bytes);

exit:
	pthread_mutex_unlock(&in->lock);

	if (ret != 0) {
		in_standby(&in->stream.common);
		ALOGV("%s: read failed - sleeping for buffer duration",
		      __func__);
		usleep(bytes * 1000000 / audio_stream_in_frame_size(stream) /
		       in_get_sample_rate(&in->stream.common));
		memset(buffer, 0, bytes);
	}
	if (bytes > 0) {
		in->frames_read += bytes / audio_stream_in_frame_size(stream);
	}
	return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream __unused)
{
	return 0;
}

static int in_get_capture_position(const struct audio_stream_in *stream,
				   int64_t *frames, int64_t *time)
{
	if (stream == NULL || frames == NULL || time == NULL) {
		return -EINVAL;
	}
	struct stream_in *in = (struct stream_in *)stream;
	int ret = -ENOSYS;

	lock_input_stream(in);
	if (in->pcm) {
		struct timespec timestamp;
		unsigned int avail;
		if (pcm_get_htimestamp(in->pcm, &avail, &timestamp) == 0) {
			*frames = in->frames_read + avail;
			*time = timestamp.tv_sec * 1000000000LL +
				timestamp.tv_nsec;
			ret = 0;
		}
	}
	pthread_mutex_unlock(&in->lock);
	return ret;
}

static int in_start(const struct audio_stream_in* stream __unused)
{
	return -ENOSYS;
}

static int in_stop(const struct audio_stream_in* stream __unused)
{
	return -ENOSYS;
}

/* hw_device_t api*/

static int adev_close(struct hw_device_t* device)
{
	size_t i;
	struct audio_device *adev = (struct audio_device *)device;

	if (!adev)
		return 0;

	pthread_mutex_lock(&adev_init_lock);

	if ((--audio_device_ref_count) == 0) {
		audio_route_free(adev->audio_route);
#ifdef USES_NXVOICE
		if (adev->use_nxvoice) {
			nx_voice_stop(adev->nxvoice_handle);
			nx_voice_close_handle(adev->nxvoice_handle);
			adev->nxvoice_handle = NULL;
		}
#endif
		free(device);
	}

	pthread_mutex_unlock(&adev_init_lock);

	return 0;
}

static int adev_init_check(const struct audio_hw_device *dev __unused)
{
	return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
	struct audio_device *adev = (struct audio_device *)dev;

	adev->voice_volume = volume;
	return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev __unused,
				  float volume __unused)
{
	return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *dev __unused,
				  float *volume __unused)
{
	return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev __unused,
			 audio_mode_t mode __unused)
{
	return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
	struct audio_device *adev = (struct audio_device *)dev;

	pthread_mutex_lock(&adev->lock);
	adev->voice_mic_mute = state;
	adev->mic_muted = state;
	pthread_mutex_unlock(&adev->lock);
	return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
	struct audio_device *adev = (struct audio_device *)dev;

	*state = adev->voice_mic_mute;
	return 0;
}

static int adev_set_parameters(struct audio_hw_device *dev,
			       const char *kv_pairs)
{
	struct audio_device *adev = (struct audio_device *)dev;
	struct str_parms *parms;
	char *str;
	char value[32];
	int val;
	int ret;
	int status = 0;

	ALOGV("%s: enter: kv_pairs: %s", __func__, kv_pairs);

	pthread_mutex_lock(&adev->lock);

	parms = str_parms_create_str(kv_pairs);
	if (status != 0) {
		goto done;
	}

	ret = str_parms_get_str(parms, "hfp_set_sampling_rate", value, sizeof(value));
	if (ret >= 0)
		pcm_config_bt_sco.rate = atoi(value);

	ret = str_parms_get_str(parms, "hfp_enable", value, sizeof(value));
	if (ret >= 0) {
		if (strcmp(value, "true") == 0) {
			hfp_enable = true;
			if (!output_streaming)
				start_bt_sco(adev->snd_card);
		} else {
			hfp_enable = false;
			stop_bt_sco();
		}
	}

done:
	str_parms_destroy(parms);
	pthread_mutex_unlock(&adev->lock);
	ALOGV("%s: exit with code(%d)", __func__, status);
	return status;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
				  const char *keys)
{
	struct audio_device *adev = (struct audio_device *)dev;
	struct str_parms *reply = str_parms_create();
	struct str_parms *query = str_parms_create_str(keys);
	char *str;

	pthread_mutex_lock(&adev->lock);

	str = str_parms_to_str(reply);
	str_parms_destroy(query);
	str_parms_destroy(reply);

	pthread_mutex_unlock(&adev->lock);
	ALOGV("%s: exit: returns - %s", __func__, str);

	return strdup("");
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev __unused,
					 const struct audio_config *config)
{
	int channel_count =
		audio_channel_count_from_in_mask(config->channel_mask);

	return get_input_buffer_size(config->sample_rate, config->format, channel_count,
	     false /* is_low_latency: since we don't know, be conservative */);
}

static int adev_open_output_stream(struct audio_hw_device *dev,
				      audio_io_handle_t handle,
				      audio_devices_t devices,
				      audio_output_flags_t flags,
				      struct audio_config *config,
				      struct audio_stream_out **stream_out,
				      const char *address __unused)
{
	struct audio_device *adev = (struct audio_device *)dev;
	struct stream_out *out;
	int ret;
	*stream_out = NULL;
	out = (struct stream_out *)calloc(1, sizeof(struct stream_out));

	if (devices == AUDIO_DEVICE_NONE)
		devices = AUDIO_DEVICE_OUT_SPEAKER;

	out->flags = flags;
	out->devices = devices;
	out->dev = adev;
	out->format = config->format;
	out->sample_rate = config->sample_rate;
	out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
	out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_STEREO;
	out->handle = handle;

	ALOGV("%s: (%d:%d)", __func__, out->flags, out->devices);


	if (out->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
		int i, fd;
		char s[100], p[5];
		char c[] = "spdif";

		// check spdif device format. -> edited by hsjung
		for (i = 0; i < 10; i++) {
			sprintf(s, "/sys/devices/platform/sound/of_node/simple-audio-card,dai-link@%d/format", i);
			fd = open(s, O_RDONLY);
			if (fd < 0) {
				ALOGI("No more devices\n");
				break;
			}
			read(fd, p, 5);
			if (strncmp((const char *)&p, (const char *)&c, 5) == 0) {
				ALOGI("found!! device format is %s %d\n", p, i);
				out->spdif_id = i;
				break;
			} else {
				ALOGI("device format is %s %d\n", p, i);
			}
		}
		out->config = pcm_config_hdmi;
	} else if (out->flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
		out->config = pcm_config_deep_buffer;
	} else if (out->flags & AUDIO_OUTPUT_FLAG_TTS) {

	} else if (out->flags & AUDIO_OUTPUT_FLAG_RAW) {

	} else {
		out->config = pcm_config_low_latency;
	}

	if (config->format != audio_format_from_pcm_format(out->config.format)) {
		out->format = audio_format_from_pcm_format(out->config.format);
	}
	out->sample_rate = out->config.rate;

	if (flags & AUDIO_OUTPUT_FLAG_PRIMARY) {
		if (adev->primary_output == NULL)
			adev->primary_output = out;
		else {
			ALOGE("%s: Primary output is already opended",
			      __func__);
			ret = -EEXIST;
			goto error_open;
		}
	}

	out->stream.common.get_sample_rate = out_get_sample_rate;
	out->stream.common.set_sample_rate = out_set_sample_rate;
	out->stream.common.get_buffer_size = out_get_buffer_size;
	out->stream.common.get_channels = out_get_channels;
	out->stream.common.get_format = out_get_format;
	out->stream.common.set_format = out_set_format;
	out->stream.common.standby = out_standby;
	out->stream.common.dump = out_dump;
	out->stream.common.set_parameters = out_set_parameters;
	out->stream.common.get_parameters = out_get_parameters;
	out->stream.common.add_audio_effect = out_add_audio_effect;
	out->stream.common.remove_audio_effect = out_remove_audio_effect;
	out->stream.get_latency = out_get_latency;
	out->stream.set_volume = out_set_volume;
	out->stream.write = out_write;

	out->stream.get_render_position = out_get_render_position;
	out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
	out->stream.get_presentation_position = out_get_presentation_position;

	out->standby = 1;
	/* out->muted = false; by calloc() */
	/* out->written = 0; by calloc() */

	pthread_mutex_init(&out->lock, (const pthread_mutexattr_t *) NULL);
	pthread_mutex_init(&out->pre_lock, (const pthread_mutexattr_t *) NULL);

	config->format = out->stream.common.get_format(&out->stream.common);
	config->channel_mask = out->stream.common.get_channels(&out->stream.common);
	config->sample_rate = out->stream.common.get_sample_rate(&out->stream.common);

	/*
	 * By locking output stream before registering, we allow the callback
	 * to update stream's state only after stream's initial state is set to
	 * adev state.
	 */
	lock_output_stream(out);
	pthread_mutex_unlock(&out->lock);

	*stream_out = &out->stream;
	ALOGV("%s: exit", __func__);
	return 0;

error_open:
	free(out);
	*stream_out = NULL;
	ALOGW("%s: exit: ret %d", __func__, ret);
	return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev __unused,
				   struct audio_stream_out *stream_out)
{
	struct stream_out *out = (struct stream_out *)stream_out;

	ALOGV("%s: enter", __func__);
	out_standby(&stream_out->common);
	pthread_mutex_destroy(&out->lock);
	free(stream_out);
	ALOGV("%s: exit", __func__);
}

static int adev_open_input_stream(struct audio_hw_device *dev,
				      audio_io_handle_t handle,
				      audio_devices_t devices,
				      struct audio_config *config,
				      struct audio_stream_in **stream_in,
				      audio_input_flags_t flags,
				      const char *address __unused,
				      audio_source_t source)
{
	struct audio_device *adev = (struct audio_device *)dev;
	struct stream_in *in;
	int ret = 0, buffer_size, frame_size;
	int channel_count =
		audio_channel_count_from_in_mask(config->channel_mask);
	bool is_low_latency = false;

	ALOGV("%s: enter", __func__);
	*stream_in = NULL;
	if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
		return -EINVAL;

	in = (struct stream_in *)calloc(1, sizeof(struct stream_in));

	pthread_mutex_init(&in->lock, (const pthread_mutexattr_t *) NULL);
	pthread_mutex_init(&in->pre_lock, (const pthread_mutexattr_t *) NULL);

	in->stream.common.get_sample_rate = in_get_sample_rate;
	in->stream.common.set_sample_rate = in_set_sample_rate;
	in->stream.common.get_buffer_size = in_get_buffer_size;
	in->stream.common.get_channels = in_get_channels;
	in->stream.common.get_format = in_get_format;
	in->stream.common.set_format = in_set_format;
	in->stream.common.standby = in_standby;
	in->stream.common.dump = in_dump;
	in->stream.common.set_parameters = in_set_parameters;
	in->stream.common.get_parameters = in_get_parameters;
	in->stream.common.add_audio_effect = in_add_audio_effect;
	in->stream.common.remove_audio_effect = in_remove_audio_effect;
	in->stream.set_gain = in_set_gain;
	in->stream.read = in_read;
	in->stream.get_input_frames_lost = in_get_input_frames_lost;
	in->stream.get_capture_position = in_get_capture_position;

	in->source = source;
	in->device = devices;
	in->dev = adev;
	in->standby = 1;
	in->channel_mask = config->channel_mask;
	in->capture_handle = handle;
	in->flags = flags;

	config->format = AUDIO_FORMAT_PCM_16_BIT;

	in->format = config->format;

        if (config->sample_rate == LOW_LATENCY_CAPTURE_SAMPLE_RATE &&
	    (flags & AUDIO_INPUT_FLAG_FAST) != 0) {
		is_low_latency = true;
	}

	in->config = pcm_config_audio_capture;

	frame_size = audio_stream_in_frame_size(&in->stream);
	buffer_size = get_input_buffer_size(config->sample_rate,
					    config->format,
					    channel_count,
					    is_low_latency);
	in->config.period_size = buffer_size / frame_size;

	in->config.channels = channel_count;
	in->config.rate = config->sample_rate;

	lock_input_stream(in);
	pthread_mutex_unlock(&in->lock);

	*stream_in = &in->stream;
	ALOGV("%s: exit", __func__);
	return 0;

err_open:
	free(in);
	*stream_in = NULL;
	return ret;

}

static void adev_close_input_stream(struct audio_hw_device *dev __unused,
				   struct audio_stream_in *stream_in)
{
	struct stream_in *in = (struct stream_in *)stream_in;

	ALOGV("%s: enter", __func__);
	in_standby(&stream_in->common);
	pthread_mutex_destroy(&in->lock);
	free(stream_in);
	ALOGV("%s: enter", __func__);
	return;

}

#ifdef USES_NXVOICE

void nxvoice_lock_input_stream(struct nxvoice_stream_in *in)
{
	pthread_mutex_lock(&in->pre_lock);
	pthread_mutex_lock(&in->lock);
	pthread_mutex_unlock(&in->pre_lock);
}

/**
 * nxvoice input stream callback implementation */
static uint32_t nxvoice_in_get_sample_rate(const struct audio_stream *stream)
{
	struct nxvoice_stream_in *in = (struct nxvoice_stream_in *)stream;

	ALOGVV("%s: sample_rate=%d\n", __func__, in->sample_rate);
	return in->sample_rate;
}

static int nxvoice_in_set_sample_rate(struct audio_stream *stream __unused,
				      uint32_t rate __unused)
{
	ALOGVV("%s (rate=%d)\n", __FUNCTION__, rate);
	return 0;
}

static size_t
nxvoice_in_get_buffer_size(const struct audio_stream *stream __unused)
{
	size_t buffer_size = NXAUDIO_INPUT_BUFFER_SIZE;

	ALOGVV("%s (buffer_size=%d)\n", __FUNCTION__, buffer_size);
	return buffer_size;
}

static audio_channel_mask_t
nxvoice_in_get_channels(const struct audio_stream *stream)
{
	struct nxvoice_stream_in *in = (struct nxvoice_stream_in *)stream;
	audio_channel_mask_t channel_mask = in->channel_mask;

	ALOGVV("%s mask=0x%x\n", __FUNCTION__, channel_mask);
	return channel_mask;
}

static audio_format_t nxvoice_in_get_format(const struct audio_stream *stream)
{
	struct nxvoice_stream_in *in = (struct nxvoice_stream_in *)stream;

	ALOGVV("%s (format=0x%x)\n", __FUNCTION__, in->format);
	return in->format;
}

static int nxvoice_in_set_format(struct audio_stream *stream __unused,
				 audio_format_t format __unused)
{
	ALOGVV("%s (format=0x%x)\n", __FUNCTION__, format);
	return -ENOSYS;
}

static int nxvoice_in_standby(struct audio_stream *stream)
{
	struct nxvoice_stream_in *in = (struct nxvoice_stream_in *)stream;
	struct audio_device *adev = in->dev;

	ALOGV("%s Enter", __func__);

	nxvoice_lock_input_stream(in);

	if (!in->standby) {
		pthread_mutex_lock(&adev->lock);
		in->standby = true;
		adev->input_source = AUDIO_SOURCE_DEFAULT;
		adev->in_device = AUDIO_DEVICE_NONE;
		select_devices(adev);
		pthread_mutex_unlock(&adev->lock);
	}
	pthread_mutex_unlock(&in->lock);

	ALOGV("%s Exit", __func__);
	return 0;
}

static int
nxvoice_in_set_parameters(struct audio_stream *stream, const char *kv_pairs)
{
	struct nxvoice_stream_in *in = (struct nxvoice_stream_in *)stream;
	struct audio_device *adev = in->dev;
	struct str_parms *parms;
	char value[32];
	int ret, val = 0;
	int status = 0;

	ALOGV("%s: enter: kv_pairs: %s", __func__, kv_pairs);
	parms = str_parms_create_str(kv_pairs);

	ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, value,
				sizeof(value));

	nxvoice_lock_input_stream(in);

	pthread_mutex_lock(&adev->lock);
	if (ret >= 0) {
		val = atoi(value);
		/* no audio source uses val == 0 */
		if (((int)in->source != val) && (val != 0)) {
			in->source = val;
		}
	}

	ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value,
				sizeof(value));

	if (ret >= 0) {
		val = atoi(value);
		if (((int)in->device != val) && (val != 0)) {
			in->device = val;
			/* If recording is in progress, change the tx device to new device */
			if (!in->standby) {
				ALOGV("update input routing change");
				adev->input_source = in->source;
				adev->in_device = in->device;
				select_devices(adev);
			}
		}
	}

	pthread_mutex_unlock(&adev->lock);
	pthread_mutex_unlock(&in->lock);

	str_parms_destroy(parms);
	ALOGV("%s: exit: code(%d)", __func__, status);
	return status;
}

static int
nxvoice_in_add_audio_effect(const struct audio_stream *stream __unused,
			    effect_handle_t effect __unused)
{
	ALOGV("%s\n", __FUNCTION__);
	return 0;
}

static int
nxvoice_in_remove_audio_effect(const struct audio_stream *stream __unused,
			       effect_handle_t effect __unused)
{
	ALOGV("%s\n", __FUNCTION__);
	return 0;
}

static ssize_t nxvoice_in_read(struct audio_stream_in *stream_in, void *buffer,
							   size_t bytes)
{
	int ret = 0;
	struct nxvoice_stream_in *in = (struct nxvoice_stream_in *)stream_in;
	struct audio_device *adev = in->dev;
	ssize_t frames = bytes / audio_stream_in_frame_size(&in->stream);

	ALOGVV("%s: bytes %d, frames %d", __func__, bytes, frames);

	nxvoice_lock_input_stream(in);

	if (in->standby) {
		pthread_mutex_lock(&adev->lock);
		adev->input_source = in->source;
		adev->in_device = in->device;
		select_devices(adev);
		pthread_mutex_unlock(&adev->lock);
		in->standby = false;
	}

	ret = nx_voice_get_data(adev->nxvoice_handle, (short *)buffer, frames);
	if ((size_t)ret != bytes) {
		nxvoice_in_standby(&in->stream.common);
		ALOGE("%s: failed to nx_voice_get_data(ret %d, bytes %d)",
			  __func__, ret, bytes);
	}

	pthread_mutex_unlock(&in->lock);

	ALOGVV("%s: return %d", __func__, bytes);
	return bytes;
}

static int
adev_nxvoice_open_input_stream(struct audio_hw_device *dev,
			       audio_io_handle_t handle,
			       audio_devices_t devices,
			       struct audio_config *config,
			       struct audio_stream_in **stream_in,
			       audio_input_flags_t flags __unused,
			       const char *address __unused,
			       audio_source_t source)
{
	struct audio_device *adev = (struct audio_device *)dev;
	struct nxvoice_stream_in *in;
	int ret;

	*stream_in = NULL;

	ALOGV("*** %s (devices=0x%x, request rate=%d, channel_mask=0x%x, frame_count=%d) ***",
		  __func__, devices, config->sample_rate, config->channel_mask,
		  config->frame_count);

	/* Respond with a request for mono if a different format is given. */
	if (config->channel_mask != AUDIO_CHANNEL_IN_MONO &&
		config->channel_mask != AUDIO_CHANNEL_IN_FRONT_BACK) {
		config->channel_mask  = AUDIO_CHANNEL_IN_MONO;
		ALOGE("%s: channel must be mono\n", __func__);
		return -EINVAL;
	}

	if (config->sample_rate != 16000) {
		ALOGE("%s: sample_rate %d is not supported, support only 16000\n",
			  __func__, config->sample_rate);
		config->sample_rate = 16000;
		return -EINVAL;
	}

	if (config->format != AUDIO_FORMAT_PCM_16_BIT) {
		ALOGE("%s: format 0x%x is not supported, support only 16bit pcm\n",
			  __func__, config->format);
		config->format = AUDIO_FORMAT_PCM_16_BIT;
		return -EINVAL;
	}

	in = (struct nxvoice_stream_in *)calloc(1, sizeof(*in));
	if (!in) {
		ALOGE("%s: failed to alloc nxvoice_stream_in", __func__);
		return -ENOMEM;
	}

	pthread_mutex_init(&in->lock, (const pthread_mutexattr_t *) NULL);
	pthread_mutex_init(&in->pre_lock, (const pthread_mutexattr_t *) NULL);

	in->dev = adev;

	in->channel_mask = config->channel_mask;
	in->sample_rate = config->sample_rate;
	in->format = config->format;

	in->stream.common.get_sample_rate = nxvoice_in_get_sample_rate;
	in->stream.common.set_sample_rate = nxvoice_in_set_sample_rate;
	in->stream.common.get_buffer_size = nxvoice_in_get_buffer_size;
	in->stream.common.get_channels = nxvoice_in_get_channels;
	in->stream.common.get_format = nxvoice_in_get_format;
	in->stream.common.set_format = nxvoice_in_set_format;
	in->stream.common.standby = nxvoice_in_standby;
	in->stream.common.dump = in_dump;
	in->stream.common.set_parameters = nxvoice_in_set_parameters;
	in->stream.common.get_parameters = in_get_parameters;
	in->stream.common.add_audio_effect = nxvoice_in_add_audio_effect;
	in->stream.common.remove_audio_effect = nxvoice_in_remove_audio_effect;
	in->stream.set_gain = in_set_gain;
	in->stream.read = nxvoice_in_read;
	in->stream.get_input_frames_lost = in_get_input_frames_lost;
	//in->stream.get_capture_position = in_get_capture_position;

	in->standby = true;

	in->source = source;
	/* strip AUDIO_DEVICE_BIT_IN to allow bitwise comparisons */
	in->device = devices & ~AUDIO_DEVICE_BIT_IN;
	in->io_handle = handle;

	nxvoice_lock_input_stream(in);
	pthread_mutex_unlock(&in->lock);

	*stream_in = &in->stream;

	return 0;
}

static void
adev_nxvoice_close_input_stream(struct audio_hw_device *dev __unused,
				struct audio_stream_in *stream_in)
{
	struct nxvoice_stream_in *in = (struct nxvoice_stream_in *)stream_in;

	ALOGV("%s\n", __func__);

	if (in != NULL) {
		nxvoice_in_standby(&stream_in->common);
		pthread_mutex_destroy(&in->lock);
		free(in);
	}

	return;
}

static size_t
adev_nxvoice_get_input_buffer_size(const struct audio_hw_device *dev __unused,
				   const struct audio_config *config __unused)
{
	size_t buffer_size = NXAUDIO_INPUT_BUFFER_SIZE;

	ALOGV("%s (buffer_size=%d)\n", __func__, buffer_size);
	return buffer_size;
}
#endif

static int adev_dump(const struct audio_hw_device *dev __unused, int fd __unused)
{
	return 0;
}


static int adev_set_master_mute(struct audio_hw_device *dev __unused,
				bool mute __unused)
{
	return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *dev __unused,
				bool *mute __unused)
{
	return -ENOSYS;
}

#ifdef USES_NXVOICE
static bool nx_voice_prop_init(struct nx_smartvoice_config *c)
{
	char buf[PROPERTY_VALUE_MAX];
	int val;

	val = property_get_bool(USE_NXVOICE_PROP_KEY, 0);
	if (val == 0) {
		ALOGI("Do not use NXVoice");
		return false;
	}

	ALOGI("Use NXVoice!!!");

	c->use_feedback = property_get_int32(USE_FEEDBACK_PROP_KEY, 0);
	c->pdm_devnum = property_get_int32(PDM_DEVNUM_PROP_KEY, 2);
	c->pdm_devnum2 = c->pdm_devnum + 1;
	c->ref_devnum = property_get_int32(REF_DEVNUM_PROP_KEY, 1);
	if (c->use_feedback)
		c->feedback_devnum =
			property_get_int32(FEEDBACK_DEVNUM_PROP_KEY, 3);
	c->pdm_chnum = property_get_int32(PDM_CHNUM_PROP_KEY, 4);
	c->pdm_gain = property_get_int32(PDM_GAIN_PROP_KEY, 0);
	c->ref_resample_out_chnum =
		property_get_int32(RESAMPLE_OUT_CHNUM_PROP_KEY, 1);
	c->sample_count = property_get_int32(SAMPLE_COUNT_PROP_KEY, 256);
	c->check_trigger = property_get_bool(CHECK_TRIGGER_PROP_KEY, 0);
	c->trigger_done_ret_value = property_get_int32(CHECK_TRIGGER_PROP_KEY, 1);
	c->pass_after_trigger = property_get_bool(PASS_AFTER_TRIGGER_PROP_KEY, 0);
	c->verbose = property_get_bool(NXVOICE_VERBOSE_PROP_KEY, 0);

	ALOGI("NXVoice Config");
	ALOGI("use_feedback: %d", c->use_feedback);
	ALOGI("pdm_devnum: %d", c->pdm_devnum);
	ALOGI("ref_devnum: %d", c->ref_devnum);
	ALOGI("feedback_devnum: %d", c->feedback_devnum);
	ALOGI("pdm_chnum: %d", c->pdm_chnum);
	ALOGI("pdm_gain: %d", c->pdm_gain);
	ALOGI("resample_out_chnum: %d", c->ref_resample_out_chnum);
	ALOGI("check_trigger: %d", c->check_trigger);
	ALOGI("trigger_done_ret_value: %d", c->trigger_done_ret_value);
	ALOGI("pass_after_trigger: %d", c->pass_after_trigger);
	ALOGI("verbose: %d", c->verbose);

	return true;
}

static void *thread_start_nxvoice(void *arg)
{
	struct audio_device *adev = (struct audio_device *)arg;
	void *handle = adev->nxvoice_handle;
	struct nx_smartvoice_config *c = &adev->nxvoice_config;
	int ret;

	ret = nx_voice_start(handle, c);
	if (ret < 0) {
		ALOGE("%s: failed to nx_voice_start", __func__);
		pthread_exit(NULL);
	}

	ALOGD("nx_voice started\n");

	if (ret == 0) {
		ALOGD("%s: child returned\n", __func__);
	} else {
		ALOGD("%s: parent returned, child pid %d", __func__, ret);
	}

	pthread_exit(NULL);
}
#endif

/* hw_module_methods_t api*/

/* This returns 1 if the input parameter looks at all plausible as a low latency period size,
 * or 0 otherwise.  A return value of 1 doesn't mean the value is guaranteed to work,
 * just that it _might_ work.
 */
static int period_size_is_plausible_for_low_latency(int period_size)
{
    switch (period_size) {
    case 48:
    case 96:
    case 144:
    case 160:
    case 192:
    case 240:
    case 320:
    case 480:
        return 1;
    default:
        return 0;
    }
}

static int adev_open(const struct hw_module_t* module, const char* id,
		     struct hw_device_t** device)
{
	ALOGV("%s: enter", __func__);
	if (strcmp(id, AUDIO_HARDWARE_INTERFACE) != 0)
		return -EINVAL;
	pthread_mutex_lock(&adev_init_lock);
	if (audio_device_ref_count != 0) {
		*device = &adev->device.common;
		audio_device_ref_count++;
		ALOGV("%s: returning existing instance of adev", __func__);
		ALOGV("%s: exit", __func__);
		pthread_mutex_unlock(&adev_init_lock);
		return 0;
	}
	adev = calloc(1, sizeof(struct audio_device));

	pthread_mutex_init(&adev->lock, (const pthread_mutexattr_t *)NULL);

	adev->device.common.tag = HARDWARE_DEVICE_TAG;
	adev->device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
	adev->device.common.module = (struct hw_module_t *)module;
	adev->device.common.close = adev_close;

	adev->device.init_check = adev_init_check;
	adev->device.set_voice_volume = adev_set_voice_volume;
	adev->device.set_master_volume = adev_set_master_volume;
	adev->device.get_master_volume = adev_get_master_volume;
	adev->device.set_mode = adev_set_mode;
	adev->device.set_mic_mute = adev_set_mic_mute;
	adev->device.get_mic_mute = adev_get_mic_mute;
	adev->device.set_parameters = adev_set_parameters;
	adev->device.get_parameters = adev_get_parameters;
	adev->device.get_input_buffer_size = adev_get_input_buffer_size;
	adev->device.open_output_stream = adev_open_output_stream;
	adev->device.close_output_stream = adev_close_output_stream;
	adev->device.open_input_stream = adev_open_input_stream;
	adev->device.close_input_stream = adev_close_input_stream;
	adev->device.dump = adev_dump;
	adev->device.set_master_mute = adev_set_master_mute;
	adev->device.get_master_mute = adev_get_master_mute;

	adev->audio_route = audio_route_init(MIXER_CARD, MIXER_XML_PATH);
	if (!adev->audio_route) {
		ALOGW("%s: mixer file not exist!!", __func__);
#ifdef QUICKBOOT
        while (1) {
            adev->audio_route = audio_route_init(MIXER_CARD, MIXER_XML_PATH);
            if (!adev->audio_route) {
                usleep(100000);
            } else {
                break;
            }
        }
        ALOGD("mixer file exist");
#endif
	}
	/* adev->cur_route_id initial value is 0 and such that first device
	 * selection is always applied by select_devices() */

	pthread_mutex_lock(&adev->lock);
	adev->mode = AUDIO_MODE_NORMAL;
	adev->active_input = NULL;
	adev->primary_output = NULL;
	adev->snd_card = 0;
	pthread_mutex_unlock(&adev->lock);

	*device = &adev->device.common;

	char value[PROPERTY_VALUE_MAX];
	int trial;

	if (property_get("audio_hal.period_size", value, NULL) > 0) {
		trial = atoi(value);
		if (period_size_is_plausible_for_low_latency(trial)) {
			pcm_config_low_latency.period_size = trial;
			pcm_config_low_latency.start_threshold = trial / 4;
			pcm_config_low_latency.avail_min = trial / 4;
			configured_low_latency_capture_period_size = trial;
		}
	}
	if (property_get("audio_hal.in_period_size", value, NULL) > 0) {
		trial = atoi(value);
		if (period_size_is_plausible_for_low_latency(trial)) {
			configured_low_latency_capture_period_size = trial;
		}
	}

#ifdef USES_NXVOICE
	adev->use_nxvoice = nx_voice_prop_init(&adev->nxvoice_config);
	if (adev->use_nxvoice) {
		adev->nxvoice_handle = nx_voice_create_handle();
		if (!adev->nxvoice_handle) {
			ALOGE("%s: failed to nx_voice_create_handle\n",
			      __func__);
			return -ENODEV;
		} else {
			pthread_t tid_nxvoice;

			pthread_create(&tid_nxvoice, NULL, thread_start_nxvoice,
				       (void *)adev);

			/* override input callback */
			adev->device.get_input_buffer_size = adev_nxvoice_get_input_buffer_size;
			adev->device.open_input_stream = adev_nxvoice_open_input_stream;
			adev->device.close_input_stream = adev_nxvoice_close_input_stream;
		}
	}
#endif

	audio_device_ref_count++;

	pthread_mutex_unlock(&adev_init_lock);

	ALOGV("%s: exit", __func__);
	return 0;
}

static struct hw_module_methods_t hal_module_methods = {
	.open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
	.common = {
		.tag = HARDWARE_MODULE_TAG,
		.module_api_version = AUDIO_MODULE_API_VERSION_0_1,
		.hal_api_version = HARDWARE_HAL_API_VERSION,
		.id = AUDIO_HARDWARE_MODULE_ID,
		.name = "Nexell Audio HAL",
		.author = "The Android Open Source Project",
		.methods = &hal_module_methods,
	},
};


