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

#ifdef USES_ECNR
//#define HAS_LOGGER
#define __EXTRA_CMDL_SERVICE
#define __TEXT_ERRORS_PRINT

#include "spf-postapi.h"
#include "rs232.h"
#include "vcpCICmdImpl.h"
#ifdef HAS_LOGGER
#include "profile/BT_16KHz_Log Enable.c"
#else
#include "profile/BT_16KHz_Log Disable.c"
#endif
#endif

int thread_exit;

static pthread_t t_voice;
static pthread_t t_sco;

struct pcm *sco_out, *voice_in;
struct pcm *voice_out, *sco_in;

#ifdef USES_ECNR
#ifndef __VCP_API_CALIB_H__
#define EXTENSION1_TEST_PARAM(c, v)	(0)
#define EXTENSION1_TEST_INIT(r)		(0)
#define EXTENSION1_TEST_PROC(r)
#define EXTENSION1_TEST_FINISH()
#endif

typedef  int int32_t;
typedef  short int16_t;

mem_reg_t reg[NUM_MEM_REGIONS];
// #define FRAME 64 // for 8 kHz
//#define FRAME 128 // for 16 kHz
//#define FRAME 196 // for 24 kHz
#define FRAME 128 // for 16 kHz
short mic[FRAME], mic_c[FRAME];
short spk[FRAME], spk_c[FRAME];
//short mout[FRAME];
//short sout[FRAME];
short sout_t[FRAME];

static void *smr[NUM_MEM_REGIONS];
static pcm_t sout[FRAME], mout[FRAME];

//#define MUILTIFRAME_20MSEC
//#define HAS_LOGGER

#define    VCP_HAS_VVOL_TEST

#ifdef HAS_LOGGER
static uart_server_params_t logger_params = {0, };
static uart_server_params_t configurator_params = {0, };
#endif

static int nx_vcp_init(void) {
	unsigned int smem = 16000;
	void *mem;

	vcp_profile_t *p = NULL;
	err_t err;
	int i;
#if defined (VAD_OUTPUT_API)
	vcp_vad_t vad_out;
#endif
#if defined VCP_HAS_VVOL_TEST
	short vvol_set;
	//short value_get;
#if defined VCP_FLOAT
	float value_set;
#endif
#if defined VCP_16_BITS
	short value_set;
#else
	int value_set;
#endif
	int ret_set = 0;
	int ret_get = 0;

	// PARAM_TX_AF_VVOL  or PARAM_TX_AF_VVOL_NOM ?
#endif
	//if (EXTENSION1_TEST_PARAM(argc, argv) < 0)
	//	return -1;

	//p = parse_input_arguments(argc, argv);

	// DO NOT CHANGE PROFILE VALUES!!!
	p = &profile;
	// check vcp profile
	err = vcp_check_profile(p);
	if (err.err) {
		if (err.err == ERR_INVALID_CRC) {
			ALOGE("Profile error: Invalid CRC!\n");
		} else {
			ALOGE("Profile error: %s %d %d\n", __text_error[err.err], err.pid, err.memb);
		}
		return -1;
	}

	/* set vcp memory */
	memset((void *)reg, 0, sizeof(reg));
	smem = vcp_get_hook_size();
	mem = malloc(smem);

	vcp_get_mem_size(p, reg, mem);

	free(mem);

	ALOGE("Hello, I am Alango VCP8!\n");

	for (i = 0; i < NUM_MEM_REGIONS; i++) {
		reg[i].mem = smr[i] = (void *)malloc(reg[i].size);
		fprintf(stderr, "I need %d bytes of memory in memory region %d to work.\n",
			reg[i].size, i + 1);
	}

#if defined (HAS_LOGGER) && (defined(WIN32) || defined(_WIN32))
	err = vcp_init_debug(p, reg);
	if (err.err) {
		if (err.err == ERR_NOT_ENOUGH_MEMORY) {
			fprintf(stderr, "%d more bytes needed in region %d\n", -reg[err.pid].size, err.pid);
		} else if (err.err == ERR_UNKNOWN) {
			fprintf(stderr, "vcp_init_debug() returns UNKNOWN error!\n");
		} else if (err.err != ERR_NO_ERROR) {
			fprintf(stderr, "vcp_init_debug() returns error %s, pid %d\n", __text_error[err.err], err.pid);
		}

		for (int i = 0; i < NUM_MEM_REGIONS; i++) {
			if (smr[i] != NULL) {
				free(smr[i]);
			}
		}
		return -1;
	}

	// open log file
	logFile = fopen ("vcp8_log.bin", "wb");
	if (logFile == NULL) {
		ALOGE("Error: Can't open log file!\n");
	}
#else
	err.err = vcp_init(p, reg);
	if (err.err != ERR_NO_ERROR) {
		fprintf(stderr, "vcp_init() returns error %s\n", __text_error[err.err]);

		for (int i = 0; i < NUM_MEM_REGIONS; i++) {
			if (smr[i] != NULL) {
				free(smr[i]);
			}
		}
		return -1;
	}
#endif

	if (EXTENSION1_TEST_INIT(reg) < 0)
		return -1;

	memset(sout, 0, sizeof(sout));
	memset(mout, 0, sizeof(mout));

	return 0;
}
#endif

// tx
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

	size = config.period_size * 2 * config.channels;

	sco_out = pcm_open(SND_BT_SCO_CARD_ID, SND_BT_SCO_DEVICE_ID, PCM_OUT | PCM_MONOTONIC, &config);
	if (!sco_out || !pcm_is_ready(sco_out)) {
		ALOGE("%s: unable to open sco_out PCM device(%s)",
		      __func__, pcm_get_error(sco_out));
		goto exit;
	}

	voice_in = pcm_open(SND_BT_CARD_ID, SND_BT_DEVICE_ID, PCM_IN | PCM_MONOTONIC, &config);
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

#ifdef USES_ECNR
#ifdef HAS_LOGGER
		vcp_process_tx_debug(VcpReg, (short *)buffer, sout, mout);
		vcp_clients_postproc();
#else // HAS_LOGGER
		vcp_process_tx(reg, (short *)buffer, sout, mout);
#endif
		ret = pcm_write(sco_out, mout, size);
#else // USES_ECNR
		ret = pcm_write(sco_out, buffer, size);
#endif
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

// rx
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

	size = config.period_size * 2 * config.channels;

	voice_out = pcm_open(SND_BT_CARD_ID, SND_BT_DEVICE_ID, PCM_OUT | PCM_MONOTONIC, &config);
	if (!voice_out || !pcm_is_ready(voice_out)) {
		ALOGE("%s: unable to open voice_out PCM device(%s)",
		      __func__, pcm_get_error(voice_out));
		goto exit;
	}
	sco_in = pcm_open(SND_BT_SCO_CARD_ID, SND_BT_SCO_DEVICE_ID, PCM_IN | PCM_MONOTONIC, &config);
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

#ifdef USES_ECNR
#ifdef HAS_LOGGER
		vcp_clients_preproc();
		vcp_process_rx_debug(VcpReg, (short *)buffer, sout);
#else // HAS_LOGGER
		vcp_process_rx(reg, (short *)buffer, sout);
#endif
		ret = pcm_write(voice_out, sout, size);
#else // USES_ECNR
		ret = pcm_write(voice_out, buffer, size);
#endif
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

#ifdef USES_ECNR
#ifdef HAS_LOGGER
	/* initialize client logger */
	logger_params.portname = "/dev/ttyAMA0";
	logger_params.speed = B4000000;
	vcp_clients_add (VCP_CLIENT_TYPE_LOGGER, VCP_TRANSPORT_TYPE_UART,
		&logger_params);

	configurator_params.portname = "/dev/ttyAMA4";
	configurator_params.speed = B115200;
	vcp_clients_add (VCP_CLIENT_TYPE_CONFIGURATOR, VCP_TRANSPORT_TYPE_UART,
		&configurator_params);
#else // HAS_LOGGER
	nx_vcp_init();
#endif
#endif

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

#ifdef USES_ECNR
#ifdef HAS_LOGGER
	vcp_clients_remove_all();
#else // HAS_LOGGER
        // alango exit
	for (int i = 0; i < NUM_MEM_REGIONS; i++) {
		if (smr[i] != NULL) {
			free(smr[i]);
		}
	}
#endif
#endif
	ALOGI("%s: exit %d", __func__, __LINE__);
}

