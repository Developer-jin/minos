#ifndef __MINOS_VDEV_H__
#define __MINOS_VDEV_H__

#include <minos/types.h>
#include <minos/list.h>
#include <asm/arch.h>
#include <minos/virq.h>

#define VDEV_NAME_SIZE	(15)

struct vdev {
	char name[VDEV_NAME_SIZE];
	struct vm *vm;
	void *iomem;
	uint32_t mem_size;
	unsigned long gvm_paddr;
	unsigned long hvm_paddr;
	int host;
	struct list_head list;
	int (*read)(struct vdev *, gp_regs *,
			unsigned long, unsigned long *);
	int (*write)(struct vdev *, gp_regs *,
			unsigned long, unsigned long *);
	void (*deinit)(struct vdev *vdev);
	void (*reset)(struct vdev *vdev);
};

struct vdev *create_host_vdev(struct vm *vm,
		unsigned long base, uint32_t size);
struct vdev *create_guest_vdev(struct vm *vm, uint32_t size);
void vdev_release(struct vdev *vdev);
int guest_vdev_init(struct vm *vm, struct vdev *vdev, uint32_t size);
int host_vdev_init(struct vm *vm, struct vdev *vdev,
		unsigned long base, uint32_t size);
int vdev_mmio_emulation(gp_regs *regs, int write,
		unsigned long address, unsigned long *value);
void vdev_set_name(struct vdev *vdev, char *name);

static int inline vdev_notify_gvm(struct vdev *vdev, uint32_t irq)
{
	return send_virq_to_vm(vdev->vm, irq);
}

static int inline vdev_notify_hvm(struct vdev *vdev, uint32_t irq)
{
	return send_virq_to_vm(get_vm_by_id(0), irq);
}

#endif
