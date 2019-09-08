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


#define TYPE_VIRTIO_AUDIO "virtio-audio-device"
#define VIRTIO_AUDIO(obj) \
        OBJECT_CHECK(VirtIOAudio, (obj), TYPE_VIRTIO_AUDIO)
#define VIRTIO_AUDIO_GET_PARENT_CLASS(obj) \
        OBJECT_GET_PARENT_CLASS(obj, TYPE_VIRTIO_AUDIO)

struct virtio_audio_config {
	uint32_t  status;

};

typedef struct VirtIOAudio {
    VirtIODevice parent_obj;

    VirtQueue *vqs;
    VirtQueue *rx_vq;
    VirtQueue *tx_vq;
    VirtQueue *ctrl_vq;

    uint32_t max_queues;
    uint32_t status;

    int multiqueue;
    uint32_t curr_queues;
    size_t config_size;
} VirtIOAudio;

#endif /* _QEMU_VIRTIO_AUDIO_H */

