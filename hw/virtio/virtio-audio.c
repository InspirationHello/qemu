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
#include "hw/sysbus.h"
#include "hw/virtio/virtio.h"
#include "hw/i386/pc.h"
#include "hw/virtio/virtio-audio.h"
#include "sysemu/kvm.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "qapi/qapi-events-misc.h"
#include "qapi/visitor.h"
#include "monitor/monitor.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "migration/misc.h"

#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

static void VioAudioLog (const char *cap, const char *fmt, ...)
{
    va_list ap;

    fprintf (stderr, "%s: ", cap);

    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
}

// #define DEBUG_VIOAUDIO
#ifdef DEBUG_VIOAUDIO
#define vioaudio_log(...) VioAudioLog ("virtio-audio", __VA_ARGS__)
#else
#define vioaudio_log(...)
#endif


static void discard_vq_data(VirtQueue *vq, VirtIODevice *vdev)
{
    VirtQueueElement *elem;

    if (!virtio_queue_ready(vq)) {
        return;
    }

    while (true) {
        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            break;
        }

        virtqueue_push(vq, elem, 0);
    }

    virtio_notify(vdev, vq);
}

static void do_flush_queued_data(VirtIOAudioOptor *optor, VirtIODevice *vdev, VirtQueue *vq)
{
    VirtQueueElement *elem;
    VirtIOAudio *vaudio;
    VirtIOAudioOptorClass *vaoptc;
    uint8_t *buf;
    size_t len, ret;

    vaudio = VIRTIO_AUDIO(vdev);    
    vaoptc = VIRTIO_AUDIO_OPTOR_GET_CLASS(optor);

    len = 0;
    buf = NULL;

    while (true) {
        size_t cur_len;

        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            break;
        }

        cur_len = iov_size(elem->out_sg, elem->out_num);
    
        if (cur_len > len) {
            g_free(buf);

            buf = g_malloc(cur_len);
            len = cur_len;
        }

        iov_to_buf(elem->out_sg, elem->out_num, 0, buf, cur_len);

        vioaudio_log("received from guest: %u.\n", cur_len);

        if(vaoptc->have_data){
            ret = vaoptc->have_data(optor, buf, cur_len);
        }else{
            ret = cur_len;
        }
        
        virtqueue_push(vq, elem, 0);
    }
    
    g_free(buf);
    virtio_notify(vdev, vq);
}

static size_t send_ctrl_msg(VirtIOAudio *vaudio, void *buf, size_t len)
{
    VirtQueueElement *elem;
    VirtQueue *vq;

    vq = vaudio->ctrl_in_vq;
    
    if (!virtio_queue_ready(vq)) {
        return 0;
    }
    
    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    if (!elem) {
        return 0;
    }

    memcpy(elem->in_sg[0].iov_base, buf, len);

    virtqueue_push(vq, elem, len);
    virtio_notify(VIRTIO_DEVICE(vaudio), vq);
    
    return len;
}

struct virtio_audio_control {
    __virtio16 event;
    __virtio16 value;
};

enum VIRTIO_AUDIO_DEVICE_EVENT {
    VIRTIO_AUDIO_DEVICE_READY = 0,
    VIRTIO_AUDIO_DEVICE_OPEN,
    VIRTIO_AUDIO_DEVICE_CLOSE,
    VIRTIO_AUDIO_DEVICE_ENABLE,
    VIRTIO_AUDIO_DEVICE_DISABLE,
    VIRTIO_AUDIO_DEVICE_SET_FORMAT,
    VIRTIO_AUDIO_DEVICE_SET_STATE,
    VIRTIO_AUDIO_DEVICE_SET_VOLUME,
    VIRTIO_AUDIO_DEVICE_SET_MUTE,
    VIRTIO_AUDIO_DEVICE_GET_MUTE,
    NR_VIRTIO_AUDIO_DEVICE_EVENT
};

static size_t send_ctrl_event(VirtIOAudio *vaudio,
                                 uint16_t event, uint16_t value)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vaudio);
    struct virtio_audio_control cpkt;

    virtio_stw_p(vdev, &cpkt.event, event);
    virtio_stw_p(vdev, &cpkt.value, value);

    return send_ctrl_msg(vaudio, &cpkt, sizeof(cpkt));
}

int virtio_audio_open(VirtIOAudio *vaudio)
{
    VirtIOAudioOptor *optor = vaudio->optor;

    if (!optor || optor->host_connected) {
        return 0;
    }

    optor->host_connected = true;
    send_ctrl_event(vaudio, VIRTIO_AUDIO_DEVICE_OPEN, 1);

    return 0;
}

static void virtio_audio_reset(VirtIODevice *vdev)
{

}

static void virtio_audio_set_status(VirtIODevice *vdev, uint8_t status)
{

}

static uint64_t virtio_audio_get_features(VirtIODevice *vdev, uint64_t f,  Error **err)
{
    return 0; // f
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

static Property virtio_audio_optor_props[] = {
    DEFINE_PROP_END_OF_LIST (),
};

/* Guest wants to notify us of some event */
static void handle_ctrl_msg(VirtIOAudio *vaudio, void *buf, size_t len)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vaudio);
    VirtIOAudioOptorClass *vaoptc;
    VirtIOAudioOptor *optor;
    struct virtio_audio_control cpkt, *gcpkt;
    uint8_t *buffer;
    size_t buffer_len;
    virtio_audio_format *format;
    int32_t *pvalue;

    optor = vaudio->optor;
    gcpkt = buf;

    if (len < sizeof(cpkt)) {
        /* The guest sent an invalid control packet */
        return;
    }
    
    if (!optor) {
        return;
    }

    vaoptc = VIRTIO_AUDIO_OPTOR_GET_CLASS(optor);

    cpkt.event = virtio_lduw_p(vdev, &gcpkt->event);
    cpkt.value = virtio_lduw_p(vdev, &gcpkt->value);

    switch(cpkt.event) {
    case VIRTIO_AUDIO_DEVICE_READY:
        vioaudio_log("received from guest: VIRTIO_AUDIO_DEVICE_READY %d.\n", cpkt.value);
    
        if (cpkt.value) {
            send_ctrl_event(vaudio, VIRTIO_AUDIO_DEVICE_OPEN, 1);
        }else{
            error_report("virtio-audio-pci: Guest failure in init device.");
        }
        
        break;
    case VIRTIO_AUDIO_DEVICE_OPEN:
        vioaudio_log("received from guest: VIRTIO_AUDIO_DEVICE_OPEN %d.\n", cpkt.value);
        
        optor->guest_connected = cpkt.value;

        if (vaoptc->set_guest_connected) {
            /* Send the guest opened notification if an app is interested */
            vaoptc->set_guest_connected(optor, cpkt.value);
        }
        
        break;
    case VIRTIO_AUDIO_DEVICE_SET_FORMAT:
        fprintf(stderr, "VIRTIO_AUDIO_DEVICE_SET_FORMAT\n");

        format = (virtio_audio_format*)((uint8_t*)gcpkt + sizeof(*gcpkt));
        fprintf(stderr, "VIRTIO_AUDIO_DEVICE_SET_FORMAT freq %u, channels: %u, avg rate: %u\n", 
                format->samples_per_sec, format->channels, format->avg_bytes_per_sec);

        if (vaoptc->set_format) {
            vaoptc->set_format(optor, format);
        }
        break;
    case VIRTIO_AUDIO_DEVICE_ENABLE:
        fprintf(stderr, "VIRTIO_AUDIO_DEVICE_ENABLE\n");

        if (vaoptc->set_disable) {
            vaoptc->set_disable(optor, false);
        }
        break;
    case VIRTIO_AUDIO_DEVICE_DISABLE:
        fprintf(stderr, "VIRTIO_AUDIO_DEVICE_DISABLE\n");

        if (vaoptc->set_disable) {
            vaoptc->set_disable(optor, cpkt.value);
        }
        break;
    case VIRTIO_AUDIO_DEVICE_SET_STATE:
        fprintf(stderr, "VIRTIO_AUDIO_DEVICE_SET_STATE %d\n", cpkt.value);

        if (vaoptc->set_state) {
            vaoptc->set_state(optor, cpkt.value);
        }
        break;
    case VIRTIO_AUDIO_DEVICE_SET_VOLUME:
        pvalue = (int32_t*)((uint8_t*)gcpkt + sizeof(*gcpkt));
        
        fprintf(stderr, "VIRTIO_AUDIO_DEVICE_SET_VOLUME %d %d\n", cpkt.value, *pvalue);

        if (vaoptc->set_volume) {
            vaoptc->set_volume(optor, 0, *pvalue);
        }
        
        break;
    case VIRTIO_AUDIO_DEVICE_SET_MUTE:
        fprintf(stderr, "VIRTIO_AUDIO_DEVICE_SET_MUTE %d\n", cpkt.value);

        if (vaoptc->set_mute) {
            vaoptc->set_mute(optor, cpkt.value);
        }
        break;
    default:
        break;
    }
}

static void virtio_audio_handle_ctrl_in(VirtIODevice *vdev, VirtQueue *vq)
{
    vioaudio_log("%s.\n", __func__);
}

static void virtio_audio_handle_ctrl_out(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtQueueElement *elem;
    VirtIOAudio *vaudio;
    uint8_t *buf;
    size_t len;

    vaudio = VIRTIO_AUDIO(vdev);

    vioaudio_log("%s.\n", __func__);

    len = 0;
    buf = NULL;

    while (true) {
        size_t cur_len;

        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if(!elem){
            break;
        }

        cur_len = iov_size(elem->out_sg, elem->out_num);
        /*
         * Allocate a new buf only if we didn't have one previously or
         * if the size of the buf differs
         */
        if (cur_len > len) {
            g_free(buf);

            buf = g_malloc(cur_len);
            len = cur_len;
        }
        
        iov_to_buf(elem->out_sg, elem->out_num, 0, buf, cur_len);

        handle_ctrl_msg(vaudio, buf, cur_len);
        
        virtqueue_push(vq, elem, 0);
    }
    
    g_free(buf);
    virtio_notify(vdev, vq);
}

static void virtio_audio_handle_rx(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOAudio *vaudio = VIRTIO_AUDIO(vdev);
    VirtIOAudioOptor *optor = vaudio->optor;
    VirtIOAudioOptorClass *vaoptc;

    vioaudio_log("%s optor: %p.\n", __func__, optor);

    if (!optor) {
        return;
    }
    
    vaoptc = VIRTIO_AUDIO_OPTOR_GET_CLASS(optor);

    /*
     * If guest_connected is false, this call is being made by the
     * early-boot queueing up of descriptors, which is just noise for
     * the host apps -- don't disturb them in that case.
     */
    if (optor->guest_connected && optor->host_connected && vaoptc->guest_writable) {
        vaoptc->guest_writable(optor);
    }
}

static void virtio_audio_handle_tx_bh(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOAudio *vaudio = VIRTIO_AUDIO(vdev);
    VirtIOAudioOptor *optor = vaudio->optor;

    vioaudio_log("%s optor: %p.\n", __func__, optor);

    if(!optor || !optor->host_connected){
        discard_vq_data(vq, vdev);
        return;
    }

    do_flush_queued_data(optor, vdev, vq);
}

static void virtio_audio_bus_dev_print(Monitor *mon, DeviceState *qdev, int indent)
{
    VirtIOAudioOptor *optor = DO_UPCAST(VirtIOAudioOptor, dev, qdev);

    vioaudio_log("%s device %s realized %d\n", __func__, qdev->id, qdev->realized);
#if 0
    monitor_printf(mon, "%*sport %d, guest %s, host %s, throttle %s\n",
                   indent, "", port->id,
                   optor->guest_connected ? "on" : "off",
                   optor->host_connected ? "on" : "off",
                   optor->throttled ? "on" : "off");
#endif
}

static void virtio_audio_bus_realize(BusState *bus, Error **errp)
{
    vioaudio_log("%s\n", __func__);
}

static void virtio_audio_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);
    k->print_dev = virtio_audio_bus_dev_print;
    k->realize = virtio_audio_bus_realize;
}

static const TypeInfo virtio_audio_bus_info = {
    .name = TYPE_VIRTIO_AUDIO_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(VirtIOAudioBus),
    .class_init = virtio_audio_bus_class_init,
};

static void virtio_audio_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOAudio *vaudio = VIRTIO_AUDIO(dev);

    vioaudio_log("%s\n", __func__);

    virtio_init(vdev, "virtio-audio", VIRTIO_ID_AUDIO,
                sizeof(struct virtio_audio_config));

     /* Spawn a new virtio-serial bus on which the ports will ride as devices */
    qbus_create_inplace(&vaudio->bus, sizeof(vaudio->bus), TYPE_VIRTIO_AUDIO_BUS,
                        dev, vdev->bus_name);
    // qbus_set_hotplug_handler(BUS(&vaudio->bus), DEVICE(vaudio), errp);
    
    vioaudio_log("%s class: %p, vaudio: %p\n", __func__, BUS(&vaudio->bus)->obj.class, vaudio);

    vaudio->bus.vaudio = vaudio;

    vaudio->rx_vq = virtio_add_queue(vdev, 128, virtio_audio_handle_rx);
    vaudio->tx_vq = virtio_add_queue(vdev, 128, virtio_audio_handle_tx_bh);
    vaudio->ctrl_in_vq = virtio_add_queue(vdev, 32, virtio_audio_handle_ctrl_in);
    vaudio->ctrl_out_vq = virtio_add_queue(vdev, 32, virtio_audio_handle_ctrl_out);
}

static void virtio_audio_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    // VirtIOAudio *vaudio = VIRTIO_AUDIO(dev);

    virtio_del_queue(vdev, 4);
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

static void virtio_audio_optor_device_realize(DeviceState *dev, Error **errp)
{
    // object_new(TYPE_VIRTIO_AUDIO_OPTOR);
    VirtIOAudioOptor *optor = VIRTIO_AUDIO_OPTOR(dev);
    VirtIOAudioOptorClass *vaoptc = VIRTIO_AUDIO_OPTOR_GET_CLASS(optor);
    VirtIOAudioBus *bus = VIRTIO_AUDIO_BUS(qdev_get_parent_bus(dev));

    Error *err = NULL;

    vioaudio_log("%s\n", __func__);
    
    optor->vaudio = bus->vaudio;

    // force assign !!!only one!!!.
    bus->vaudio->optor = optor;

    vaoptc->realize(dev, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
}

static void virtio_audio_optor_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIOAudioOptor *optor = VIRTIO_AUDIO_OPTOR(dev);
    VirtIOAudioOptorClass *vaoptc = VIRTIO_AUDIO_OPTOR_GET_CLASS(optor);
    VirtIOAudio *vaudio = optor->vaudio;

    if (vaoptc->unrealize) {
        vaoptc->unrealize(dev, errp);
    }
}

static void virtio_audio_optor_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_SOUND, k->categories);
    k->bus_type = TYPE_VIRTIO_AUDIO_BUS;
    k->realize = virtio_audio_optor_device_realize;
    k->unrealize = virtio_audio_optor_device_unrealize;
    k->props = virtio_audio_optor_props;
}

static const TypeInfo virtio_audio_optor_type_info = {
    .name = TYPE_VIRTIO_AUDIO_OPTOR,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(VirtIOAudioOptor),
    .abstract = true,
    .class_size = sizeof(VirtIOAudioOptorClass),
    .class_init = virtio_audio_optor_class_init,
};

static const TypeInfo virtio_audio_info = {
    .name = TYPE_VIRTIO_AUDIO,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOAudio),
    .instance_init = virtio_audio_instance_init,
    .class_init = virtio_audio_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_audio_bus_info);
    type_register_static(&virtio_audio_info);
    type_register_static(&virtio_audio_optor_type_info);
}

type_init(virtio_register_types)

#pragma GCC diagnostic pop