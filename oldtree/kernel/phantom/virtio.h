
#include <phantom_types.h>
#include <virtio_ring.h>
#include <hal.h>
#include "i386/pci.h"

typedef struct virtio_ring
{
    int                 index; // pos in rings[] below
    physaddr_t          phys; // phys mem pointed by next struct's pointers
    struct vring        vr;
    int                 nFreeDescriptors; // Number of free buffer descriptors
    int                 freeHead; // index of head of free descs list
    int                 nAdded; // how many slots we added to avail ring[] and didnt add to avail idx
    int                 lastUsedIdx; // last idx in used ring we extracted buffer from


    hal_spinlock_t      lock;
} virtio_ring_t;


#define VIRTIO_LOCK(r) hal_spin_lock(&r->lock)
#define VIRTIO_UNLOCK(r) hal_spin_unlock(&r->lock)


#define VIRTIO_MAX_RINGS 4

typedef struct virtio_device
{
    int         basereg; // IO base addr
    int         irq;     // Interrupt no

    u_int32_t 	host_features;
    u_int32_t 	guest_features;

    pci_cfg_t * pci;     // for any case keep pointer to all PCI conf data

    void (*interrupt)(struct virtio_device *me, int isr );

    virtio_ring_t *     rings[VIRTIO_MAX_RINGS];

} virtio_device_t;

int virtio_probe( virtio_device_t *vd, pci_cfg_t *pci );


void virtio_reset( virtio_device_t *vd );

u_int8_t virtio_get_status( virtio_device_t *vd );
void virtio_set_status( virtio_device_t *vd, u_int8_t status );

void virtio_set_features( virtio_device_t *vd, u_int32_t features );
u_int32_t virtio_get_features( virtio_device_t *vd );

void virtio_get_config_struct( virtio_device_t *vd, void *buf, unsigned len );
void virtio_set_config_struct( virtio_device_t *vd, const void *buf, unsigned len );


int virtio_attach_buffers(virtio_device_t *vd, int qindex,
                          int nWrite, struct vring_desc *writeData,
                          int nRead, struct vring_desc *readData
                          );

int virtio_detach_buffer( virtio_device_t *vd, int qindex,
                          physaddr_t *buf, size_t *bufsize, size_t *datalen );

void virtio_kick(virtio_device_t *vd, int qindex);

