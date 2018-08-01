/*
 * Copyright (c) 2009 Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   3. Neither the name of the copyright holder nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef HAX_CORE_VCPU_H_
#define HAX_CORE_VCPU_H_

#include "emulate.h"
#include "vmx.h"
#include "mtrr.h"
#include "vm.h"
#include "pmu.h"
#include "../../include/hax_interface.h"
#include "config.h"

#define NR_GMSR     5
#define NR_EMT64MSR 6

struct gstate {
    struct vmx_msr gmsr[NR_GMSR];
    // IA32_PMCx, since APM v1
    uint64 apm_pmc_msrs[APM_MAX_GENERAL_COUNT];
    // IA32_PERFEVTSELx, since APM v1
    uint64 apm_pes_msrs[APM_MAX_GENERAL_COUNT];
    // IA32_TSC_AUX
    uint64 tsc_aux;
    struct hax_page *gfxpage;
    // APIC_BASE MSR
    uint64 apic_base;
};

struct cvtlb {
    vaddr_t va;
    paddr_t ha;
    uint64 flags;
    uint guest_order;
    uint order;
    uint access;
    uint flag;
};

struct hax_mmu;
struct per_cpu_data;

struct vcpu_vmx_data {
    uint32 pin_ctls_base;
    uint32 pcpu_ctls_base;
    uint32 scpu_ctls_base;
    uint32 entry_ctls_base;
    uint32 exc_bitmap_base;
    uint32 exit_ctls_base;

    uint32 pin_ctls;
    uint32 pcpu_ctls;
    uint32 scpu_ctls;
    uint32 entry_ctls;
    uint32 exc_bitmap;
    uint32 exit_ctls;

    uint64 cr0_mask, cr0_shadow;
    uint64 cr4_mask, cr4_shadow;
    uint32 entry_exception_vector;
    uint32 entry_exception_error_code;

    uint32 exit_exception_error_code;
    interruption_info_t exit_intr_info;
    interruption_info_t entry_intr_info;
    uint32 exit_idt_vectoring;
    uint32 exit_instr_length;
    uint32 entry_instr_length;

    exit_reason_t exit_reason;
    exit_qualification_t exit_qualification;
    interruptibility_state_t interruptibility_state;
};

/* Information saved by instruction decoder and used by post-MMIO handler */
struct vcpu_post_mmio {
    enum {
        /* No-op (i.e. no need for any post-MMIO handling) */
        VCPU_POST_MMIO_NOOP,
        /* Writing to a register */
        VCPU_POST_MMIO_WRITE_REG,
        /* Writing to a memory location (GVA) */
        VCPU_POST_MMIO_WRITE_MEM
    } op;
    union {
        /* Index to the register to write to (for VCPU_POST_MMIO_WRITE_REG) */
        uint8 reg_index;
        /* GVA to write to (for VCPU_POST_MMIO_WRITE_MEM) */
        vaddr_t va;
    };
    /* How to manipulate hax_fastmmio.value before use by |op| */
    enum {
        /* No manipulation (i.e. use hax_fastmmio.value as is) */
        VCPU_POST_MMIO_MANIP_NONE,
        /* Bitwise AND */
        VCPU_POST_MMIO_MANIP_AND,
        /* Bitwise OR */
        VCPU_POST_MMIO_MANIP_OR,
        /* Bitwise XOR */
        VCPU_POST_MMIO_MANIP_XOR
    } manip;
    /* Another value (besides hax_fastmmio.value) for use by |manip| */
    uint64 value;
};

#ifdef CONFIG_HAX_EPT2
struct mmio_fetch_cache {
    uint64 last_gva;
    uint64 last_guest_cr3;
    void *kva;
    hax_kmap_user kmap;
    int hit_count;
};
#endif  // CONFIG_HAX_EPT2

#define IOS_MAX_BUFFER 64

struct vcpu_t {
    uint16 vcpu_id;
    uint16 cpu_id;
    /*
     * VPID: Virtual Processor Identifier
     * VPIDs provide a way for software to identify to the processor
     * the address spaces for different "virtual processors"
     * The processor may use this identification to maintain concurrently
     * information for multiple address spaces in its TLBs and paging-structure
     * caches, even when non-zero PCIDs are not being used.
     * Reference: SDM, Volume 3, Chapter 4.11.2 & Chapter 28.1.
     */
    uint16 vpid;
    uint32 launched;
    /*
     * This one should co-exist with the is_running and paused,
     * but considering this needs atomic, don't trouble to clean it now
     */
#define VCPU_STATE_FLAGS_OPENED 0x1
    uint64 flags;
    hax_atomic_t ref_count;
    hax_list_head vcpu_list;
    hax_mutex tmutex;

    struct vm_t *vm;
    struct hax_mmu *mmu;
    struct vcpu_state_t *state;
    struct hax_tunnel *tunnel;
    uint8 *io_buf;
    struct hax_page *vmcs_page;
    void *vcpu_host;
    struct {
        uint64 paused                          : 1;
        uint64 panicked                        : 1;
        uint64 is_running                      : 1;
        uint64 is_vmcs_loaded                  : 1;
        uint64 event_injected                  : 1;
        /* vcpu->state is valid or not */
#define GS_STALE      0
#define GS_VALID      1
        uint64 cur_state                       : 1;
        uint64 vmcs_pending                    : 1;
        uint64 vmcs_pending_entry_error_code   : 1;
        uint64 vmcs_pending_entry_instr_length : 1;
        uint64 vmcs_pending_entry_intr_info    : 1;
        uint64 vmcs_pending_guest_cr3          : 1;
        uint64 padding                         : 53;
    };

    /* For TSC offseting feature*/
    int64 tsc_offset;

    /* vmx control and states */
    struct vcpu_vmx_data vmx;

    /* Registers to store guest view of MTRR's */
    struct mtrr_t mtrr_initial_state;
    struct mtrr_t mtrr_current_state;

    /* These GPAs will be loaded into VMCS fields PDPTE{0..3} when EPT is
     * enabled and the vCPU is about to enter PAE paging mode. */
    uint64 pae_pdptes[4];

    uint64 cr_pat;
    uint64 cpuid_features_flag_mask;

    /* Interrupt stuff */
    uint32 intr_pending[8];
    uint32 nr_pending_intrs;

    struct gstate gstate;
    struct hax_vcpu_mem *tunnel_vcpumem;
    struct hax_vcpu_mem *iobuf_vcpumem;
    struct cvtlb prefetch[16];

    struct em_context_t emulate_ctxt;
    struct vcpu_post_mmio post_mmio;
#ifdef CONFIG_HAX_EPT2
    struct mmio_fetch_cache mmio_fetch;
#endif  // CONFIG_HAX_EPT2
};

#define vmx(v, field) v->vmx.field

struct vcpu_t *vcpu_create(struct vm_t *vm, void *vm_host, int vcpu_id);
int vcpu_execute(struct vcpu_t *vcpu);
void vcpu_load_host_state(struct vcpu_t *vcpu);
void vcpu_save_host_state(struct vcpu_t *vcpu);
void load_guest_msr(struct vcpu_t *vcpu);
void save_guest_msr(struct vcpu_t *vcpu);

int vtlb_active(struct vcpu_t *vcpu);
int vcpu_vmexit_handler(struct vcpu_t *vcpu, exit_reason_t exit_reason,
                        struct hax_tunnel *htun);
void vcpu_vmread_all(struct vcpu_t *vcpu);
void vcpu_vmwrite_all(struct vcpu_t *vcpu, int force_vtlb_flush);

int vcpu_teardown(struct vcpu_t *vcpu);

int vcpu_get_regs(struct vcpu_t *vcpu, struct vcpu_state_t *state);
int vcpu_put_regs(struct vcpu_t *vcpu, struct vcpu_state_t *state);
int vcpu_get_fpu(struct vcpu_t *vcpu, struct fx_layout *fl);
int vcpu_put_fpu(struct vcpu_t *vcpu, struct fx_layout *fl);
int vcpu_get_msr(struct vcpu_t *vcpu, uint64 entry, uint64 *val);
int vcpu_put_msr(struct vcpu_t *vcpu, uint64 entry, uint64 val);

/* The declaration for OS wrapper code */
int hax_vcpu_destroy_host(struct vcpu_t *cvcpu, void *vcpu_host);
int hax_vcpu_create_host(struct vcpu_t *cvcpu, void *vm_host, int vm_id,
                         int vcpu_id);

int hax_vm_destroy_host(struct vm_t *vm, void *vm_host);
int hax_vm_create_host(struct vm_t *cvm, int vm_id);

int vcpu_pause(struct vcpu_t *vcpu);
int vcpu_unpause(struct vcpu_t *vcpu);
int vcpu_takeoff(struct vcpu_t *vcpu);

void *vcpu_vmcs_va(struct vcpu_t *vcpu);
paddr_t vcpu_vmcs_pa(struct vcpu_t *vcpu);
int set_vcpu_tunnel(struct vcpu_t *vcpu, struct hax_tunnel *tunnel,
                    uint8 *iobuf);

static inline bool valid_vcpu_id(int vcpu_id)
{
    if ((vcpu_id >= 0) && (vcpu_id < HAX_MAX_VCPUS))
        return true;
    return false;
}

bool vcpu_is_panic(struct vcpu_t *vcpu);
#ifndef hax_panic_vcpu
void hax_panic_vcpu(struct vcpu_t *v, char *fmt, ...);
#endif

#endif  // HAX_CORE_VCPU_H_
