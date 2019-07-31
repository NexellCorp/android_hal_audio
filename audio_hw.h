#include <tinyalsa/asoundlib.h>
#include <hardware/audio.h>

struct audio_device {
    struct audio_hw_device device;
    audio_devices_t out_device;
    audio_devices_t in_device;

    pthread_mutex_t lock; /* see note below on mutex acquisition order */

    struct mixer *mixer;

    audio_mode_t mode;

    int *snd_dev_ref_cnt;

    struct audio_route *audio_route;
    struct audio_route *audio_route2;
    audio_source_t input_source;
    int cur_route_id;

    float voice_volume;
    bool voice_mic_mute;
    bool mic_muted;
    bool master_mute;

    int snd_card;

    unsigned int last_patch_id;

    bool hfp_enable;
    bool output_streaming;
    struct pcm_config hfp_pcm_config;

#ifdef USES_NXVOICE
    bool use_nxvoice;
    struct nx_smartvoice_config nxvoice_config;
    void *nxvoice_handle;
#endif
};

