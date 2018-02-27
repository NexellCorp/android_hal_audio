/*****************************************************************************
 **
 **  Description:    Nexell Bluetooth Sco HAL.
 **
 **  Copyright (c) 2017, Nexell Corp., All Rights Reserved.
 **  Proprietary and confidential.
 **
 *****************************************************************************/
#define SET_SCHED_PRIO

#ifdef SET_SCHED_PRIO
#include <sys/prctl.h>
#include <sys/resource.h>
#include <utils/ThreadDefs.h>
#include <cutils/sched_policy.h>
#endif

int thread_exit;

static pthread_t t_voice;
static pthread_t t_sco;

struct pcm *sco_out, *voice_in;
struct pcm *voice_out, *sco_in;

static void* thread_voice()
{
	struct pcm_config config;
	int size;
	char *buffer = NULL;
	int ret = 0;

#ifdef SET_SCHED_PRIO
	prctl(PR_SET_NAME, (unsigned long)"bt_voice_thread", 0, 0, 0);
	set_sched_policy(0, SP_AUDIO_SYS);
	setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_URGENT_AUDIO);
#endif

	config = pcm_config_bt_sco;

	ALOGD("in %s", __func__);

	size = config.period_size * config.period_count;

	sco_out = pcm_open(0, 2, PCM_OUT | PCM_MONOTONIC, &config);
	if (!sco_out || !pcm_is_ready(sco_out)) {
		ALOGE("%s: unable to open sco_out PCM device(%s)",
		      __func__, pcm_get_error(sco_out));
		goto exit;
	}

	voice_in = pcm_open(0, 0, PCM_IN | PCM_MONOTONIC, &config);
	if (!voice_in || !pcm_is_ready(voice_in)) {
		ALOGE("%s: unable to open voice_in PCM device(%s)",
		      __func__, pcm_get_error(voice_in));
		goto exit;
	}

	buffer = (char *)malloc(size);

	pcm_start(sco_out);
	pcm_start(voice_in);

	while(!thread_exit) {
		ret = pcm_read(voice_in, buffer, size);
		if (ret) {
			if (errno != EBADFD)
				ALOGE("%s: failed to voice_in pcm_read(%d : %s)", __func__, errno, pcm_get_error(voice_in));
			continue;
		}
		ret = pcm_write(sco_out, buffer, size);
		if (ret) {
			if (errno != EBADFD)
				ALOGE("%s: failed to sco_out pcm_write(%d : %s)", __func__, errno, pcm_get_error(sco_out));
			continue;
		}
	}

	free(buffer);

	pcm_close(sco_out);
	pcm_close(voice_in);

	ALOGD("out %s", __func__);

exit:
	pthread_exit(&ret);
}

static void* thread_sco()
{
	struct pcm_config config;
	int size;
	char *buffer = NULL;
	int ret = 0;

#ifdef SET_SCHED_PRIO
	prctl(PR_SET_NAME, (unsigned long)"bt_sco_thread", 0, 0, 0);
	set_sched_policy(0, SP_AUDIO_SYS);
	setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_URGENT_AUDIO);
#endif

	config = pcm_config_bt_sco;

	ALOGD("in %s", __func__);

	size = config.period_size * config.period_count;
	voice_out = pcm_open(0, 0, PCM_OUT | PCM_MONOTONIC, &config);
	if (!voice_out || !pcm_is_ready(voice_out)) {
		ALOGE("%s: unable to open voice_out PCM device(%s)",
		      __func__, pcm_get_error(voice_out));
		goto exit;
	}
	sco_in = pcm_open(0, 2, PCM_IN | PCM_MONOTONIC, &config);
	if (!sco_in || !pcm_is_ready(sco_in)) {
		ALOGE("%s: unable to open sco_in PCM device(%s)",
		      __func__, pcm_get_error(sco_in));
		goto exit;
	}

	buffer = (char *)malloc(size);

	pcm_start(voice_out);
	pcm_start(sco_in);

	while(!thread_exit) {
		ret = pcm_read(sco_in, buffer, size);
		if (ret) {
			if (errno != EBADFD)
				ALOGE("%s: failed to sco_in pcm_read(%d : %s)", __func__, errno, pcm_get_error(sco_in));
			continue;
		}
		ret = pcm_write(voice_out, buffer, size);
		if (ret) {
			if (errno != EBADFD)
				ALOGE("%s: failed to voice_out pcm_write(%d : %s)", __func__, errno, pcm_get_error(voice_out));
			continue;
		}
	}

	free(buffer);

	pcm_close(voice_out);
	pcm_close(sco_in);

	ALOGD("out %s", __func__);

exit:
	pthread_exit(&ret);
}

void start_bt_sco()
{
#ifdef SET_SCHED_PRIO
	pthread_attr_t attr1, attr2;
	struct sched_param param1, param2;
	int policy1, policy2;
#endif

	ALOGI("%s: enter %d", __func__, __LINE__);

	thread_exit = 0;

#ifdef SET_SCHED_PRIO
	pthread_attr_init(&attr1);
	pthread_attr_init(&attr2);

	memset(&param1, 0, sizeof(param1));
	memset(&param2, 0, sizeof(param2));

	pthread_attr_setdetachstate(&attr1, PTHREAD_CREATE_JOINABLE);
	pthread_attr_setdetachstate(&attr2, PTHREAD_CREATE_JOINABLE);

	pthread_create(&t_voice, &attr1, thread_voice, NULL);
	pthread_create(&t_sco, &attr2, thread_sco, NULL);

	pthread_getschedparam(t_voice, &policy1, &param1);
	pthread_getschedparam(t_sco, &policy2, &param2);

	param1.sched_priority = sched_get_priority_max(policy1);
	param2.sched_priority = sched_get_priority_max(policy2);

	pthread_setschedparam(t_voice, policy1, &param1);
	pthread_setschedparam(t_sco, policy2, &param2);

	pthread_attr_destroy(&attr1);
	pthread_attr_destroy(&attr2);
#else
	pthread_create(&t_voice, NULL, thread_voice, NULL);
	pthread_create(&t_sco, NULL, thread_sco, NULL);
#endif

	ALOGI("%s: exit %d", __func__, __LINE__);
}

void stop_bt_sco()
{
	int status;
	int rc;

	ALOGI("%s: enter %d", __func__, __LINE__);

	thread_exit = 1;

	pcm_stop(voice_in);
	pcm_stop(sco_out);
	rc = pthread_join(t_voice, (void **)&status);
	if (rc == 0)
		t_voice = (pthread_t)0;

	pcm_stop(sco_in);
	pcm_stop(voice_out);
	rc = pthread_join(t_sco, (void **)&status);
	if (rc == 0)
		t_sco = (pthread_t)0;

	ALOGI("%s: exit %d", __func__, __LINE__);
}

