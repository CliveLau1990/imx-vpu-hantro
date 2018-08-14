/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
--                                                                            --
-- This software is confidential and proprietary and may be used only as      --
--   expressly authorized by VeriSilicon in a written licensing agreement.    --
--                                                                            --
--         This entire notice must be reproduced on all copies                --
--                       and may not be removed.                              --
--                                                                            --
--------------------------------------------------------------------------------
-- Redistribution and use in source and binary forms, with or without         --
-- modification, are permitted provided that the following conditions are met:--
--   * Redistributions of source code must retain the above copyright notice, --
--       this list of conditions and the following disclaimer.                --
--   * Redistributions in binary form must reproduce the above copyright      --
--       notice, this list of conditions and the following disclaimer in the  --
--       documentation and/or other materials provided with the distribution. --
--   * Neither the names of Google nor the names of its contributors may be   --
--       used to endorse or promote products derived from this software       --
--       without specific prior written permission.                           --
--------------------------------------------------------------------------------
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"--
-- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE  --
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE --
-- ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE  --
-- LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR        --
-- CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF       --
-- SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   --
-- INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN    --
-- CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)    --
-- ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE --
-- POSSIBILITY OF SUCH DAMAGE.                                                --
--------------------------------------------------------------------------------
------------------------------------------------------------------------------*/

#include "hantrodec.h"
#include "../dwl/dwl_defs.h"

#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/wait.h>

/* Address translation from CPU bus address to PCI bus address. */
/* TODO(mheikkinen) Now set separately in memalloc and kernel driver,
 * should this be set in a single place. */
#define HLINA_TRANSL_BASE               0x0

/* TODO(mheikkinen) These are the Xilinx defaults. */
#define PCI_VENDOR_ID_HANTRO            0x10ee
#define PCI_DEVICE_ID_HANTRO_PCI       0x7011
/* Base address got control register */
//#define PCI_CONTROL_BAR                1
#define PCI_CONTROL_BAR                0

/* PCIe hantro driver offse in control register */
//#define HANTRO_REG_OFFSET               0x1000
#define HANTRO_REG_OFFSET               0x0
/* Base address of PCI base address translation */
#define HLINA_ADDR_TRANSL_REG            0x20c/4

#define HXDEC_MAX_CORES                 1

#define HANTRO_DEC_ORG_REGS             60
#define HANTRO_PP_ORG_REGS              41

#define HANTRO_DEC_EXT_REGS             27
#define HANTRO_PP_EXT_REGS              9

#define HANTRO_DEC_TOTAL_REGS           (HANTRO_DEC_ORG_REGS + HANTRO_DEC_EXT_REGS)
#define HANTRO_PP_TOTAL_REGS            (HANTRO_PP_ORG_REGS + HANTRO_PP_EXT_REGS)
#define HANTRO_TOTAL_REGS               155

#define HANTRO_DEC_ORG_FIRST_REG        0
#define HANTRO_DEC_ORG_LAST_REG         59
#define HANTRO_DEC_EXT_FIRST_REG        119
#define HANTRO_DEC_EXT_LAST_REG         145

#define HANTRO_PP_ORG_FIRST_REG         60
#define HANTRO_PP_ORG_LAST_REG          100
#define HANTRO_PP_EXT_FIRST_REG         146
#define HANTRO_PP_EXT_LAST_REG          154

#define VP_PB_INT_LT                    30
#define SOCLE_INT                       36

#define HXDEC_NO_IRQ                    -1

/* module defaults */
#ifdef USE_64BIT_ENV
#define DEC_IO_SIZE             ((HANTRO_TOTAL_REGS) * 4) /* bytes */
#else
#define DEC_IO_SIZE             ((HANTRO_DEC_ORG_REGS + HANTRO_PP_ORG_REGS) * 4) /* bytes */
#endif
#define DEC_IRQ                 HXDEC_NO_IRQ

static const int DecHwId[] = {
  0x8190,
  0x8170,
  0x9170,
  0x9190,
  0x6731
};

unsigned long base_port = -1;

unsigned long multicorebase[HXDEC_MAX_CORES] = {
  -1,
};

int irq = DEC_IRQ;
int elements = 0;

/* module_param(name, type, perm) */
module_param(base_port, ulong, 0);
module_param(irq, int, 0);
module_param_array(multicorebase, ulong, &elements, 0);

static int hantrodec_major = 0; /* dynamic allocation */

/* here's all the must remember stuff */
typedef struct {
  char *buffer;
  unsigned int iosize;
  volatile u8 *hwregs[HXDEC_MAX_CORES];
  int irq;
  int cores;
  struct fasync_struct *async_queue_dec;
  struct fasync_struct *async_queue_pp;
} hantrodec_t;

static hantrodec_t hantrodec_data; /* dynamic allocation? */

static int ReserveIO(void);
static void ReleaseIO(void);

/* PCIe resources */
/* TODO(mheikkinen) Implement multicore support. */

struct pci_dev *g_dev = NULL;    /* PCI device structure. */

unsigned long g_base_hdwr;        /* PCI base register address (Hardware address) */
u32 g_base_len;                   /* Base register address Length */
void *g_base_virt = NULL;         /* Base register virtual address */
unsigned long g_hantro_reg_base = 0;/* Base register for Hantro IP */
u32 *g_hantro_reg_virt = NULL;     /* Virtual register for Hantro IP */

static int PcieInit(void);
static void ResetAsic(hantrodec_t * dev);

#ifdef HANTRODEC_DEBUG
static void dump_regs(hantrodec_t *dev);
#endif

/* IRQ handler */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
static irqreturn_t hantrodec_isr(int irq, void *dev_id, struct pt_regs *regs);
#else
static irqreturn_t hantrodec_isr(int irq, void *dev_id);
#endif


static u32 dec_regs[HXDEC_MAX_CORES][DEC_IO_SIZE/4];
struct semaphore dec_core_sem;
struct semaphore pp_core_sem;

static int dec_irq = 0;
static int pp_irq = 0;

atomic_t irq_rx = ATOMIC_INIT(0);
atomic_t irq_tx = ATOMIC_INIT(0);

static struct file* dec_owner[HXDEC_MAX_CORES];
static struct file* pp_owner[HXDEC_MAX_CORES];

DEFINE_SPINLOCK(owner_lock);
//spinlock_t owner_lock = SPIN_LOCK_UNLOCKED;

DECLARE_WAIT_QUEUE_HEAD(dec_wait_queue);
DECLARE_WAIT_QUEUE_HEAD(pp_wait_queue);

DECLARE_WAIT_QUEUE_HEAD(hw_queue);

#define DWL_CLIENT_TYPE_H264_DEC         1U
#define DWL_CLIENT_TYPE_MPEG4_DEC        2U
#define DWL_CLIENT_TYPE_JPEG_DEC         3U
#define DWL_CLIENT_TYPE_PP               4U
#define DWL_CLIENT_TYPE_VC1_DEC          5U
#define DWL_CLIENT_TYPE_MPEG2_DEC        6U
#define DWL_CLIENT_TYPE_VP6_DEC          7U
#define DWL_CLIENT_TYPE_AVS_DEC          8U
#define DWL_CLIENT_TYPE_RV_DEC           9U
#define DWL_CLIENT_TYPE_VP8_DEC          10U

static u32 cfg[HXDEC_MAX_CORES];

static void ReadCoreConfig(hantrodec_t *dev) {
  int c;
  u32 reg, mask, tmp;

  memset(cfg, 0, sizeof(cfg));

  for(c = 0; c < dev->cores; c++) {
    /* Decoder configuration */
    reg = ioread32(dev->hwregs[c] + HANTRODEC_SYNTH_CFG * 4);

    tmp = (reg >> DWL_H264_E) & 0x3U;
    if(tmp) printk(KERN_INFO "hantrodec: Core[%d] has H264\n", c);
    cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC : 0;

    tmp = (reg >> DWL_JPEG_E) & 0x01U;
    if(tmp) printk(KERN_INFO "hantrodec: Core[%d] has JPEG\n", c);
    cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

    tmp = (reg >> DWL_MPEG4_E) & 0x3U;
    if(tmp) printk(KERN_INFO "hantrodec: Core[%d] has MPEG4\n", c);
    cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_MPEG4_DEC : 0;

    tmp = (reg >> DWL_VC1_E) & 0x3U;
    if(tmp) printk(KERN_INFO "hantrodec: Core[%d] has VC1\n", c);
    cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VC1_DEC: 0;

    tmp = (reg >> DWL_MPEG2_E) & 0x01U;
    if(tmp) printk(KERN_INFO "hantrodec: Core[%d] has MPEG2\n", c);
    cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_MPEG2_DEC : 0;

    tmp = (reg >> DWL_VP6_E) & 0x01U;
    if(tmp) printk(KERN_INFO "hantrodec: Core[%d] has VP6\n", c);
    cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP6_DEC : 0;

    reg = ioread32(dev->hwregs[c] + HANTRODEC_SYNTH_CFG_2 * 4);

    /* VP7 and WEBP is part of VP8 */
    mask =  (1 << DWL_VP8_E) | (1 << DWL_VP7_E) | (1 << DWL_WEBP_E);
    tmp = (reg & mask);
    if(tmp & (1 << DWL_VP8_E))
      printk(KERN_INFO "hantrodec: Core[%d] has VP8\n", c);
    if(tmp & (1 << DWL_VP7_E))
      printk(KERN_INFO "hantrodec: Core[%d] has VP7\n", c);
    if(tmp & (1 << DWL_WEBP_E))
      printk(KERN_INFO "hantrodec: Core[%d] has WebP\n", c);
    cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP8_DEC : 0;

    tmp = (reg >> DWL_AVS_E) & 0x01U;
    if(tmp) printk(KERN_INFO "hantrodec: Core[%d] has AVS\n", c);
    cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_AVS_DEC: 0;

    tmp = (reg >> DWL_RV_E) & 0x03U;
    if(tmp) printk(KERN_INFO "hantrodec: Core[%d] has RV\n", c);
    cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_RV_DEC : 0;

    /* Post-processor configuration */
    reg = ioread32(dev->hwregs[c] + HX170PP_SYNTH_CFG * 4);

    tmp = (reg >> DWL_PP_E) & 0x01U;
    if(tmp) printk(KERN_INFO "hantrodec: Core[%d] has PP\n", c);
    cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_PP : 0;
  }
}

static int CoreHasFormat(const u32 *cfg, int Core, u32 format) {
  return (cfg[Core] & (1 << format)) ? 1 : 0;
}

int GetDecCore(long Core, hantrodec_t *dev, struct file* filp) {
  int success = 0;
  unsigned long flags;

  spin_lock_irqsave(&owner_lock, flags);
  if(dec_owner[Core] == NULL ) {
    dec_owner[Core] = filp;
    success = 1;
  }

  spin_unlock_irqrestore(&owner_lock, flags);

  return success;
}

int GetDecCoreAny(long *Core, hantrodec_t *dev, struct file* filp,
                  unsigned long format) {
  int success = 0;
  long c;

  *Core = -1;

  for(c = 0; c < dev->cores; c++) {
    /* a free Core that has format */
    if(CoreHasFormat(cfg, c, format) && GetDecCore(c, dev, filp)) {
      success = 1;
      *Core = c;
      break;
    }
  }

  return success;
}

long ReserveDecoder(hantrodec_t *dev, struct file* filp, unsigned long format) {
  long Core = -1;

  /* reserve a Core */
  if (down_interruptible(&dec_core_sem))
    return -ERESTARTSYS;

  /* lock a Core that has specific format*/
  if(wait_event_interruptible(hw_queue,
                              GetDecCoreAny(&Core, dev, filp, format) != 0 ))
    return -ERESTARTSYS;

  return Core;
}

void ReleaseDecoder(hantrodec_t *dev, long Core) {
  u32 status;
  unsigned long flags;
  u32 counter = 0;


  status = ioread32(dev->hwregs[Core] + HX170_IRQ_STAT_DEC_OFF);

  /* make sure HW is disabled */
  if(status & HX170_DEC_E) {
    printk(KERN_INFO "hantrodec: DEC[%li] still enabled -> reset\n", Core);

    while(status & HX170_DEC_E) {
      printk(KERN_INFO "hantrodec: Killed, wait for HW finish\n", Core);
      status = ioread32(dev->hwregs[Core] + HX170_IRQ_STAT_DEC_OFF);
      if(++counter > 500000) {

        printk(KERN_INFO "hantrodec: Killed, timeout\n", Core);
        break;
      }
    }

    iowrite32(0, dev->hwregs[Core] + HX170_IRQ_STAT_DEC_OFF);

  }

  spin_lock_irqsave(&owner_lock, flags);

  dec_owner[Core] = NULL;

  spin_unlock_irqrestore(&owner_lock, flags);

  up(&dec_core_sem);

  wake_up_interruptible_all(&hw_queue);
}

long ReservePostProcessor(hantrodec_t *dev, struct file* filp) {
  unsigned long flags;

  long Core = 0;

  /* single Core PP only */
  if (down_interruptible(&pp_core_sem))
    return -ERESTARTSYS;

  spin_lock_irqsave(&owner_lock, flags);

  pp_owner[Core] = filp;

  spin_unlock_irqrestore(&owner_lock, flags);

  return Core;
}

void ReleasePostProcessor(hantrodec_t *dev, long Core) {
  unsigned long flags;

  u32 status = ioread32(dev->hwregs[Core] + HX170_IRQ_STAT_PP_OFF);

  /* make sure HW is disabled */
  if(status & HX170_PP_E) {
    printk(KERN_INFO "hantrodec: PP[%li] still enabled -> reset\n", Core);

    /* disable IRQ */
    status |= HX170_PP_IRQ_DISABLE;

    /* disable postprocessor */
    status &= (~HX170_PP_E);
    iowrite32(0x10, dev->hwregs[Core] + HX170_IRQ_STAT_PP_OFF);
  }

  spin_lock_irqsave(&owner_lock, flags);

  pp_owner[Core] = NULL;

  spin_unlock_irqrestore(&owner_lock, flags);

  up(&pp_core_sem);
}

long ReserveDecPp(hantrodec_t *dev, struct file* filp, unsigned long format) {
  /* reserve Core 0, DEC+PP for pipeline */
  unsigned long flags;

  long Core = 0;

  /* check that Core has the requested dec format */
  if(!CoreHasFormat(cfg, Core, format))
    return -EFAULT;

  /* check that Core has PP */
  if(!CoreHasFormat(cfg, Core, DWL_CLIENT_TYPE_PP))
    return -EFAULT;

  /* reserve a Core */
  if (down_interruptible(&dec_core_sem))
    return -ERESTARTSYS;

  /* wait until the Core is available */
  if(wait_event_interruptible(hw_queue,
                              GetDecCore(Core, dev, filp) != 0)) {
    up(&dec_core_sem);
    return -ERESTARTSYS;
  }


  if (down_interruptible(&pp_core_sem)) {
    ReleaseDecoder(dev, Core);
    return -ERESTARTSYS;
  }

  spin_lock_irqsave(&owner_lock, flags);
  pp_owner[Core] = filp;
  spin_unlock_irqrestore(&owner_lock, flags);

  return Core;
}

long DecFlushRegs(hantrodec_t *dev, struct core_desc *Core) {
  long ret = 0, i;

  u32 id = Core->id;

  /* copy original dec regs to kernal space*/
  ret = copy_from_user(dec_regs[id], Core->regs, HANTRO_DEC_ORG_REGS*4);
#ifdef USE_64BIT_ENV
  /* copy extended dec regs to kernal space*/
  ret = copy_from_user(dec_regs[id] + HANTRO_DEC_EXT_FIRST_REG,
                       Core->regs + HANTRO_DEC_EXT_FIRST_REG,
                       HANTRO_DEC_EXT_REGS*4);
#endif
  if (ret) {
    PDEBUG("copy_from_user failed, returned %li\n", ret);
    return -EFAULT;
  }

  /* write dec regs but the status reg[1] to hardware */
  /* both original and extended regs need to be written */
  for(i = 2; i <= HANTRO_DEC_ORG_LAST_REG; i++)
    iowrite32(dec_regs[id][i], dev->hwregs[id] + i*4);
#ifdef USE_64BIT_ENV
  for(i = HANTRO_DEC_EXT_FIRST_REG; i <= HANTRO_DEC_EXT_LAST_REG; i++)
    iowrite32(dec_regs[id][i], dev->hwregs[id] + i*4);
#endif
  /* write the status register, which may start the decoder */
  iowrite32(dec_regs[id][1], dev->hwregs[id] + 4);

  PDEBUG("flushed registers on Core %d\n", id);

  return 0;
}

long DecRefreshRegs(hantrodec_t *dev, struct core_desc *Core) {
  long ret, i;
  u32 id = Core->id;
#ifdef USE_64BIT_ENV
  /* user has to know exactly what they are asking for */
  if(Core->size != (HANTRO_DEC_TOTAL_REGS * 4))
    return -EFAULT;
#else
  /* user has to know exactly what they are asking for */
  if(Core->size != (HANTRO_DEC_ORG_REGS * 4))
    return -EFAULT;
#endif

  /* read all registers from hardware */
  /* both original and extended regs need to be read */
  for(i = 0; i <= HANTRO_DEC_ORG_LAST_REG; i++)
    dec_regs[id][i] = ioread32(dev->hwregs[id] + i*4);
#ifdef USE_64BIT_ENV
  for(i = HANTRO_DEC_EXT_FIRST_REG; i <= HANTRO_DEC_EXT_LAST_REG; i++)
    dec_regs[id][i] = ioread32(dev->hwregs[id] + i*4);
#endif
  /* put registers to user space*/
  /* put original registers to user space*/
  ret = copy_to_user(Core->regs, dec_regs[id], HANTRO_DEC_ORG_REGS*4);
#ifdef USE_64BIT_ENV
  /*put extended registers to user space*/
  ret = copy_to_user(Core->regs + HANTRO_DEC_EXT_FIRST_REG,
                     dec_regs[id] + HANTRO_DEC_EXT_FIRST_REG,
                     HANTRO_DEC_EXT_REGS * 4);
#endif
  if (ret) {
    PDEBUG("copy_to_user failed, returned %li\n", ret);
    return -EFAULT;
  }

  return 0;
}

static int CheckDecIrq(hantrodec_t *dev, int id) {
  unsigned long flags;
  int rdy = 0;

  const u32 irq_mask = (1 << id);

  spin_lock_irqsave(&owner_lock, flags);

  if(dec_irq & irq_mask) {
    /* reset the wait condition(s) */
    dec_irq &= ~irq_mask;
    rdy = 1;
  }

  spin_unlock_irqrestore(&owner_lock, flags);

  return rdy;
}

long WaitDecReadyAndRefreshRegs(hantrodec_t *dev, struct core_desc *Core) {
  u32 id = Core->id;

  PDEBUG("wait_event_interruptible DEC[%d]\n", id);

  if(wait_event_interruptible(dec_wait_queue, CheckDecIrq(dev, id))) {
    PDEBUG("DEC[%d]  wait_event_interruptible interrupted\n", id);
    return -ERESTARTSYS;
  }

  atomic_inc(&irq_tx);

  /* refresh registers */
  return DecRefreshRegs(dev, Core);
}

long PPFlushRegs(hantrodec_t *dev, struct core_desc *Core) {
  long ret = 0;
  u32 id = Core->id;
  u32 i;

  /* copy original dec regs to kernal space*/
  ret = copy_from_user(dec_regs[id] + HANTRO_PP_ORG_FIRST_REG,
                       Core->regs + HANTRO_PP_ORG_FIRST_REG,
                       HANTRO_PP_ORG_REGS*4);
#ifdef USE_64BIT_ENV
  /* copy extended dec regs to kernal space*/
  ret = copy_from_user(dec_regs[id] + HANTRO_PP_EXT_FIRST_REG,
                       Core->regs + HANTRO_PP_EXT_FIRST_REG,
                       HANTRO_PP_EXT_REGS*4);
#endif
  if (ret) {
    PDEBUG("copy_from_user failed, returned %li\n", ret);
    return -EFAULT;
  }

  /* write all regs but the status reg[1] to hardware */
  /* both original and extended regs need to be written */
  for(i = HANTRO_PP_ORG_FIRST_REG + 1; i <= HANTRO_PP_ORG_LAST_REG; i++)
    iowrite32(dec_regs[id][i], dev->hwregs[id] + i*4);
#ifdef USE_64BIT_ENV
  for(i = HANTRO_PP_EXT_FIRST_REG; i <= HANTRO_PP_EXT_LAST_REG; i++)
    iowrite32(dec_regs[id][i], dev->hwregs[id] + i*4);
#endif
  /* write the stat reg, which may start the PP */
  iowrite32(dec_regs[id][HANTRO_PP_ORG_FIRST_REG],
            dev->hwregs[id] + HANTRO_PP_ORG_FIRST_REG * 4);

  return 0;
}

long PPRefreshRegs(hantrodec_t *dev, struct core_desc *Core) {
  long i, ret;
  u32 id = Core->id;
#ifdef USE_64BIT_ENV
  /* user has to know exactly what they are asking for */
  if(Core->size != (HANTRO_PP_TOTAL_REGS * 4))
    return -EFAULT;
#else
  /* user has to know exactly what they are asking for */
  if(Core->size != (HANTRO_PP_ORG_REGS * 4))
    return -EFAULT;
#endif
  /* read all registers from hardware */
  /* both original and extended regs need to be read */
  for(i = HANTRO_PP_ORG_FIRST_REG; i <= HANTRO_PP_ORG_LAST_REG; i++)
    dec_regs[id][i] = ioread32(dev->hwregs[id] + i*4);
#ifdef USE_64BIT_ENV
  for(i = HANTRO_PP_EXT_FIRST_REG; i <= HANTRO_PP_EXT_LAST_REG; i++)
    dec_regs[id][i] = ioread32(dev->hwregs[id] + i*4);
#endif
  /* put registers to user space*/
  /* put original registers to user space*/
  ret = copy_to_user(Core->regs + HANTRO_PP_ORG_FIRST_REG,
                     dec_regs[id] + HANTRO_PP_ORG_FIRST_REG,
                     HANTRO_PP_ORG_REGS*4);
#ifdef USE_64BIT_ENV
  /* put extended registers to user space*/
  ret = copy_to_user(Core->regs + HANTRO_PP_EXT_FIRST_REG,
                     dec_regs[id] + HANTRO_PP_EXT_FIRST_REG,
                     HANTRO_PP_EXT_REGS * 4);
#endif
  if (ret) {
    PDEBUG("copy_to_user failed, returned %li\n", ret);
    return -EFAULT;
  }

  return 0;
}

static int CheckPPIrq(hantrodec_t *dev, int id) {
  unsigned long flags;
  int rdy = 0;

  const u32 irq_mask = (1 << id);

  spin_lock_irqsave(&owner_lock, flags);

  if(pp_irq & irq_mask) {
    /* reset the wait condition(s) */
    pp_irq &= ~irq_mask;
    rdy = 1;
  }

  spin_unlock_irqrestore(&owner_lock, flags);

  return rdy;
}

long WaitPPReadyAndRefreshRegs(hantrodec_t *dev, struct core_desc *Core) {
  u32 id = Core->id;

  PDEBUG("wait_event_interruptible PP[%d]\n", id);

  if(wait_event_interruptible(pp_wait_queue, CheckPPIrq(dev, id))) {
    PDEBUG("PP[%d]  wait_event_interruptible interrupted\n", id);
    return -ERESTARTSYS;
  }

  atomic_inc(&irq_tx);

  /* refresh registers */
  return PPRefreshRegs(dev, Core);
}

static int CheckCoreIrq(hantrodec_t *dev, const struct file *filp, int *id) {
  unsigned long flags;
  int rdy = 0, n = 0;

  do {
    u32 irq_mask = (1 << n);

    spin_lock_irqsave(&owner_lock, flags);

    if(dec_irq & irq_mask) {
      if (dec_owner[n] == filp) {
        /* we have an IRQ for our client */

        /* reset the wait condition(s) */
        dec_irq &= ~irq_mask;

        /* signal ready Core no. for our client */
        *id = n;

        rdy = 1;

        break;
      } else if(dec_owner[n] == NULL) {
        /* zombie IRQ */
        printk(KERN_INFO "IRQ on Core[%d], but no owner!!!\n", n);

        /* reset the wait condition(s) */
        dec_irq &= ~irq_mask;
      }
    }

    spin_unlock_irqrestore(&owner_lock, flags);

    n++; /* next Core */
  } while(n < dev->cores);

  return rdy;
}

long WaitCoreReady(hantrodec_t *dev, const struct file *filp, int *id) {
  PDEBUG("wait_event_interruptible CORE\n");

  if(wait_event_interruptible(dec_wait_queue, CheckCoreIrq(dev, filp, id))) {
    PDEBUG("CORE  wait_event_interruptible interrupted\n");
    return -ERESTARTSYS;
  }

  atomic_inc(&irq_tx);

  return 0;
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_ioctl
 Description     : communication method to/from the user space

 Return type     : long
------------------------------------------------------------------------------*/

static long hantrodec_ioctl(struct file *filp, unsigned int cmd,
                           unsigned long arg) {
  int err = 0;
  long tmp;

#ifdef HW_PERFORMANCE
  struct timeval *end_time_arg;
#endif

  PDEBUG("ioctl cmd 0x%08x\n", cmd);
  /*
   * extract the type and number bitfields, and don't decode
   * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
   */
  if (_IOC_TYPE(cmd) != HANTRODEC_IOC_MAGIC)
    return -ENOTTY;
  if (_IOC_NR(cmd) > HANTRODEC_IOC_MAXNR)
    return -ENOTTY;

  /*
   * the direction is a bitmask, and VERIFY_WRITE catches R/W
   * transfers. `Type' is user-oriented, while
   * access_ok is kernel-oriented, so the concept of "read" and
   * "write" is reversed
   */
  if (_IOC_DIR(cmd) & _IOC_READ)
    err = !access_ok(VERIFY_WRITE, (void *) arg, _IOC_SIZE(cmd));
  else if (_IOC_DIR(cmd) & _IOC_WRITE)
    err = !access_ok(VERIFY_READ, (void *) arg, _IOC_SIZE(cmd));

  if (err)
    return -EFAULT;

  switch (cmd) {
  case HANTRODEC_IOC_CLI:
    disable_irq(hantrodec_data.irq);
    break;
  case HANTRODEC_IOC_STI:
    enable_irq(hantrodec_data.irq);
    break;
  case HANTRODEC_IOCGHWOFFSET:
    __put_user(multicorebase[0], (unsigned long *) arg);
    break;
  case HANTRODEC_IOCGHWIOSIZE:
    __put_user(hantrodec_data.iosize, (unsigned int *) arg);
    break;
  case HANTRODEC_IOC_MC_OFFSETS: {
    tmp = copy_to_user((unsigned long *) arg, multicorebase, sizeof(multicorebase));
    if (err) {
      PDEBUG("copy_to_user failed, returned %li\n", tmp);
      return -EFAULT;
    }
    break;
  }
  case HANTRODEC_IOC_MC_CORES:
    __put_user(hantrodec_data.cores, (unsigned int *) arg);
    break;
  case HANTRODEC_IOCS_DEC_PUSH_REG: {
    struct core_desc Core;

    /* get registers from user space*/
    tmp = copy_from_user(&Core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    DecFlushRegs(&hantrodec_data, &Core);
    break;
  }
  case HANTRODEC_IOCS_PP_PUSH_REG: {
    struct core_desc Core;

    /* get registers from user space*/
    tmp = copy_from_user(&Core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    PPFlushRegs(&hantrodec_data, &Core);
    break;
  }
  case HANTRODEC_IOCS_DEC_PULL_REG: {
    struct core_desc Core;

    /* get registers from user space*/
    tmp = copy_from_user(&Core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    return DecRefreshRegs(&hantrodec_data, &Core);
  }
  case HANTRODEC_IOCS_PP_PULL_REG: {
    struct core_desc Core;

    /* get registers from user space*/
    tmp = copy_from_user(&Core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    return PPRefreshRegs(&hantrodec_data, &Core);
  }
  case HANTRODEC_IOCH_DEC_RESERVE: {
    PDEBUG("Reserve DEC Core, format = %li\n", arg);
    return ReserveDecoder(&hantrodec_data, filp, arg);
  }
  case HANTRODEC_IOCT_DEC_RELEASE: {
    if(arg >= hantrodec_data.cores || dec_owner[arg] != filp) {
      PDEBUG("bogus DEC release, Core = %li\n", arg);
      return -EFAULT;
    }

    PDEBUG("Release DEC, Core = %li\n", arg);

    ReleaseDecoder(&hantrodec_data, arg);

    break;
  }
  case HANTRODEC_IOCQ_PP_RESERVE:
    return ReservePostProcessor(&hantrodec_data, filp);
  case HANTRODEC_IOCT_PP_RELEASE: {
    if(arg != 0 || pp_owner[arg] != filp) {
      PDEBUG("bogus PP release %li\n", arg);
      return -EFAULT;
    }

    ReleasePostProcessor(&hantrodec_data, arg);

    break;
  }
  case HANTRODEC_IOCX_DEC_WAIT: {
    struct core_desc Core;

    /* get registers from user space */
    tmp = copy_from_user(&Core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    return WaitDecReadyAndRefreshRegs(&hantrodec_data, &Core);
  }
  case HANTRODEC_IOCX_PP_WAIT: {
    struct core_desc Core;

    /* get registers from user space */
    tmp = copy_from_user(&Core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    return WaitPPReadyAndRefreshRegs(&hantrodec_data, &Core);
  }
  case HANTRODEC_IOCG_CORE_WAIT: {
    int id;
    tmp = WaitCoreReady(&hantrodec_data, filp, &id);
    __put_user(id, (int *) arg);
    return tmp;
  }
  case HANTRODEC_IOX_ASIC_ID: {
    u32 id;
    __get_user(id, (u32*)arg);

    if(id >= hantrodec_data.cores) {
      return -EFAULT;
    }
    id = ioread32(hantrodec_data.hwregs[id]);
    __put_user(id, (u32 *) arg);
  }
  case HANTRODEC_DEBUG_STATUS: {
    printk(KERN_INFO "hantrodec: dec_irq     = 0x%08x \n", dec_irq);
    printk(KERN_INFO "hantrodec: pp_irq      = 0x%08x \n", pp_irq);

    printk(KERN_INFO "hantrodec: IRQs received/sent2user = %d / %d \n",
           atomic_read(&irq_rx), atomic_read(&irq_tx));

    for (tmp = 0; tmp < hantrodec_data.cores; tmp++) {
      printk(KERN_INFO "hantrodec: dec_core[%li] %s\n",
             tmp, dec_owner[tmp] == NULL ? "FREE" : "RESERVED");
      printk(KERN_INFO "hantrodec: pp_core[%li]  %s\n",
             tmp, pp_owner[tmp] == NULL ? "FREE" : "RESERVED");
    }
  }
  default:
    return -ENOTTY;
  }

  return 0;
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_open
 Description     : open method

 Return type     : int
------------------------------------------------------------------------------*/

static int hantrodec_open(struct inode *inode, struct file *filp) {
  PDEBUG("dev opened\n");
  return 0;
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_release
 Description     : Release driver

 Return type     : int
------------------------------------------------------------------------------*/

static int hantrodec_release(struct inode *inode, struct file *filp) {
  int n;
  hantrodec_t *dev = &hantrodec_data;

  PDEBUG("closing ...\n");

  for(n = 0; n < dev->cores; n++) {
    if(dec_owner[n] == filp) {
      PDEBUG("releasing dec Core %i lock\n", n);
      ReleaseDecoder(dev, n);
    }
  }

  for(n = 0; n < 1; n++) {
    if(pp_owner[n] == filp) {
      PDEBUG("releasing pp Core %i lock\n", n);
      ReleasePostProcessor(dev, n);
    }
  }

  PDEBUG("closed\n");
  return 0;
}

/* VFS methods */
static struct file_operations hantrodec_fops = {
  .owner = THIS_MODULE,
  .open = hantrodec_open,
  .release = hantrodec_release,
  .unlocked_ioctl = hantrodec_ioctl,
  .fasync = NULL
};

/*------------------------------------------------------------------------------
 Function name   : hantrodec_init
 Description     : Initialize the driver

 Return type     : int
------------------------------------------------------------------------------*/

int __init hantrodec_init(void) {
  int result, i;

  PDEBUG("module init\n");

  printk(KERN_INFO "hantrodec: dec/pp kernel module. \n");

  result = PcieInit();
  if(result)
    goto err;
  multicorebase[0] = g_hantro_reg_base;
  elements = 1;
  printk(KERN_INFO "hantrodec: Init single Core at 0x%16lx IRQ=%i\n",
         multicorebase[0], irq);

  hantrodec_data.iosize = DEC_IO_SIZE;
  hantrodec_data.irq = irq;

  for(i=0; i< HXDEC_MAX_CORES; i++) {
    hantrodec_data.hwregs[i] = 0;
    /* If user gave less Core bases that we have by default,
     * invalidate default bases
     */
    if(elements && i>=elements) {
      multicorebase[i] = -1;
    }
  }

  hantrodec_data.async_queue_dec = NULL;
  hantrodec_data.async_queue_pp = NULL;

  result = register_chrdev(hantrodec_major, "hantrodec", &hantrodec_fops);
  if(result < 0) {
    printk(KERN_INFO "hantrodec: unable to get major %d\n", hantrodec_major);
    goto err;
  } else if(result != 0) { /* this is for dynamic major */
    hantrodec_major = result;
  }

  result = ReserveIO();
  if(result < 0) {
    goto err;
  }

  memset(dec_owner, 0, sizeof(dec_owner));
  memset(pp_owner, 0, sizeof(pp_owner));

  sema_init(&dec_core_sem, hantrodec_data.cores);
  sema_init(&pp_core_sem, 1);

  /* read configuration fo all cores */
  ReadCoreConfig(&hantrodec_data);

  /* reset hardware */
  ResetAsic(&hantrodec_data);

  /* get the IRQ line */
  if(irq > 0) {
    result = request_irq(irq, hantrodec_isr,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))
                         SA_INTERRUPT | SA_SHIRQ,
#else
                         IRQF_SHARED,
#endif
                         "hantrodec", (void *) &hantrodec_data);
    if(result != 0) {
      if(result == -EINVAL) {
        printk(KERN_ERR "hantrodec: Bad irq number or handler\n");
      } else if(result == -EBUSY) {
        printk(KERN_ERR "hantrodec: IRQ <%d> busy, change your config\n",
               hantrodec_data.irq);
      }

      ReleaseIO();
      goto err;
    }
  } else {
    printk(KERN_INFO "hantrodec: IRQ not in use!\n");
  }

  printk(KERN_INFO "hantrodec: module inserted. Major = %d\n", hantrodec_major);

  return 0;

err:
  printk(KERN_INFO "hantrodec: module not inserted\n");
  unregister_chrdev(hantrodec_major, "hantrodec");
  return result;
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_cleanup
 Description     : clean up

 Return type     : int
------------------------------------------------------------------------------*/

void __exit hantrodec_cleanup(void) {
  hantrodec_t *dev = &hantrodec_data;

  /* reset hardware */
  ResetAsic(dev);

  /* free the IRQ */
  if(dev->irq != -1) {
    free_irq(dev->irq, (void *) dev);
  }

  printk(KERN_INFO "hantrodec: Release IO\n");
  ReleaseIO();

  unregister_chrdev(hantrodec_major, "hantrodec");

  printk(KERN_INFO "hantrodec: module removed\n");
  return;
}

/*------------------------------------------------------------------------------
 Function name   : PcieInit
 Description     : Initialize PCI Hw access

 Return type     : int
 ------------------------------------------------------------------------------*/

static int PcieInit(void) {
  g_dev = pci_get_device(PCI_VENDOR_ID_HANTRO, PCI_DEVICE_ID_HANTRO_PCI, g_dev);
  if (NULL == g_dev) {
    printk("Init: Hardware not found.\n");
    //return (CRIT_ERR);
    return (-1);
  }

  if (0 > pci_enable_device(g_dev)) {
    printk("Init: Device not enabled.\n");
    return (-1);
  }

  // Get Base Address of registers from pci structure. Should come from pci_dev
  // structure, but that element seems to be missing on the development system.
  g_base_hdwr = pci_resource_start (g_dev, PCI_CONTROL_BAR);
  if (0 > g_base_hdwr) {
    printk(KERN_INFO "Init: Base Address not set.\n");
    return (-1);
  }
  printk(KERN_INFO "Base hw val %l_x\n", (unsigned long)g_base_hdwr);

  g_base_len = pci_resource_len (g_dev, PCI_CONTROL_BAR);
  printk(KERN_INFO "Base hw len %d\n", (unsigned int)g_base_len);

  // Remap the I/O register block so that it can be safely accessed.

  g_base_virt = ioremap(g_base_hdwr, g_base_len);
  if (!g_base_virt) {
    printk(KERN_INFO "Init: Could not remap memory.\n");
    //return (CRIT_ERR);
    return (-1);
  }

  // Try to gain exclusive control of memory for demo hardware.
  if (0 > check_mem_region(g_base_hdwr, g_base_len)) {
    printk(KERN_INFO ": Init: Memory in use.\n");
    //return (CRIT_ERR);
    return (-1);
  }

  if (!request_mem_region(g_base_hdwr,g_base_len, "HantroDrv")) {
    printk(KERN_INFO "hantrodec: failed to reserve HW regs\n");
    return -1;
  }

  g_hantro_reg_base = g_base_hdwr + HANTRO_REG_OFFSET;
  g_hantro_reg_virt = (unsigned int *)g_base_virt + HANTRO_REG_OFFSET/4;
  printk(KERN_INFO "hantrodec: Hantro register bus %l_x virtual %l_x\n",
         (unsigned long)g_hantro_reg_base,g_hantro_reg_virt );

  ((unsigned int*)g_base_virt)[HLINA_ADDR_TRANSL_REG] = HLINA_TRANSL_BASE;
  printk("hantrodec: Address translation base for %x\n",
         (((unsigned int*)g_base_virt)[HLINA_ADDR_TRANSL_REG]));
  return 0;
}

/*------------------------------------------------------------------------------
 Function name   : CheckHwId
 Return type     : int
------------------------------------------------------------------------------*/
static int CheckHwId(hantrodec_t * dev) {
  long int hwid;
  int i;
  size_t num_hw = sizeof(DecHwId) / sizeof(*DecHwId);

  int found = 0;

  for (i = 0; i < dev->cores; i++) {
    if (dev->hwregs[i] != NULL ) {
      hwid = readl(dev->hwregs[i]);
      printk(KERN_INFO "hantrodec: Core %d HW ID=0x%08lx\n", i, hwid);
      hwid = (hwid >> 16) & 0xFFFF; /* product version only */

      while (num_hw--) {
        if (hwid == DecHwId[num_hw]) {
          printk(KERN_INFO "hantrodec: Supported HW found at 0x%16lx\n",
                 multicorebase[i]);
          found++;
          break;
        }
      }
      if (!found) {
        printk(KERN_INFO "hantrodec: Unknown HW found at 0x%16lx\n",
               multicorebase[i]);
        return 0;
      }
      found = 0;
      num_hw = sizeof(DecHwId) / sizeof(*DecHwId);
    }
  }

  return 1;
}

static int ReserveIO(void) {

  hantrodec_data.hwregs[0] = (volatile u8 *) g_hantro_reg_virt;

  if (hantrodec_data.hwregs[0] == NULL ) {
    printk(KERN_INFO "hantrodec: failed to ioremap HW regs\n");
    ReleaseIO();
    return -EBUSY;
  }

  hantrodec_data.cores = 1;
  /* check for correct HW */
  if (!CheckHwId(&hantrodec_data)) {
    ReleaseIO();
    return -EBUSY;
  }
  return 0;

}
/*------------------------------------------------------------------------------
 Function name   : releaseIO
 Description     : release

 Return type     : void
------------------------------------------------------------------------------*/

static void ReleaseIO(void) {

  if (g_base_virt != NULL)
    iounmap((void *) g_base_virt);
  release_mem_region( g_base_hdwr ,g_base_len);

}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_isr
 Description     : interrupt handler

 Return type     : irqreturn_t
------------------------------------------------------------------------------*/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
irqreturn_t hantrodec_isr(int irq, void *dev_id, struct pt_regs *regs)
#else
irqreturn_t hantrodec_isr(int irq, void *dev_id)
#endif
{
  unsigned long flags;
  unsigned int handled = 0;
  int i;
  volatile u8 *hwregs;

  hantrodec_t *dev = (hantrodec_t *) dev_id;
  u32 irq_status_dec;
  u32 irq_status_pp;

  spin_lock_irqsave(&owner_lock, flags);

  for(i=0; i<dev->cores; i++) {
    volatile u8 *hwregs = dev->hwregs[i];

    /* interrupt status register read */
    irq_status_dec = ioread32(hwregs + HX170_IRQ_STAT_DEC_OFF);

    if(irq_status_dec & HX170_DEC_IRQ) {
      /* clear dec IRQ */
      irq_status_dec &= (~HX170_DEC_IRQ);
      iowrite32(irq_status_dec, hwregs + HX170_IRQ_STAT_DEC_OFF);

      PDEBUG("decoder IRQ received! Core %d\n", i);

      atomic_inc(&irq_rx);

      dec_irq |= (1 << i);

      wake_up_interruptible_all(&dec_wait_queue);
      handled++;
    }
  }

  /* check PP also */
  hwregs = dev->hwregs[0];
  irq_status_pp = ioread32(hwregs + HX170_IRQ_STAT_PP_OFF);
  if(irq_status_pp & HX170_PP_IRQ) {
    /* clear pp IRQ */
    irq_status_pp &= (~HX170_PP_IRQ);
    iowrite32(irq_status_pp, hwregs + HX170_IRQ_STAT_PP_OFF);

    PDEBUG("post-processor IRQ received!\n");

    atomic_inc(&irq_rx);

    pp_irq |= 1;

    wake_up_interruptible_all(&pp_wait_queue);
    handled++;
  }

  spin_unlock_irqrestore(&owner_lock, flags);

  if(!handled) {
    PDEBUG("IRQ received, but not x170's!\n");
  }

  return IRQ_RETVAL(handled);
}

/*------------------------------------------------------------------------------
 Function name   : ResetAsic
 Description     : reset asic

 Return type     :
------------------------------------------------------------------------------*/
void ResetAsic(hantrodec_t * dev) {
  int i, j;
  u32 status;

  for (j = 0; j < dev->cores; j++) {
    status = ioread32(dev->hwregs[j] + HX170_IRQ_STAT_DEC_OFF);

    if( status & HX170_DEC_E) {
      /* abort with IRQ disabled */
      status = HX170_DEC_ABORT | HX170_DEC_IRQ_DISABLE;
      iowrite32(status, dev->hwregs[j] + HX170_IRQ_STAT_DEC_OFF);
    }

    /* reset PP */
    iowrite32(0, dev->hwregs[j] + HX170_IRQ_STAT_PP_OFF);

    for (i = 4; i < dev->iosize; i += 4) {
      iowrite32(0, dev->hwregs[j] + i);
    }
  }
}

/*------------------------------------------------------------------------------
 Function name   : dump_regs
 Description     : Dump registers

 Return type     :
------------------------------------------------------------------------------*/
#ifdef HANTRODEC_DEBUG
void dump_regs(hantrodec_t *dev) {
  int i,c;

  PDEBUG("Reg Dump Start\n");
  for(c = 0; c < dev->cores; c++) {
    for(i = 0; i < dev->iosize; i += 4*4) {
      PDEBUG("\toffset %04X: %08X  %08X  %08X  %08X\n", i,
             ioread32(dev->hwregs[c] + i),
             ioread32(dev->hwregs[c] + i + 4),
             ioread32(dev->hwregs[c] + i + 16),
             ioread32(dev->hwregs[c] + i + 24));
    }
  }
  PDEBUG("Reg Dump End\n");
}
#endif


module_init( hantrodec_init);
module_exit( hantrodec_cleanup);

/* module description */
MODULE_LICENSE("Proprietary");
MODULE_AUTHOR("Google Finland Oy");
MODULE_DESCRIPTION("Driver module for Hantro Decoder/Post-Processor");

