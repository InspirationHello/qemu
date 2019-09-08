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
#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/timer.h"
#include "qemu-common.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-audio.h"
#include "sysemu/kvm.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "qapi/qapi-events-misc.h"
#include "qapi/visitor.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "migration/misc.h"

#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"

static void virtio_audio_reset(VirtIODevice *vdev)
{

}

static void virtio_audio_set_status(VirtIODevice *vdev, uint8_t status)
{

}

static uint64_t virtio_audio_get_features(VirtIODevice *vdev, uint64_t f,
                                            Error **errp)
{

  return 0;
}

static void virtio_audio_set_config(VirtIODevice *vdev,
                                      const uint8_t *config_data)
{

}

static void virtio_audio_get_config(VirtIODevice *vdev, uint8_t *config_data)
{

}

static const VMStateDescription vmstate_virtio_audio = {
    .name = "virtio-audio",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_audio_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_audio_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{

}

static void virtio_audio_handle_rx(VirtIODevice *vdev, VirtQueue *vq)
{

}

static void virtio_audio_handle_tx_bh(VirtIODevice *vdev, VirtQueue *vq)
{

}

static void virtio_audio_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOAudio *vaudio = VIRTIO_AUDIO(dev);

    virtio_init(vdev, "virtio-audio", VIRTIO_ID_AUDIO,
                sizeof(struct virtio_audio_config));

    vaudio->rx_vq = virtio_add_queue(vdev, 128, virtio_audio_handle_rx);
    vaudio->tx_vq = virtio_add_queue(vdev, 128, virtio_audio_handle_tx_bh);
    vaudio->ctrl_vq = virtio_add_queue(vdev, 64, virtio_audio_handle_ctrl);
}

static void virtio_audio_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    //VirtIOAudio *vaudio = VIRTIO_AUDIO(dev);

    virtio_del_queue(vdev, 3);
    virtio_cleanup(vdev);
}

static void virtio_audio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = virtio_audio_properties;
    dc->vmsd = &vmstate_virtio_audio;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    vdc->realize = virtio_audio_device_realize;
    vdc->unrealize = virtio_audio_device_unrealize;
    vdc->get_config = virtio_audio_get_config;
    vdc->set_config = virtio_audio_set_config;
    vdc->get_features = virtio_audio_get_features;
    vdc->set_status = virtio_audio_set_status;
    vdc->reset = virtio_audio_reset;
}

static void virtio_audio_instance_init(Object *obj)
{
    VirtIOAudio *vaudio = VIRTIO_AUDIO(obj);

    /*
     * The default config_size is sizeof(struct virtio_audio_config).
     * Can be overriden with virtio_audio_set_config_size.
     */
    vaudio->config_size = sizeof(struct virtio_audio_config);
}

static const TypeInfo virtio_audio_info = {
    .name = TYPE_VIRTIO_AUDIO,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOAudio),
    .instance_init = virtio_audio_instance_init,
    .class_init = virtio_audio_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_audio_info);
}

type_init(virtio_register_types)
