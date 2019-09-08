#include "qemu/osdep.h"
#include "hw/audio/soundhw.h"
#include "audio/audio.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "sysemu/dma.h"

#include "hw/virtio/virtio-audio.h"
#include "hw/virtio/virtio-audio-pci.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"

static void VSoundCardLog (const char *cap, const char *fmt, ...)
{
    va_list ap;

    fprintf (stderr, "%s: ", cap);

    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
}

// #define DEBUG_VIOAUDIO
#ifdef DEBUG_VIOAUDIO
#define vsound_card_log(...) VSoundCardLog ("vsound_card", __VA_ARGS__)
#else
#define vsound_card_log(...)
#endif

#define TYPE_VSOUND_CARD "VSoundCard"
#define VSOUND_CARD(obj) \
    OBJECT_CHECK(VSoundCardState, (obj), TYPE_VSOUND_CARD)
#define VSOUND_CARD_STATE(obj) ((VSoundCardState *)(obj))

#define TYPE_VSOUND_CARD_OPTOR "VSoundCardOptor"
#define VSOUND_CARD_OPTOR(obj) \
     OBJECT_CHECK(VirtIOAudioOptor, (obj), TYPE_VSOUND_CARD_OPTOR)


typedef struct VSoundCardState {
    VirtIOAudioPCI      dev;
    
    QEMUSoundCard       card;
    
    SWVoiceIn          *voice_pi;
    SWVoiceOut         *voice_po;
    SWVoiceIn          *voice_mc;

    uint64_t            buffer_size;
    uint8_t            *buffer; // ring buffer for playback, one producer and one consumer only
    uint8_t            *record_buffer;

    int64_t             rpos;
    int64_t             wpos;

    virtio_audio_format format;

    uint8_t             mute;
    uint8_t             vol[2]; // 255
} VSoundCardState;    

enum {
    PI_INDEX = 0,
    PO_INDEX,
    MC_INDEX,
    LAST_INDEX
};

static void po_callback (void *opaque, int free);
static void pi_callback (void *opaque, int avail);
static void mc_callback (void *opaque, int avail);

static void vsoundcard_on_reset(DeviceState *dev);


static int write_audio (VSoundCardState *s, int max, int *stop)
{
    uint32_t written = 0, copied = 0;
    uint32_t start, chunk;
    int64_t buffer_size = s->buffer_size;
    int64_t buffer_mask = buffer_size - 1;

    int64_t wpos, rpos, to_copy = 0;

    wpos = atomic_read(&s->wpos);
    rpos = atomic_read(&s->rpos);
    
    to_copy = wpos - rpos;
    if (to_copy < 0) {
        to_copy += buffer_size;
    }
    to_copy = MIN(to_copy, max);
    
    if (!to_copy) {
        *stop = 1;
        
        if(max){
            vsound_card_log("%s buffer underrun, wpos %" PRId64 " rpos %" PRId64 "\n", 
                __func__, wpos, rpos);
        }
        
        return 0;
    }

#ifdef DUMP_PCM
    static FILE *pcm_fp = NULL;

    if(pcm_fp == NULL){
        pcm_fp = fopen("spice_virtio_audio_write_audio.pcm", "wb");
    }

    static FILE *pcm_fp_o = NULL;

    if(pcm_fp_o == NULL){
        pcm_fp_o = fopen("spice_virtio_audio_write_audio_o.pcm", "wb");
    }
#endif

    while(to_copy){
        start = (uint32_t)(rpos & buffer_mask);
        chunk = (uint32_t)MIN(buffer_size - start, to_copy);

#ifdef DUMP_PCM
        if(pcm_fp){
            fwrite(s->buffer + start, chunk, sizeof(char), pcm_fp);
        }
#endif

        copied = AUD_write (s->voice_po, s->buffer + start, chunk);
        vsound_card_log ("%s max=%x to_copy=%x chunk=%x copied=%x\n", __func__,
               max, to_copy, chunk, copied);
        if (!copied) {
            *stop = 1; // (chunk ? 0 : 1);
            // vsound_card_log("%s copy end\n", __func__);
            break;
        }

#ifdef DUMP_PCM
        if(pcm_fp_o){
            fwrite(s->buffer + start, copied, sizeof(char), pcm_fp_o);
        }
#endif

        to_copy -= copied;
        rpos    += copied;
        written += copied;

        if (chunk != copied) {
            vsound_card_log("%s chunk != copied, chunk %u copied %u\n", __func__, chunk, copied);
            // break;
        }
    }

    // s->rpos += written;
    atomic_set(&s->rpos, rpos & buffer_mask);

    if(!to_copy){
        // !!! TODO !!!
    }
        
    return written;
}

static int read_audio (VSoundCardState *s, int max, int *stop)
{
    uint32_t nread = 0;
    
    return nread;
}


static void transfer_audio (VSoundCardState *s, int index, int elapsed)
{
    int stop = 0;

    while ((elapsed >> 1) && !stop) {
        int temp;

        switch (index) {
        case PO_INDEX:
            temp = write_audio (s, elapsed, &stop);
            elapsed -= temp;
            break;

        case PI_INDEX:
        case MC_INDEX:
            temp = read_audio (s, elapsed, &stop);
            elapsed -= temp;
            break;
        }
        
    }
    
}


static void pi_callback (void *opaque, int avail)
{
    transfer_audio (opaque, PI_INDEX, avail);
}

static void mc_callback (void *opaque, int avail)
{
    transfer_audio (opaque, MC_INDEX, avail);
}

static void po_callback (void *opaque, int free)
{
    transfer_audio (opaque, PO_INDEX, free);
}


static void open_voice (VSoundCardState *s, int index, int freq)
{
    struct audsettings as;

    as.freq = freq;
    as.nchannels = 2;
    as.fmt = AUDIO_FORMAT_S16;
    as.endianness = 0;

    switch (index) {
        case PI_INDEX:
            s->voice_pi = AUD_open_in (
                &s->card,
                s->voice_pi,
                "vsound.pi",
                s,
                pi_callback,
                &as
                );
            break;

        case PO_INDEX:
            s->voice_po = AUD_open_out (
                &s->card,
                s->voice_po,
                "vsound.po",
                s,
                po_callback,
                &as
                );
            break;

        case MC_INDEX:
            s->voice_mc = AUD_open_in (
                &s->card,
                s->voice_mc,
                "vsound.mc",
                s,
                mc_callback,
                &as
                );
            break;
   }
   
}

static void reset_voices (VSoundCardState *s, uint8_t active[LAST_INDEX])
{
    int freq = (int)s->format.samples_per_sec;

    open_voice (s, PI_INDEX, freq);
    AUD_set_active_in (s->voice_pi, active[PI_INDEX]);

    open_voice (s, PO_INDEX, freq);
    AUD_set_active_out (s->voice_po, active[PO_INDEX]);

    open_voice (s, MC_INDEX, freq);
    AUD_set_active_in (s->voice_mc, active[MC_INDEX]);
}

static void voice_set_active (VSoundCardState *s, int bm_index, int on)
{
    switch (bm_index) {
    case PI_INDEX:
        AUD_set_active_in (s->voice_pi, on);
        break;

    case PO_INDEX:
        AUD_set_active_out (s->voice_po, on);
        break;

    case MC_INDEX:
        AUD_set_active_in (s->voice_mc, on);
        break;

    default:
        vsound_card_log ("invalid bm_index(%d) in voice_set_active", bm_index);
        break;
    }
}


static ssize_t flush_buf(VirtIOAudioOptor *optor,
                         const uint8_t *buf, ssize_t len)
{
    VirtIOAudioOptor *vsound_card_optor = VSOUND_CARD_OPTOR(optor);
    VirtIOAudioPCI *vaudio_pci = container_of(vsound_card_optor->vaudio, VirtIOAudioPCI, vdev); // DO_UPCAST (VirtIOAudioPCI, vdev, vsound_card_optor->vaudio);
    VSoundCardState *vsound_card = VSOUND_CARD(vaudio_pci);
    int64_t wpos = atomic_read(&vsound_card->wpos);
    int64_t rpos = atomic_read(&vsound_card->rpos);
    int64_t buffer_size = vsound_card->buffer_size;
    int64_t buffer_mask = buffer_size - 1;
    uint32_t start, chunk, copied = 0;
    int64_t to_transfer;

    vsound_card_log("%s vsound_card %p\n", __func__, vsound_card);

#ifdef DUMP_PCM
    static FILE *pcm_fp = NULL;

    if(pcm_fp == NULL){
        pcm_fp = fopen("spice_virtio_audio_flush_buffer.pcm", "wb");
    }

    if(pcm_fp){
        fwrite(buf, len, sizeof(char), pcm_fp);
    }
#endif

    to_transfer = rpos - wpos;
    if (to_transfer <= 0) {
        to_transfer += buffer_size;
    }
    to_transfer = MIN (len, to_transfer);

    vsound_card_log("%s rpos %" PRId64 " wpos %" PRId64 " len %u, to_transfer %d.\n", 
            __func__, rpos, wpos, len, to_transfer);

    while(to_transfer){
        start = (uint32_t)(wpos & buffer_mask);
        chunk = (uint32_t)MIN(buffer_size - start, to_transfer);

        memcpy(vsound_card->buffer + start, buf + copied, chunk);

        copied      += chunk;
        wpos        += chunk;
        to_transfer -= chunk;
        // vsound_card->wpos += chunk;
    }

    atomic_set(&vsound_card->wpos, wpos & buffer_mask);

    vsound_card_log("%s writted %u.\n", __func__, copied);

    return copied;
}

static void set_format(VirtIOAudioOptor *optor, virtio_audio_format *format)
{
    VirtIOAudioOptor *vsound_card_optor = VSOUND_CARD_OPTOR(optor);
    VirtIOAudioPCI *vaudio_pci = container_of(vsound_card_optor->vaudio, VirtIOAudioPCI, vdev); // DO_UPCAST (VirtIOAudioPCI, vdev, vsound_card_optor->vaudio);
    VSoundCardState *vsound_card = VSOUND_CARD(vaudio_pci);

    if(format){
        vsound_card->format = *format;

        vsoundcard_on_reset(DEVICE(vsound_card));
    }
}

static void set_disable(VirtIOAudioOptor *optor, int disable)
{
    VirtIOAudioOptor *vsound_card_optor = VSOUND_CARD_OPTOR(optor);
    VirtIOAudioPCI *vaudio_pci = container_of(vsound_card_optor->vaudio, VirtIOAudioPCI, vdev); // DO_UPCAST (VirtIOAudioPCI, vdev, vsound_card_optor->vaudio);
    VSoundCardState *vsound_card = VSOUND_CARD(vaudio_pci);

    
    
}

static void set_state(VirtIOAudioOptor *optor, int state)
{
    VirtIOAudioOptor *vsound_card_optor = VSOUND_CARD_OPTOR(optor);
    VirtIOAudioPCI *vaudio_pci = container_of(vsound_card_optor->vaudio, VirtIOAudioPCI, vdev); // DO_UPCAST (VirtIOAudioPCI, vdev, vsound_card_optor->vaudio);
    VSoundCardState *vsound_card = VSOUND_CARD(vaudio_pci);

    
}

static void set_volume(VirtIOAudioOptor *optor, int channel, int32_t volume)
{
    VirtIOAudioOptor *vsound_card_optor = VSOUND_CARD_OPTOR(optor);
    VirtIOAudioPCI *vaudio_pci = container_of(vsound_card_optor->vaudio, VirtIOAudioPCI, vdev); // DO_UPCAST (VirtIOAudioPCI, vdev, vsound_card_optor->vaudio);
    VSoundCardState *vsound_card = VSOUND_CARD(vaudio_pci);

    int mute = vsound_card->mute;
    int64_t vol, rvol, lvol;

    vsound_card_log("%s\n", __func__);

    vol = volume + 6291456;
    rvol = lvol = vol * 255LL / (6291456LL);

    fprintf(stderr, "set_volume volume %d, vol %ld, right %ld, left %ld\n", 
        volume, vol, lvol, rvol);

    vsound_card->vol[0] = (uint8_t)lvol;
    vsound_card->vol[1] = (uint8_t)rvol;

    AUD_set_volume_out (vsound_card->voice_po, mute, vsound_card->vol[0], vsound_card->vol[1]);
}

static void set_mute(VirtIOAudioOptor *optor, int mute)
{
    VirtIOAudioOptor *vsound_card_optor = VSOUND_CARD_OPTOR(optor);
    VirtIOAudioPCI *vaudio_pci = container_of(vsound_card_optor->vaudio, VirtIOAudioPCI, vdev); // DO_UPCAST (VirtIOAudioPCI, vdev, vsound_card_optor->vaudio);
    VSoundCardState *vsound_card = VSOUND_CARD(vaudio_pci);

    vsound_card_log("%s\n", __func__);

    vsound_card->mute = (uint8_t)mute;

    AUD_set_volume_out (vsound_card->voice_po, vsound_card->mute, vsound_card->vol[0], vsound_card->vol[1]);
}

static int vsoundcard_init (PCIBus *bus)
{
    DeviceState *dev, *opter_dev;
    VirtIOAudio *vaudio;
    VirtIOAudioPCI *vaudio_pci;
    PCIDevice *pci_dev;

    vsound_card_log("%s\n", __func__);

    pci_dev = pci_create_simple (bus, -1,  TYPE_VSOUND_CARD);
    if(!pci_dev){
        vsound_card_log("%s Failed to create VSoundCard pci device\n", __func__);
        return -1;
    }

    dev = &pci_dev->qdev;
    if(!dev){
        vsound_card_log("%s Failed to create VSoundCard device\n", __func__);
        return -1;
    }

    vaudio_pci = VIRTIO_AUDIO_PCI(dev);

    vaudio = &vaudio_pci->vdev;
    
    vsound_card_log("%s vaudio %p\n", __func__, vaudio);

    opter_dev = qdev_create(BUS(&vaudio->bus), TYPE_VSOUND_CARD_OPTOR);
    if(!opter_dev){
        vsound_card_log("%s Failed to create VSoundCardOptor device\n", __func__);
        return -1;
    }

    qdev_init_nofail(opter_dev);
    
    return 0;
}

static Property vsoundcard_optor_properties[] = {
    DEFINE_PROP_END_OF_LIST (),
};

static void vsoundcard_optor_realize(DeviceState *dev, Error **errp)
{
    VirtIOAudioOptor *optor = VIRTIO_AUDIO_OPTOR(dev);
    VirtIOAudioOptorClass *k = VIRTIO_AUDIO_OPTOR_GET_CLASS(dev);

    vsound_card_log("%s\n", __func__);
    vsound_card_log("%s audio optor %p\n", __func__, optor->vaudio->optor);

    virtio_audio_open(optor->vaudio);
}

static void vsoundcard_optor_unrealize(DeviceState *dev, Error **errp)
{
    vsound_card_log("%s\n", __func__);
}

static void vsoundcard_optor_class_init (ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS (klass);
    VirtIOAudioOptorClass *k = VIRTIO_AUDIO_OPTOR_CLASS(klass);

    k->realize = vsoundcard_optor_realize;
    k->unrealize = vsoundcard_optor_unrealize;
    k->have_data = flush_buf;
    k->set_format = set_format;
    k->set_disable = set_disable;
    k->set_volume = set_volume;
    k->set_mute = set_mute;
    // k->set_guest_connected = set_guest_connected;

    dc->props = vsoundcard_optor_properties;
}

static size_t vsoundcard_try_fill_buffer(const char *file_name, uint8_t *buffer, size_t size)
{
    FILE * pcm_fp;
    long file_size, to_read;
    size_t read_rst = 0;
    
    pcm_fp = fopen(file_name, "rb");
    if(!pcm_fp) {
        vsound_card_log("%s failed to open wav file for read\n", __func__);
        return 0;
    }

    // obtain file size:
    fseek (pcm_fp , 0 , SEEK_END);
    file_size = ftell (pcm_fp);
    rewind (pcm_fp);

    to_read = (long)MIN(file_size, size);

    vsound_card_log("%s try read %ld, file size %ld\n", __func__, to_read, file_size);

    read_rst = fread (buffer, to_read, 1, pcm_fp);
    if (read_rst != to_read) {
        vsound_card_log("%s failed to read file\n", __func__);

        fclose (pcm_fp);
        return read_rst;
    }

    fclose (pcm_fp);

    vsound_card_log("%s try read %ld successed\n", __func__, to_read);

    return to_read;
}

static uint32_t roundup_pow_of_two(uint32_t x)
{
    x--;

    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;

    x++;

    return x;
}

static void vsoundcard_instance_init(Object *obj)
{
    VSoundCardState *sc_state = VSOUND_CARD_STATE(obj);

    vsound_card_log("%s\n", __func__);
    vsound_card_log("%s vsound_card %p\n", __func__, sc_state);

    sc_state->vol[0] = 64;
    sc_state->vol[1] = 64;

    // init default audio pcm format
    sc_state->format.channels = 2;
    sc_state->format.samples_per_sec = 48000;
    sc_state->format.avg_bytes_per_sec = 19200;
    sc_state->format.bits_per_sample = 16;

    // allocate ring buffer
    sc_state->buffer_size = roundup_pow_of_two(1024 * 1024 * 2);
    sc_state->buffer = g_malloc0(sc_state->buffer_size * sizeof(*sc_state->buffer));

#ifdef DEBUG_VSOUND_LATENCY
    // reset the wpos for start, try check if some error not cause by transfer latency.
    sc_state->wpos = vsoundcard_try_fill_buffer("try_fill_buffer.pcm", sc_state->buffer, sc_state->buffer_size);
#endif
}

static void vsoundcard_realize(VirtIOPCIProxy *dev, Error **errp)
{
    VSoundCardState *s = VSOUND_CARD(dev);

    vsound_card_log("%s\n", __func__);
    vsound_card_log("%s vsound_card %p\n", __func__, s);

    AUD_register_card ("vsoundcard", &s->card);

    vsoundcard_on_reset(DEVICE(s));
}

static void vsoundcard_unrealize(VirtIOPCIProxy *dev)
{
    VSoundCardState *s = VSOUND_CARD(dev);

    vsound_card_log("%s\n", __func__);

    AUD_close_in(&s->card, s->voice_pi);
    AUD_close_out(&s->card, s->voice_po);
    AUD_close_in(&s->card, s->voice_mc);
    
    AUD_remove_card(&s->card);
}

static void vsoundcard_on_reset(DeviceState *dev)
{
    VSoundCardState *s = VSOUND_CARD(dev); // container_of(dev, VSoundCardState, dev.vdev);
    uint8_t active[LAST_INDEX] = { 0 };
        
    vsound_card_log("%s, sound card %p\n", __func__, s);

    reset_voices(s, active);

    voice_set_active (s, PO_INDEX, 1);
}

static void vsoundcard_class_init (ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS (klass);
    VirtIOAudioPCIClass *k = VIRTIO_AUDIO_PCI_CLASS(klass);
    
    vsound_card_log("%s\n", __func__);

    k->realize = vsoundcard_realize;
    
#if 0
    dc->realize = vsoundcard_realize;
    dc->unrealize = vsoundcard_unrealize;
#endif

    dc->reset = vsoundcard_on_reset;
}

static const TypeInfo vsoundcard_optor_info = {
    .name          = TYPE_VSOUND_CARD_OPTOR,
    .parent        = TYPE_VIRTIO_AUDIO_OPTOR,
    .class_init    = vsoundcard_optor_class_init,
};


static const TypeInfo vsoundcard_info = {
    .name          = TYPE_VSOUND_CARD,
    .parent        = TYPE_VIRTIO_AUDIO_PCI,
    .instance_size = sizeof (VSoundCardState),
    .instance_init = vsoundcard_instance_init,
    .class_init    = vsoundcard_class_init,
};

static void vsoundcard_register_types (void)
{
    type_register_static (&vsoundcard_optor_info);
    type_register_static (&vsoundcard_info);
    
    pci_register_soundhw("vsoundcard", "Virtual sound card", vsoundcard_init);
}

type_init (vsoundcard_register_types)

#pragma GCC diagnostic pop
