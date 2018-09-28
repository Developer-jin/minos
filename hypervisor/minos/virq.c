/*
 * Copyright (C) 2018 Min Le (lemin9538@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <minos/minos.h>
#include <minos/irq.h>
#include <minos/sched.h>
#include <minos/virq.h>
#include <minos/virt.h>

static DEFINE_SPIN_LOCK(hvm_irq_lock);

static inline struct virq_desc *
get_virq_desc(struct vcpu *vcpu, uint32_t virq)
{
	struct vm *vm = vcpu->vm;

	if (virq < VM_LOCAL_VIRQ_NR)
		return &vcpu->virq_struct->local_desc[virq];

	if (virq >= VM_VIRQ_NR(vm->vspi_nr))
		return NULL;

	return &vm->vspi_desc[VIRQ_SPI_OFFSET(virq)];
}

static void inline virq_kick_vcpu(struct vcpu *vcpu,
		struct virq_desc *desc)
{
	kick_vcpu(vcpu);
}

static int __send_virq(struct vcpu *vcpu,
		uint32_t vno, uint32_t hno, int hw, int pr)
{
	int index;
	unsigned long flags;
	struct virq *virq;
	struct virq_struct *virq_struct = vcpu->virq_struct;

	spin_lock_irqsave(&virq_struct->lock, flags);

	/*
	 * The following cases are considered software programming
	 * errors and result in UNPREDICTABLE behavior:
	 *
	 * • Having a List register entry with ICH_LR<n>_EL2.HW= 1
	 *   which is associated with a physical interrupt, inactive
	 *   state or in pending state in the List registers if the
	 *   Distributor does not have the corresponding physical
	 *   interrupt in either the active state or the active and
	 *   pending state.
	 * • If ICC_CTLR_EL1.EOImode == 0 or ICC_CTLR_EL3.EOImode_EL3 == 0
	 *   then either:
	 *   — Having an active interrupt in the List registers with a priorit
	 *   that is not set in the corresponding Active Priorities Register.
	 *   — Having two interrupts in the List registers in the active stat
	 *   with the same preemption priority.>
	 * • Having two or more interrupts with the same pINTID in the Lis
	 *   registers for a single virtual CPU interface.
	 */
	for_each_set_bit(index, virq_struct->irq_bitmap,
			CONFIG_VCPU_MAX_ACTIVE_IRQS) {
		virq = &virq_struct->virqs[index];
		if (virq->v_intno == vno) {
			if (virq->state == VIRQ_STATE_PENDING) {
				if ((vno == 33) || (vno == 34))
					printf("------1\n");
				spin_unlock_irqrestore(&virq_struct->lock, flags);
				return 0;
			} else {
				if ((vno == 33) || (vno == 34))
					printf("------2\n");
				goto out;
			}
		}
	}

	index = find_first_zero_bit(virq_struct->irq_bitmap,
			CONFIG_VCPU_MAX_ACTIVE_IRQS);
	if (index == CONFIG_VCPU_MAX_ACTIVE_IRQS) {
		/*
		 * no empty resource to handle this virtual irq
		 * need to drop it ? TBD
		 */
		pr_error("Can not send this virq now %d\n", vno);
		spin_unlock(&virq_struct->lock);
		return -EAGAIN;
	}

	virq = &virq_struct->virqs[index];
	virq->h_intno = hno;
	virq->v_intno = vno;
	virq->hw = hw;
	virq->pr = pr;
	set_bit(index, virq_struct->irq_bitmap);

out:
	virq->state = VIRQ_STATE_PENDING;
	if (virq->list.next == NULL)
		list_add_tail(&virq_struct->pending_list, &virq->list);

	spin_unlock_irqrestore(&virq_struct->lock, flags);

	return 0;
}

static int inline send_virq(struct vcpu *vcpu, struct virq_desc *desc, int hw)
{
	int ret;
	struct vm *vm = vcpu->vm;

	if (!desc || !desc->enable)
		return -EINVAL;

	/* do not send irq to vm if not online or suspend state */
	if ((vm->state == VM_STAT_OFFLINE) ||
			(vm->state == VM_STAT_REBOOT)) {
		pr_warn("send virq failed vm is offline or reboot\n");
		return -EINVAL;
	}

	/*
	 * check the state of the vm, if the vm
	 * is in suspend state and the irq can not
	 * wake up the vm, just return other wise
	 * need to kick the vcpu
	 */
	if (vm->state == VM_STAT_SUSPEND) {
		if (!(desc->flags & VIRQ_FLAGS_CAN_WAKEUP)) {
			pr_warn("send virq failed vm is suspend\n");
			return -EAGAIN;
		}
	}

	ret = __send_virq(vcpu, desc->vno, desc->hno, hw, desc->pr);
	if (ret) {
		pr_warn("send virq to vcpu-%d-%d failed\n",
				get_vmid(vcpu), get_vcpu_id(vcpu));
		return ret;
	}

	virq_kick_vcpu(vcpu, desc);
	return 0;
}

static int guest_irq_handler(uint32_t irq, void *data)
{
	struct vcpu *vcpu;
	struct virq_desc *desc = (struct virq_desc *)data;

	if ((!desc) || (!desc->hw))
		return -EINVAL;

	/* send the virq to the guest */
	if ((desc->vmid == VIRQ_AFFINITY_ANY) &&
			(desc->vcpu_id == VIRQ_AFFINITY_ANY))
		vcpu = current_vcpu;
	else
		vcpu = get_vcpu_by_id(desc->vmid, desc->vcpu_id);

	if (!vcpu) {
		pr_error("%s: Can not get the vcpu for irq:%d\n", irq);
		return -ENOENT;
	}

	return send_virq(vcpu, desc, desc->hw);
}

uint32_t virq_get_type(struct vcpu *vcpu, uint32_t virq)
{
	struct virq_desc *desc;

	desc = get_virq_desc(vcpu, virq);
	if (!desc)
		return -ENOENT;

	return desc->type;
}

int virq_set_type(struct vcpu *vcpu, uint32_t virq, int value)
{
	struct virq_desc *desc;

	desc = get_virq_desc(vcpu, virq);
	if (!desc)
		return -ENOENT;

	/*
	 * 0 - IRQ_TYPE_LEVEL_HIGH
	 * 1 - IRQ_TYPE_EDGE_RISING
	 */
	if (desc->type != value) {
		desc->type = value;
		if (desc->hw) {
			if (value)
				value = IRQ_FLAGS_EDGE_RISING;
			else
				value = IRQ_FLAGS_LEVEL_HIGH;

			irq_set_type(desc->hno, value);
		}
	}

	return 0;
}

int virq_set_priority(struct vcpu *vcpu, uint32_t virq, int pr)
{
	struct virq_desc *desc;

	desc = get_virq_desc(vcpu, virq);
	if (!desc) {
		pr_debug("virq is no exist %d\n", virq);
		return -ENOENT;
	}

	pr_debug("set the pr:%d for virq:%d\n", pr, virq);
	desc->pr = pr;

	return 0;
}

int virq_enable(struct vcpu *vcpu, uint32_t virq)
{
	struct virq_desc *desc;

	desc = get_virq_desc(vcpu, virq);
	if (!desc)
		return -ENOENT;

	desc->enable = 1;

	if (virq > VM_LOCAL_VIRQ_NR) {
		if (desc->hw)
			irq_unmask(desc->hno);
	}

	return 0;
}

int virq_disable(struct vcpu *vcpu, uint32_t virq)
{
	struct virq_desc *desc;

	desc = get_virq_desc(vcpu, virq);
	if (!desc)
		return -ENOENT;

	desc->enable = 0;

	if (virq > VM_LOCAL_VIRQ_NR) {
		if (desc->hw)
			irq_mask(desc->hno);
	}

	return 0;
}

int __send_virq_to_vcpu(struct vcpu *vcpu, uint32_t virq, int hw)
{
	int ret;
	unsigned long flags;
	struct virq_desc *desc;

	desc = get_virq_desc(vcpu, virq);
	local_irq_save(flags);
	ret = send_virq(vcpu, desc, hw);
	local_irq_restore(flags);

	return ret;
}

int send_hirq_to_vcpu(struct vcpu *vcpu, uint32_t virq)
{
	return __send_virq_to_vcpu(vcpu, virq, 1);
}

int send_virq_to_vcpu(struct vcpu *vcpu, uint32_t virq)
{
	return __send_virq_to_vcpu(vcpu, virq, 0);
}

int send_virq_to_vm(struct vm *vm, uint32_t virq)
{
	struct virq_desc *desc;
	struct vcpu *vcpu;

	if ((!vm) || (virq < VM_LOCAL_VIRQ_NR))
		return -EINVAL;

	desc = get_virq_desc(vm->vcpus[0], virq);
	if (!desc)
		return -ENOENT;

	if (desc->hw) {
		pr_error("can not send hw irq in here\n");
		return -EPERM;
	}

	vcpu = get_vcpu_in_vm(vm, desc->vcpu_id);
	if (!vcpu)
		return -ENOENT;

	return send_virq(vcpu, desc, desc->hw);
}

void send_vsgi(struct vcpu *sender, uint32_t sgi, cpumask_t *cpumask)
{
	int cpu;
	unsigned long flags;
	struct vcpu *vcpu;
	struct vm *vm = sender->vm;
	struct virq_desc *desc;

	local_irq_save(flags);
	for_each_set_bit(cpu, cpumask->bits, vm->vcpu_nr) {
		vcpu = vm->vcpus[cpu];
		desc = get_virq_desc(vcpu, sgi);
		send_virq(vcpu, desc, desc->hw);
	}
	local_irq_restore(flags);
}

void clear_pending_virq(struct vcpu *vcpu, uint32_t irq)
{
	int bit;
	struct virq *virq;
	unsigned long flags;
	struct virq_struct *virq_struct = vcpu->virq_struct;

	/*
	 * this function can only called by the current
	 * running vcpu and excuted on the related pcpu
	 */
	spin_lock_irqsave(&virq_struct->lock, flags);

	for_each_set_bit(bit, virq_struct->irq_bitmap,
			CONFIG_VCPU_MAX_ACTIVE_IRQS) {
		virq = &virq_struct->virqs[bit];

		if ((virq->v_intno == irq) &&
			(virq->state == VIRQ_STATE_PENDING)) {
			irq_update_virq(virq, VIRQ_ACTION_REMOVE);
			if (virq->list.next != NULL)
				list_del(&virq->list);
			virq->state = VIRQ_STATE_INACTIVE;
			clear_bit(bit, virq_struct->irq_bitmap);
		}
	}

	spin_unlock_irqrestore(&virq_struct->lock, flags);
}

static int irq_enter_to_guest(void *item, void *data)
{
	/*
	 * here we send the real virq to the vcpu
	 * before it enter to guest
	 */
	struct virq *virq, *n;
	struct vcpu *vcpu = (struct vcpu *)item;
	struct virq_struct *virq_struct = vcpu->virq_struct;

	spin_lock(&virq_struct->lock);

	list_for_each_entry_safe(virq, n, &virq_struct->pending_list, list) {
		if (virq->state != VIRQ_STATE_PENDING) {
			pr_error("something was wrong with this irq %d\n", virq->id);
			continue;
		}

		virq->state = VIRQ_STATE_ACTIVE;
		irq_send_virq(virq);
		list_del(&virq->list);
		list_add_tail(&virq_struct->active_list, &virq->list);
		if (virq->hw)
			virq_struct->pending_hirq++;
		else
			virq_struct->pending_virq++;
	}

	spin_unlock(&virq_struct->lock);

	return 0;
}

static int irq_exit_from_guest(void *item, void *data)
{
	/*
	 * here we update the states of the irq state
	 * which the vcpu is handles, since this is running
	 * on percpu and hanlde per_vcpu's data so do not
	 * need spinlock
	 */
	int status;
	struct virq *virq, *n;
	struct vcpu *vcpu = (struct vcpu *)item;
	struct virq_struct *virq_struct = vcpu->virq_struct;

	spin_lock(&virq_struct->lock);

	list_for_each_entry_safe(virq, n, &virq_struct->active_list, list) {

		status = irq_get_virq_state(virq);

		/*
		 * the virq has been handled by the VCPU
		 */
		if (status == VIRQ_STATE_INACTIVE) {
			if (virq->state == VIRQ_STATE_ACTIVE) {
				virq->state = VIRQ_STATE_INACTIVE;
				irq_update_virq(virq, VIRQ_ACTION_CLEAR);
				list_del(&virq->list);
				virq->list.next = NULL;
				clear_bit(virq->id, virq_struct->irq_bitmap);
				if (virq->hw)
					virq_struct->pending_hirq--;
				else
					virq_struct->pending_virq--;
			} else if (virq->state == VIRQ_STATE_PENDING) {
				irq_update_virq(virq, VIRQ_ACTION_CLEAR);
				list_del(&virq->list);
				list_add_tail(&virq_struct->pending_list, &virq->list);
			}
		} else {
			if (virq->state == VIRQ_STATE_PENDING)
				virq->state = VIRQ_STATE_ACTIVE;
		}
	}

	spin_unlock(&virq_struct->lock);

	return 0;
}

void vcpu_virq_struct_reset(struct vcpu *vcpu)
{
	int i;
	struct virq *virq;
	struct virq_struct *virq_struct = vcpu->virq_struct;

	virq_struct->active_count = 0;
	spin_lock_init(&virq_struct->lock);
	init_list(&virq_struct->pending_list);
	init_list(&virq_struct->active_list);
	virq_struct->pending_virq = 0;
	virq_struct->pending_hirq = 0;

	bitmap_clear(virq_struct->irq_bitmap,
			0, CONFIG_VCPU_MAX_ACTIVE_IRQS);
	/*
	 * reset the virq struct to the default state, the
	 * local irq desc do not need to reset, and keep the
	 * default vaule
	 */
	for (i = 0; i < CONFIG_VCPU_MAX_ACTIVE_IRQS; i++) {
		virq = &virq_struct->virqs[i];
		virq->h_intno = 0;
		virq->v_intno = 0;
		virq->state = VIRQ_STATE_INACTIVE;
		virq->id = i;
		virq->hw = 0;
		virq->list.next = NULL;
	}
}

void vcpu_virq_struct_init(struct vcpu *vcpu)
{
	int i;
	struct virq_desc *desc;
	struct virq *virq;
	struct virq_struct *virq_struct = vcpu->virq_struct;

	if (!virq_struct)
		return;

	virq_struct->active_count = 0;
	spin_lock_init(&virq_struct->lock);
	init_list(&virq_struct->pending_list);
	init_list(&virq_struct->active_list);
	virq_struct->pending_virq = 0;
	virq_struct->pending_hirq = 0;

	bitmap_clear(virq_struct->irq_bitmap,
			0, CONFIG_VCPU_MAX_ACTIVE_IRQS);
	memset(&virq_struct->local_desc, 0,
			sizeof(struct virq_desc) * VM_LOCAL_VIRQ_NR);

	for (i = 0; i < CONFIG_VCPU_MAX_ACTIVE_IRQS; i++) {
		virq = &virq_struct->virqs[i];
		virq->h_intno = 0;
		virq->v_intno = 0;
		virq->state = VIRQ_STATE_INACTIVE;
		virq->id = i;
		virq->hw = 0;
		init_list(&virq->list);
		virq->list.next = NULL;
	}

	for (i = 0; i < VM_LOCAL_VIRQ_NR; i++) {
		desc = &virq_struct->local_desc[i];
		desc->hw = 0;
		desc->enable = 1;
		desc->vcpu_id = vcpu->vcpu_id;
		desc->vmid = vcpu->vm->vmid;
		desc->vno = i;
		desc->hno = 0;
	}
}

static void vm_config_virq(struct vm *vm)
{
	int i, j;
	struct irqtag *irqtag;
	struct virq_desc *desc;
	struct vcpu *c;

	for (i = 0; i < mv_config->nr_irqtag; i++) {
		irqtag = &mv_config->irqtags[i];

		/* config local virq if local irq report as hw irq*/
		if (irqtag->vno < VM_LOCAL_VIRQ_NR) {
			for (j = 0; j < vm->vcpu_nr; j++) {
				c = vm->vcpus[j];
				desc = &c->virq_struct->local_desc[irqtag->vno];
				desc->hw = 1;
				desc->hno = irqtag->hno;
			}

			continue;
		}

		if (vm->vmid != irqtag->vmid)
			continue;

		if (irqtag->vno >= vm->vspi_nr)
			continue;

		desc = &vm->vspi_desc[VIRQ_SPI_OFFSET(irqtag->vno)];
		desc->hw = 1;
		desc->enable = 0;
		desc->vno = irqtag->vno;
		desc->hno = irqtag->hno;
		desc->vcpu_id = irqtag->vcpu_id;
		desc->pr = 0xa0;
		desc->vmid = vm->vmid;
		set_bit(VIRQ_SPI_OFFSET(desc->vno), vm->vspi_map);

		c = get_vcpu_by_id(desc->vmid, desc->vcpu_id);
		if (!c)
			continue;

		irq_set_affinity(desc->hno, c->affinity);
		request_irq(desc->hno, guest_irq_handler,
			    IRQ_FLAGS_VCPU, c->name, (void *)desc);
		irq_mask(desc->hno);
	}
}

int alloc_vm_virq(struct vm *vm)
{
	int virq;
	int count = vm->vspi_nr;
	struct virq_desc *desc;

	if (vm->vmid == 0)
		spin_lock(&hvm_irq_lock);

	virq = find_next_zero_bit_loop(vm->vspi_map, count, 0);
	if (virq >= count)
		virq = -1;

	if (virq >= 0) {
		desc = &vm->vspi_desc[virq];
		desc->vno = virq + VM_LOCAL_VIRQ_NR;
		desc->hw = 0;
		desc->vmid = vm->vmid;
		set_bit(virq, vm->vspi_map);
	}

	if (vm->vmid == 0)
		spin_unlock(&hvm_irq_lock);

	return (virq >= 0 ? virq + VM_LOCAL_VIRQ_NR : -1);
}

void release_vm_virq(struct vm *vm, int virq)
{
	if (vm->vmid == 0)
		spin_lock(&hvm_irq_lock);

	clear_bit(VIRQ_SPI_OFFSET(virq), vm->vspi_map);

	if (vm->vmid == 0)
		spin_unlock(&hvm_irq_lock);
}

static int virq_create_vm(void *item, void *args)
{
	uint32_t vspi_nr;
	uint32_t size, tmp, map_size;
	struct vm *vm = (struct vm *)item;

	if (vm->vmid == 0)
		vspi_nr = HVM_SPI_VIRQ_NR;
	else
		vspi_nr = GVM_SPI_VIRQ_NR;

	tmp = sizeof(struct virq_desc) * vspi_nr;
	tmp = BALIGN(tmp, sizeof(unsigned long));
	size = PAGE_BALIGN(tmp);
	vm->virq_same_page = 0;
	vm->vspi_desc = get_free_pages(PAGE_NR(size));
	if (!vm->vspi_desc)
		return -ENOMEM;

	map_size = BITS_TO_LONGS(vspi_nr) * sizeof(unsigned long);

	if ((size - tmp) >= map_size) {
		vm->vspi_map = (unsigned long *)
			((unsigned long)vm->vspi_desc + tmp);
		vm->virq_same_page = 1;
	} else {
		vm->vspi_map = malloc(BITS_TO_LONGS(vspi_nr) *
				sizeof(unsigned long));
		if (!vm->vspi_map)
			return -ENOMEM;
	}

	memset(vm->vspi_desc, 0, tmp);
	memset(vm->vspi_map, 0, BITS_TO_LONGS(vspi_nr) *
			sizeof(unsigned long));
	vm->vspi_nr = vspi_nr;

	vm_config_virq(vm);

	return 0;
}

void vm_virq_reset(struct vm *vm)
{
	int i;
	struct virq_desc *desc;

	/* reset the all the spi virq for the vm */
	for ( i = 0; i < vm->vspi_nr; i++) {
		desc = &vm->vspi_desc[i];
		desc->enable = 0;
		desc->pr = 0x0;
		desc->type = 0x0;

		if (desc->hw)
			irq_mask(desc->hno);
	}
}

static int virq_destroy_vm(void *item, void *data)
{
	int i;
	struct virq_desc *desc;
	struct vm *vm = (struct vm *)item;

	if (vm->vspi_desc) {
		for (i = 0; i < VIRQ_SPI_NR(vm->vspi_nr); i++) {
			desc = &vm->vspi_desc[i];

			/* should check whether the hirq is pending or not */
			if (desc->enable && desc->hw &&
					desc->hno > VM_LOCAL_VIRQ_NR)
				irq_mask(desc->hno);
		}

		free(vm->vspi_desc);
	}

	if (!vm->virq_same_page)
		free(vm->vspi_map);

	return 0;
}

void virqs_init(void)
{
	register_hook(irq_exit_from_guest,
			MINOS_HOOK_TYPE_EXIT_FROM_GUEST);

	register_hook(irq_enter_to_guest,
			MINOS_HOOK_TYPE_ENTER_TO_GUEST);

	register_hook(virq_create_vm, MINOS_HOOK_TYPE_CREATE_VM);
	register_hook(virq_destroy_vm, MINOS_HOOK_TYPE_DESTROY_VM);
}
