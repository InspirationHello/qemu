/*
 * Virtio audio PCI Bindings
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"

#include "virtio-pci.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-audio.h"
#include "qemu/module.h"
#include "qapi/error.h"

/* virtio-audio-pci */

typedef struct VirtIOAudioPCI VirtIOAudioPCI;

/*
 * virtio-audio-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_AUDIO_PCI "virtio-audio-pci"
#define VIRTIO_AUDIO_PCI(obj) \
        OBJECT_CHECK(VirtIOAudioPCI, (obj), TYPE_VIRTIO_AUDIO_PCI)

struct VirtIOAudioPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOAudio vdev;
};
 

static void virtio_audio_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOAudioPCI *vaudio = VIRTIO_AUDIO_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&vaudio->vdev);
    Error *err = NULL;

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
}

static void virtio_audio_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    k->realize = virtio_audio_pci_realize;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);

    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_AUDIO;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
}

static void virtio_audio_initfn(Object *obj)
{
    VirtIOAudioPCI *dev = VIRTIO_AUDIO_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_AUDIO);
}

static const VirtioPCIDeviceTypeInfo virtio_audio_pci_info = {
    .base_name             = TYPE_VIRTIO_AUDIO_PCI,
    .generic_name          = "virtio-audio-pci",
    .transitional_name     = "virtio-audio-pci-transitional",
    .non_transitional_name = "virtio-audio-pci-non-transitional",
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIOAudioPCI),
    .instance_init = virtio_audio_initfn,
    .class_init    = virtio_audio_pci_class_init,
};

static void virtio_audio_pci_register(void)
{
    virtio_pci_types_register(&virtio_audio_pci_info);
}

type_init(virtio_audio_pci_register)
