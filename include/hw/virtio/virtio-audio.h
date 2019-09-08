/*
 * Virtio audio Support
 *
 * Copyright (c) 2017 Intel Corperation.
 *
 * Authors:
 *    Luo Xionghu<xionghu.luo@intel.com>
 *    Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef _QEMU_VIRTIO_AUDIO_H
#define _QEMU_VIRTIO_AUDIO_H

#include "standard-headers/linux/virtio_audio.h"
#include "hw/virtio/virtio.h"
#include "sysemu/iothread.h"


#define DEBUG_VIRTIO_AUDIO 1

#define APRINTF(fmt, ...) \
do { \
    if (DEBUG_VIRTIO_AUDIO) { \
        fprintf(stderr, "virtio_audio: " fmt, ##__VA_ARGS__); \
    } \
} while (0)


typedef struct virtio_audio_format virtio_audio_format;


#define TYPE_VIRTIO_AUDIO "virtio-audio-device"
#define VIRTIO_AUDIO(obj) \
        OBJECT_CHECK(VirtIOAudio, (obj), TYPE_VIRTIO_AUDIO)
#define VIRTIO_AUDIO_GET_PARENT_CLASS(obj) \
        OBJECT_GET_PARENT_CLASS(obj, TYPE_VIRTIO_AUDIO)

#define TYPE_VIRTIO_AUDIO_OPTOR "virtio-audio-operator"
#define VIRTIO_AUDIO_OPTOR(obj) \
     OBJECT_CHECK(VirtIOAudioOptor, (obj), TYPE_VIRTIO_AUDIO_OPTOR)
#define VIRTIO_AUDIO_OPTOR_CLASS(klass) \
     OBJECT_CLASS_CHECK(VirtIOAudioOptorClass, (klass), TYPE_VIRTIO_AUDIO_OPTOR)
#define VIRTIO_AUDIO_OPTOR_GET_CLASS(obj) \
     OBJECT_GET_CLASS(VirtIOAudioOptorClass, (obj), TYPE_VIRTIO_AUDIO_OPTOR)

#define TYPE_VIRTIO_AUDIO_BUS "virtio-audio-bus"
#define VIRTIO_AUDIO_BUS(obj) \
      OBJECT_CHECK(VirtIOAudioBus, (obj), TYPE_VIRTIO_AUDIO_BUS)

typedef struct VirtIOAudio VirtIOAudio;
typedef struct VirtIOAudioOptor VirtIOAudioOptor;
typedef struct VirtIOAudioOptorClass VirtIOAudioOptorClass;
typedef struct VirtIOAudioBus VirtIOAudioBus;

struct VirtIOAudioBus {
    BusState qbus;

    /* This is the parent device that provides the bus for ports. */
    VirtIOAudio *vaudio;
    
    uint32_t reserved;
};

struct VirtIOAudio {
    VirtIODevice parent_obj;

    VirtIOAudioBus bus;

    VirtQueue *vqs;
    VirtQueue *rx_vq, *tx_vq;
    VirtQueue *ctrl_in_vq, *ctrl_out_vq;

    /* only one? not array or list!? */
    VirtIOAudioOptor *optor;

    uint32_t max_queues;
    uint32_t status;

    int multiqueue;
    uint32_t curr_queues;
    size_t config_size;
};

struct VirtIOAudioOptorClass {
    DeviceClass parent_class;

    /*
     * The per-port (or per-app) realize function that's called when a
     * new device is found on the bus.
     */
    DeviceRealize realize;
    /*
     * Per-port unrealize function that's called when a port gets
     * hot-unplugged or removed.
     */
    DeviceUnrealize unrealize;

    /* Callbacks for guest events */
    /* Guest opened/closed device. */
    void (*set_guest_connected)(VirtIOAudioOptor *optor, int guest_connected);

    /* Guest is now ready to accept data (virtqueues set up). */
    void (*guest_ready)(VirtIOAudioOptor *optor);

    /*
     * Guest has enqueued a buffer for the host to write into.
     * Called each time a buffer is enqueued by the guest;
     * irrespective of whether there already were free buffers the
     * host could have consumed.
     *
     * This is dependent on both the guest and host end being
     * connected.
     */
    void (*guest_writable)(VirtIOAudioOptor *optor);

    /*
     * Guest wrote some data to the port. This data is handed over to
     * the app via this callback.  The app can return a size less than
     * 'len'.  In this case, throttling will be enabled for this port.
     */
    ssize_t (*have_data)(VirtIOAudioOptor *optor, const uint8_t *buf,
                         ssize_t len);

    void (*set_format)(VirtIOAudioOptor *optor, virtio_audio_format *format);

    void (*set_disable)(VirtIOAudioOptor *optor, int disable);
    
    void (*set_state)(VirtIOAudioOptor *optor, int state);

    void (*set_volume)(VirtIOAudioOptor *optor, int channel, int32_t volume);

    void (*set_mute)(VirtIOAudioOptor *optor, int mute);
};

struct VirtIOAudioOptor {
    DeviceState dev;

    /* Is the corresponding guest device open? */
    bool guest_connected;
    /* Is this device open for IO on the host? */
    bool host_connected;
    /* Do apps not want to receive data? */
    bool throttled;

    VirtIOAudio *vaudio;

    QTAILQ_ENTRY(VirtIOAudioOptor) next;

};


struct virtio_audio_config {
    uint32_t  status;
};

struct virtio_audio_format {
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t samples_per_sec;
    uint32_t avg_bytes_per_sec;
};

int virtio_audio_open(VirtIOAudio *vaudio);


#endif /* _QEMU_VIRTIO_AUDIO_H */

