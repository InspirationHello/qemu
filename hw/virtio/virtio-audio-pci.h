#ifndef QEMU_VIRTIO_AUDIO_PCI_H
#define QEMU_VIRTIO_AUDIO_PCI_H

#include "virtio-pci.h"
#include "hw/virtio/virtio-audio.h"

/* virtio-audio-pci */

typedef struct VirtIOAudioPCI VirtIOAudioPCI;

/*
 * virtio-audio-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_AUDIO_PCI "virtio-audio-pci"
#define VIRTIO_AUDIO_PCI(obj) \
        OBJECT_CHECK(VirtIOAudioPCI, (obj), TYPE_VIRTIO_AUDIO_PCI)
#define VIRTIO_AUDIO_PCI_GET_CLASS(obj) \
        OBJECT_GET_CLASS(VirtIOAudioPCIClass, obj, TYPE_VIRTIO_AUDIO_PCI)
#define VIRTIO_AUDIO_PCI_CLASS(klass) \
        OBJECT_CLASS_CHECK(VirtIOAudioPCIClass, klass, TYPE_VIRTIO_AUDIO_PCI)

typedef struct VirtIOAudioPCIClass {
    VirtioPCIClass parent_class;
    void (*realize)(VirtIOPCIProxy *vpci_dev, Error **errp);
} VirtIOAudioPCIClass;

struct VirtIOAudioPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOAudio vdev;
};

#endif
