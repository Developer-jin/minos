#ifndef __MINOS_VIRT_VMM_H__
#define __MINOS_VIRT_VMM_H__

#include <minos/types.h>
#include <minos/mm.h>
#include <minos/mmu.h>
#include <config/config.h>

/*
 * below is the memory map for guest vm
 * - max physic size 1T
 * - 0x0 - 0x3fcfffff is the resve memory space
 * - 0x3fd00000 - 0x40000000 is the shared memory space
 * - 0x40000000 - 0x7fffffff is the io space
 * - 0x80000000 - 0x80000000 + 512G is the normal memory
 */

#define GUSET_PHYSIC_MEM_MAX_SIZE	(1UL << 40)
#define GUEST_PHYSIC_MEM_START		(0x0000000000000000UL)

#define GUEST_IO_MEM_START		(0x00000000UL)
#define GUEST_IO_MEM_SIZE		(0x80000000UL)

#ifdef CONFIG_PLATFORM_FVP
#define VM0_MMAP_MEM_START		(0xc0000000UL)
#define VM0_MMAP_MEM_SIZE		(0x40000000UL)
#define GUEST_NORMAL_MEM_START		(0x80000000UL)
#define GUSET_MEMORY_END		(0xc0000000UL)
#else
#define VM0_MMAP_MEM_START		(0x80000000UL)
#define VM0_MMAP_MEM_SIZE		(0x40000000UL)
#define GUEST_NORMAL_MEM_START		(0xc0000000UL)
#define GUSET_MEMORY_END		(0x000000ffffffffffUL)
#endif

#define VM_MMAP_MAX_SIZE		(VM0_MMAP_MEM_SIZE / CONFIG_MAX_VM)
#define VM_MMAP_ENTRY_COUNT		(VM_MMAP_MAX_SIZE >> MEM_BLOCK_SHIFT)

#define VM_VIRT_IO_MEM_START		(VM0_MMAP_MEM_START)
#define VM_VIRT_IO_MEM_SIZE		(VM_MMAP_MAX_SIZE)

struct vm;

/*
 * page_table_base : the lvl0 table base
 * mem_list : static config memory region for this vm
 * block_list : the mem_block allocated for this vm
 * head : the pages table allocated for this vm
 */
struct mm_struct {
	size_t mem_size;
	size_t mem_free;
	unsigned long mem_base;
	unsigned long pgd_base;
	struct page *head;
	struct list_head mem_list;
	struct list_head block_list;
	spinlock_t lock;
};

int register_memory_region(struct memtag *res);
int vm_mm_init(struct vm *vm);

int map_vm_memory(struct vm *vm, unsigned long vir_base,
		unsigned long phy_base, size_t size, int type);
int unmap_vm_memory(struct vm *vm, unsigned long vir_addr,
			size_t size, int type);

int alloc_vm_memory(struct vm *vm, unsigned long start, size_t size);
void release_vm_memory(struct vm *vm);

int create_host_mapping(unsigned long vir, unsigned long phy,
		size_t size, unsigned long flags);
int destroy_host_mapping(unsigned long vir, size_t size);

static inline int
io_remap(unsigned long vir, unsigned long phy, size_t size)
{
	return create_host_mapping(vir, phy, size, VM_IO);
}

static inline int
io_unmap(unsigned long vir, size_t size)
{
	return destroy_host_mapping(vir, size);
}

unsigned long get_vm_mmap_info(int vmid, unsigned long *size);
int vm_mmap(struct vm *vm, unsigned long offset, unsigned long size);
void vm_unmmap(struct vm *vm);

#endif
