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
#include <minos/vmodule.h>
#include <asm/vtimer.h>
#include <minos/io.h>
#include <asm/processer.h>
#include <asm/exception.h>
#include <minos/timer.h>
#include <minos/irq.h>
#include <minos/sched.h>
#include <minos/virq.h>

int vtimer_vmodule_id = INVAILD_MODULE_ID;

#define get_access_vtimer(vtimer, c, access)		\
	do {						\
		vtimer = &c->phy_timer;			\
	} while (0)

static void phys_timer_expire_function(unsigned long data)
{
	struct vtimer *vtimer = (struct vtimer *)data;

	vtimer->cnt_ctl |= CNT_CTL_ISTATUS;
	vtimer->cnt_cval = 0;

	if (!(vtimer->cnt_ctl & CNT_CTL_IMASK))
		send_virq_to_vcpu(vtimer->vcpu, vtimer->virq);
}

static void virt_timer_expire_function(unsigned long data)
{
	struct vtimer *vtimer = (struct vtimer *)data;

	send_virq_to_vcpu(vtimer->vcpu, vtimer->virq);
}

static void vtimer_state_restore(struct vcpu *vcpu, void *context)
{
	struct vtimer_context *c = (struct vtimer_context *)context;
	struct vtimer *vtimer = &c->virt_timer;

	del_timer(&vtimer->timer);

	write_sysreg64(c->offset, CNTVOFF_EL2);
	write_sysreg64(vtimer->cnt_cval, CNTV_CVAL_EL0);
	write_sysreg32(vtimer->cnt_ctl, CNTV_CTL_EL0);
	dsb();
}

static void vtimer_state_save(struct vcpu *vcpu, void *context)
{
	struct vtimer_context *c = (struct vtimer_context *)context;
	struct vtimer *vtimer = &c->virt_timer;

	dsb();
	vtimer->cnt_ctl = read_sysreg32(CNTV_CTL_EL0);
	write_sysreg32(vtimer->cnt_ctl & ~CNT_CTL_ENABLE, CNTV_CTL_EL0);
	vtimer->cnt_cval = read_sysreg64(CNTV_CVAL_EL0);

	if (vcpu->state == VCPU_STAT_IDLE)
		return;

	if ((vtimer->cnt_ctl & CNT_CTL_ENABLE) &&
		!(vtimer->cnt_ctl & CNT_CTL_IMASK)) {
		mod_timer(&vtimer->timer, ticks_to_ns(vtimer->cnt_cval +
				c->offset - boot_tick));
	}

	dsb();
}

static void vtimer_state_init(struct vcpu *vcpu, void *context)
{
	struct vtimer *vtimer;
	struct vtimer_context *c = (struct vtimer_context *)context;

	if (get_vcpu_id(vcpu) == 0)
		vcpu->vm->time_offset = NOW();

	memset(c, 0, sizeof(struct vtimer_context));
	c->offset = vcpu->vm->time_offset;

	vtimer = &c->virt_timer;
	vtimer->vcpu = vcpu;
	init_timer_on_cpu(&vtimer->timer, vcpu->affinity);
	vtimer->timer.function = virt_timer_expire_function;
	vtimer->timer.data = (unsigned long)vtimer;
	vtimer->virq = 27;
	vtimer->cnt_ctl = 0;
	vtimer->cnt_cval = 0;

	vtimer = &c->phy_timer;
	vtimer->vcpu = vcpu;
	init_timer_on_cpu(&vtimer->timer, vcpu->affinity);
	vtimer->timer.function = phys_timer_expire_function;
	vtimer->timer.data = (unsigned long)vtimer;
	vtimer->virq = 30;
	vtimer->cnt_ctl = 0;
	vtimer->cnt_cval = 0;
}

static void vcpu_release_vtimer(void *data)
{
	struct vtimer *vtimer = (struct vtimer *)data;

	del_timer(&vtimer->timer);
	pr_info("delete timer of vtimer in smp call done\n");
}

static void vtimer_state_deinit(struct vcpu *vcpu, void *context)
{
	int i;
	struct vtimer *vtimers[2];
	struct vtimer *vtimer;
	int cpuid = smp_processor_id();
	struct vtimer_context *c = (struct vtimer_context *)context;

	/*
	 * need to delete the vcpu's vtimer before release
	 * the vcpu, if the vcpu is on the current pcpu, del
	 * it directly, otherwise need to call smp_function_call
	 * to delete it on the pcpu which it belongs to, to avoid
	 * race
	 */
	vtimers[0] = &c->virt_timer;
	vtimers[1] = &c->phy_timer;

	for (i = 0; i < 2; i++) {
		vtimer = vtimers[i];
		if (vcpu_affinity(vcpu) != cpuid) {
			pr_debug("delete vtimer using smp call\n");
			smp_function_call(vcpu_affinity(vcpu),
				vcpu_release_vtimer, vtimer, 1);
		} else
			del_timer(&vtimer->timer);

		pr_info("delete vtimer for vcpu done %d\n");
	}
}

static void vtimer_handle_cntp_ctl(gp_regs *regs,
		int access, int read, unsigned long *value)
{
	uint32_t v;
	struct vtimer *vtimer;
	struct vtimer_context *c = (struct vtimer_context *)
		get_vmodule_data_by_id(current_vcpu, vtimer_vmodule_id);

	get_access_vtimer(vtimer, c, access);

	if (read) {
		*value = vtimer->cnt_ctl;
	} else {
		v = (uint32_t)(*value);
		v &= ~CNT_CTL_ISTATUS;

		if (v & CNT_CTL_ENABLE)
			v |= vtimer->cnt_ctl & CNT_CTL_ISTATUS;
		vtimer->cnt_ctl = v;

		if ((vtimer->cnt_ctl & CNT_CTL_ENABLE) && (vtimer->cnt_cval != 0)) {
			mod_timer(&vtimer->timer, vtimer->cnt_cval + c->offset);
		} else {
			del_timer(&vtimer->timer);
		}
	}
}

static void vtimer_handle_cntp_tval(gp_regs *regs,
		int access, int read, unsigned long *value)
{
	struct vtimer *vtimer;
	unsigned long now;
	unsigned long ticks;
	struct vtimer_context *c = (struct vtimer_context *)
		get_vmodule_data_by_id(current_vcpu, vtimer_vmodule_id);

	get_access_vtimer(vtimer, c, access);
	now = NOW() - c->offset;

	if (read) {
		ticks = ns_to_ticks(vtimer->cnt_cval - now) & 0xffffffff;
		*value = ticks;
	} else {
		unsigned long v = *value;

		vtimer->cnt_cval = now + ticks_to_ns(v);
		if (vtimer->cnt_ctl & CNT_CTL_ENABLE) {
			vtimer->cnt_ctl &= ~CNT_CTL_ISTATUS;
			mod_timer(&vtimer->timer, vtimer->cnt_cval + c->offset);
		}
	}
}

static void vtimer_handle_cntp_cval(gp_regs *regs,
		int access, int read, unsigned long *value)
{
	struct vtimer *vtimer;
	struct vtimer_context *c = (struct vtimer_context *)
		get_vmodule_data_by_id(current_vcpu, vtimer_vmodule_id);

	get_access_vtimer(vtimer, c, access);

	if (read) {
		*value = ns_to_ticks(vtimer->cnt_cval);
	} else {
		vtimer->cnt_cval = ticks_to_ns(*value);
		if (vtimer->cnt_ctl & CNT_CTL_ENABLE) {
			vtimer->cnt_ctl &= ~CNT_CTL_ISTATUS;
			mod_timer(&vtimer->timer, vtimer->cnt_cval + c->offset);
		}
	}
}

int vtimer_sysreg_simulation(gp_regs *regs, uint32_t esr_value)
{
	struct esr_sysreg *sysreg = (struct esr_sysreg *)&esr_value;
	uint32_t reg = esr_value & ESR_SYSREG_REGS_MASK;
	unsigned long value = 0;
	int read = sysreg->read;

	if (!read)
		value = get_reg_value(regs, sysreg->reg);

	switch (reg) {
	case ESR_SYSREG_CNTP_CTL_EL0:
		vtimer_handle_cntp_ctl(regs, ACCESS_REG, read, &value);
		break;
	case ESR_SYSREG_CNTP_CVAL_EL0:
		vtimer_handle_cntp_cval(regs, ACCESS_REG, read, &value);
		break;
	case ESR_SYSREG_CNTP_TVAL_EL0:
		vtimer_handle_cntp_tval(regs, ACCESS_REG, read, &value);
		break;
	default:
		break;
	}

	if (read)
		set_reg_value(regs, sysreg->reg, value);

	return 0;
}

static inline int vtimer_phy_mem_handler(gp_regs *regs, int read,
		unsigned long address, unsigned long *value)
{
	unsigned long offset = address - 0x2a830000;

	switch (offset) {
	case CNTP_CTL:
		vtimer_handle_cntp_ctl(regs, ACCESS_MEM, read, value);
		break;
	case CNTP_CVAL:
		vtimer_handle_cntp_cval(regs, ACCESS_MEM, read, value);
		break;
	case CNTP_TVAL:
		vtimer_handle_cntp_tval(regs, ACCESS_MEM, read, value);
		break;
	default:
		break;
	}

	return 0;
}

static int vtimer_vmodule_init(struct vmodule *vmodule)
{
	vmodule->context_size = sizeof(struct vtimer_context);
	vmodule->pdata = NULL;
	vmodule->state_init = vtimer_state_init;
	vmodule->state_save = vtimer_state_save;
	vmodule->state_restore = vtimer_state_restore;
	vmodule->state_deinit = vtimer_state_deinit;
	vmodule->state_reset = vtimer_state_deinit;
	vtimer_vmodule_id = vmodule->id;

	return 0;
}

MINOS_MODULE_DECLARE(armv8_vtimer, "armv8-vtimer",
		(void *)vtimer_vmodule_init);
