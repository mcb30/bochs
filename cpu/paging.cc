/////////////////////////////////////////////////////////////////////////
// $Id: paging.cc,v 1.224 2010/05/16 05:23:18 sshwarts Exp $
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2001-2010  The Bochs Project
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA B 02110-1301 USA
/////////////////////////////////////////////////////////////////////////

#define NEED_CPU_REG_SHORTCUTS 1
#include "bochs.h"
#include "cpu.h"
#define LOG_THIS BX_CPU_THIS_PTR

// X86 Registers Which Affect Paging:
// ==================================
//
// CR0:
//   bit 31: PG, Paging (386+)
//   bit 16: WP, Write Protect (486+)
//     0: allow   supervisor level writes into user level RO pages
//     1: inhibit supervisor level writes into user level RO pages
//
// CR3:
//   bit 31..12: PDBR, Page Directory Base Register (386+)
//   bit      4: PCD, Page level Cache Disable (486+)
//     Controls caching of current page directory.  Affects only the processor's
//     internal caches (L1 and L2).
//     This flag ignored if paging disabled (PG=0) or cache disabled (CD=1).
//     Values:
//       0: Page Directory can be cached
//       1: Page Directory not cached
//   bit      3: PWT, Page level Writes Transparent (486+)
//     Controls write-through or write-back caching policy of current page
//     directory.  Affects only the processor's internal caches (L1 and L2).
//     This flag ignored if paging disabled (PG=0) or cache disabled (CD=1).
//     Values:
//       0: write-back caching enabled
//       1: write-through caching enabled
//
// CR4:
//   bit 4: PSE, Page Size Extension (Pentium+)
//     0: 4KByte pages (typical)
//     1: 4MByte or 2MByte pages
//   bit 5: PAE, Physical Address Extension (Pentium Pro+)
//     0: 32bit physical addresses
//     1: 36bit physical addresses
//   bit 7: PGE, Page Global Enable (Pentium Pro+)
//     The global page feature allows frequently used or shared pages
//     to be marked as global (PDE or PTE bit 8).  Global pages are
//     not flushed from TLB on a task switch or write to CR3.
//     Values:
//       0: disables global page feature
//       1: enables global page feature
//
//    page size extention and physical address size extention matrix (legacy mode)
//   ==============================================================================
//   CR0.PG  CR4.PAE  CR4.PSE  PDPE.PS  PDE.PS | page size   physical address size
//   ==============================================================================
//      0       X        X       R         X   |   --          paging disabled
//      1       0        0       R         X   |   4K              32bits
//      1       0        1       R         0   |   4K              32bits
//      1       0        1       R         1   |   4M              32bits
//      1       1        X       R         0   |   4K              36bits
//      1       1        X       R         1   |   2M              36bits

//     page size extention and physical address size extention matrix (long mode)
//   ==============================================================================
//   CR0.PG  CR4.PAE  CR4.PSE  PDPE.PS  PDE.PS | page size   physical address size
//   ==============================================================================
//      1       1        X       0         0   |   4K              52bits
//      1       1        X       0         1   |   2M              52bits
//      1       1        X       1         -   |   1G              52bits


// Page Directory/Table Entry Fields Defined:
// ==========================================
// NX: No Execute
//   This bit controls the ability to execute code from all physical
//   pages mapped by the table entry.
//     0: Code can be executed from the mapped physical pages
//     1: Code cannot be executed
//   The NX bit can only be set when the no-execute page-protection
//   feature is enabled by setting EFER.NXE=1, If EFER.NXE=0, the
//   NX bit is treated as reserved. In this case, #PF occurs if the
//   NX bit is not cleared to zero.
//
// G: Global flag
//   Indiciates a global page when set.  When a page is marked
//   global and the PGE flag in CR4 is set, the page table or
//   directory entry for the page is not invalidated in the TLB
//   when CR3 is loaded or a task switch occurs.  Only software
//   clears and sets this flag.  For page directory entries that
//   point to page tables, this flag is ignored and the global
//   characteristics of a page are set in the page table entries.
//
// PS: Page Size flag
//   Only used in page directory entries.  When PS=0, the page
//   size is 4KBytes and the page directory entry points to a
//   page table.  When PS=1, the page size is 4MBytes for
//   normal 32-bit addressing and 2MBytes if extended physical
//   addressing.
//
// PAT: Page-Attribute Table
//   This bit is only present in the lowest level of the page
//   translation hierarchy. The PAT bit is the high-order bit
//   of a 3-bit index into the PAT register. The other two
//   bits involved in forming the index are the PCD and PWT
//   bits.
//
// D: Dirty bit:
//   Processor sets the Dirty bit in the 2nd-level page table before a
//   write operation to an address mapped by that page table entry.
//   Dirty bit in directory entries is undefined.
//
// A: Accessed bit:
//   Processor sets the Accessed bits in both levels of page tables before
//   a read/write operation to a page.
//
// PCD: Page level Cache Disable
//   Controls caching of individual pages or page tables.
//   This allows a per-page based mechanism to disable caching, for
//   those pages which contained memory mapped IO, or otherwise
//   should not be cached.  Processor ignores this flag if paging
//   is not used (CR0.PG=0) or the cache disable bit is set (CR0.CD=1).
//   Values:
//     0: page or page table can be cached
//     1: page or page table is not cached (prevented)
//
// PWT: Page level Write Through
//   Controls the write-through or write-back caching policy of individual
//   pages or page tables.  Processor ignores this flag if paging
//   is not used (CR0.PG=0) or the cache disable bit is set (CR0.CD=1).
//   Values:
//     0: write-back caching
//     1: write-through caching
//
// U/S: User/Supervisor level
//   0: Supervisor level - for the OS, drivers, etc.
//   1: User level - application code and data
//
// R/W: Read/Write access
//   0: read-only access
//   1: read/write access
//
// P: Present
//   0: Not present
//   1: Present
// ==========================================

// Combined page directory/page table protection:
// ==============================================
// There is one column for the combined effect on a 386
// and one column for the combined effect on a 486+ CPU.
//
// +----------------+-----------------+----------------+----------------+
// |  Page Directory|     Page Table  |   Combined 386 |  Combined 486+ |
// |Privilege  Type | Privilege  Type | Privilege  Type| Privilege  Type|
// |----------------+-----------------+----------------+----------------|
// |User       R    | User       R    | User       R   | User       R   |
// |User       R    | User       RW   | User       R   | User       R   |
// |User       RW   | User       R    | User       R   | User       R   |
// |User       RW   | User       RW   | User       RW  | User       RW  |
// |User       R    | Supervisor R    | User       R   | Supervisor RW  |
// |User       R    | Supervisor RW   | User       R   | Supervisor RW  |
// |User       RW   | Supervisor R    | User       R   | Supervisor RW  |
// |User       RW   | Supervisor RW   | User       RW  | Supervisor RW  |
// |Supervisor R    | User       R    | User       R   | Supervisor RW  |
// |Supervisor R    | User       RW   | User       R   | Supervisor RW  |
// |Supervisor RW   | User       R    | User       R   | Supervisor RW  |
// |Supervisor RW   | User       RW   | User       RW  | Supervisor RW  |
// |Supervisor R    | Supervisor R    | Supervisor RW  | Supervisor RW  |
// |Supervisor R    | Supervisor RW   | Supervisor RW  | Supervisor RW  |
// |Supervisor RW   | Supervisor R    | Supervisor RW  | Supervisor RW  |
// |Supervisor RW   | Supervisor RW   | Supervisor RW  | Supervisor RW  |
// +----------------+-----------------+----------------+----------------+

// Page Fault Error Code Format:
// =============================
//
// bits 31..4: Reserved
// bit  3: RSVD (Pentium Pro+)
//   0: fault caused by reserved bits set to 1 in a page directory
//      when the PSE or PAE flags in CR4 are set to 1
//   1: fault was not caused by reserved bit violation
// bit  2: U/S (386+)
//   0: fault originated when in supervior mode
//   1: fault originated when in user mode
// bit  1: R/W (386+)
//   0: access causing the fault was a read
//   1: access causing the fault was a write
// bit  0: P (386+)
//   0: fault caused by a nonpresent page
//   1: fault caused by a page level protection violation

// Some paging related notes:
// ==========================
//
// - When the processor is running in supervisor level, all pages are both
//   readable and writable (write-protect ignored).  When running at user
//   level, only pages which belong to the user level are accessible;
//   read/write & read-only are readable, read/write are writable.
//
// - If the Present bit is 0 in either level of page table, an
//   access which uses these entries will generate a page fault.
//
// - (A)ccess bit is used to report read or write access to a page
//   or 2nd level page table.
//
// - (D)irty bit is used to report write access to a page.
//
// - Processor running at CPL=0,1,2 maps to U/S=0
//   Processor running at CPL=3     maps to U/S=1

#if BX_SUPPORT_X86_64
  #define BX_INVALID_TLB_ENTRY BX_CONST64(0xffffffffffffffff)
#else
  #define BX_INVALID_TLB_ENTRY 0xffffffff
#endif

// bit [11] of the TLB lpf used for TLB_HostPtr valid indication
#define TLB_LPFOf(laddr) AlignedAccessLPFOf(laddr, 0x7ff)

#if BX_CPU_LEVEL >= 4
#  define BX_PRIV_CHECK_SIZE 32
#else
#  define BX_PRIV_CHECK_SIZE 16
#endif

// The 'priv_check' array is used to decide if the current access
// has the proper paging permissions.  An index is formed, based
// on parameters such as the access type and level, the write protect
// flag and values cached in the TLB.  The format of the index into this
// array is:
//
//   |4 |3 |2 |1 |0 |
//   |wp|us|us|rw|rw|
//    |  |  |  |  |
//    |  |  |  |  +---> r/w of current access
//    |  |  +--+------> u/s,r/w combined of page dir & table (cached)
//    |  +------------> u/s of current access
//    +---------------> Current CR0.WP value

/* 0xff0bbb0b */
static const Bit8u priv_check[BX_PRIV_CHECK_SIZE] =
{
  1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 1, 1,
#if BX_CPU_LEVEL >= 4
  1, 0, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 1, 0, 1, 1
#endif
};

#define BX_PAGING_PHY_ADDRESS_RESERVED_BITS \
    (BX_PHY_ADDRESS_RESERVED_BITS & BX_CONST64(0xfffffffffffff))

#define PAGE_DIRECTORY_NX_BIT (BX_CONST64(0x8000000000000000))

#define BX_CR3_PAGING_MASK    (BX_CONST64(0x000ffffffffff000))

#define BX_CR3_LEGACY_PAE_PAGING_MASK (0xffffffe0)

// Each entry in the TLB cache has 3 entries:
//
//   lpf:         Linear Page Frame (page aligned linear address of page)
//     bits 32..12  Linear page frame
//     bit  11      0: TLB HostPtr access allowed, 1: not allowed
//     bit  10...0  Invalidate index
//
//   ppf:         Physical Page Frame (page aligned phy address of page)
//
//   hostPageAddr:
//                Host Page Frame address used for direct access to
//                the mem.vector[] space allocated for the guest physical
//                memory.  If this is zero, it means that a pointer
//                to the host space could not be generated, likely because
//                that page of memory is not standard memory (it might
//                be memory mapped IO, ROM, etc).
//
//   accessBits:
//
//     bit  31:     Page is a global page.
//
//       The following bits are used for a very efficient permissions
//       check.  The goal is to be able, using only the current privilege
//       level and access type, to determine if the page tables allow the
//       access to occur or at least should rewalk the page tables.  On
//       the first read access, permissions are set to only read, so a
//       rewalk is necessary when a subsequent write fails the tests.
//       This allows for the dirty bit to be set properly, but for the
//       test to be efficient.  Note that the CR0.WP flag is not present.
//       The values in the following flags is based on the current CR0.WP
//       value, necessitating a TLB flush when CR0.WP changes.
//
//       The test is:
//         OK = (accessBits & ((E<<2) | (W<<1) | U)) <> 0
//
//       where E:1=Execute, 0=Data;
//             W:1=Write, 0=Read;
//             U:1=CPL3, 0=CPL0-2
//
//       Thus for reads, it is:
//         OK =       (          U )
//       for writes:
//         OK = 0x2 | (          U )
//       and for code fetches:
//         OK = 0x4 | (          U )
//
//       Note, that the TLB should have TLB_HostPtr bit set when direct
//       access through host pointer is NOT allowed for the page. A memory
//       operation asking for a direct access through host pointer will
//       set TLB_HostPtr bit in its lpf field and thus get TLB miss result 
//       when the direct access is not allowed.
//

#define TLB_HostPtr     (0x800) /* set this bit when direct access is NOT allowed */

#define TLB_GlobalPage  (0x80000000)

#define TLB_SysOnly     (0x1)
#define TLB_ReadOnly    (0x2)
#define TLB_NoExecute   (0x4)

// === TLB Instrumentation section ==============================

// Note: this is an approximation of what Peter Tattam had.

#define InstrumentTLB 0

#if InstrumentTLB
static unsigned tlbLookups=0;
static unsigned tlbMisses=0;
static unsigned tlbGlobalFlushes=0;
static unsigned tlbNonGlobalFlushes=0;

#define InstrTLB_StatsMask 0xfffff

#define InstrTLB_Stats() {\
  if ((tlbLookups & InstrTLB_StatsMask) == 0) { \
    BX_INFO(("TLB lookup:%8d miss:%8d %6.2f%% flush:%8d %6.2f%%", \
          tlbLookups, \
          tlbMisses, \
          tlbMisses * 100.0 / tlbLookups, \
          (tlbGlobalFlushes+tlbNonGlobalFlushes), \
          (tlbGlobalFlushes+tlbNonGlobalFlushes) * 100.0 / tlbLookups \
          )); \
    tlbLookups = tlbMisses = tlbGlobalFlushes = tlbNonGlobalFlushes = 0; \
    } \
  }
#define InstrTLB_Increment(v) (v)++

#else
#define InstrTLB_Stats()
#define InstrTLB_Increment(v)
#endif

// ==============================================================

void BX_CPU_C::TLB_flush(void)
{
#if InstrumentTLB
  InstrTLB_Increment(tlbGlobalFlushes);
#endif

  invalidate_prefetch_q();

  for (unsigned n=0; n<BX_TLB_SIZE; n++) {
    BX_CPU_THIS_PTR TLB.entry[n].lpf = BX_INVALID_TLB_ENTRY;
  }

#if BX_CPU_LEVEL >= 5
  BX_CPU_THIS_PTR TLB.split_large = 0;  // flush whole TLB
#endif

#if BX_SUPPORT_MONITOR_MWAIT
  // invalidating of the TLB might change translation for monitored page
  // and cause subsequent MWAIT instruction to wait forever
  BX_CPU_THIS_PTR monitor.reset_monitor();
#endif
}

#if BX_CPU_LEVEL >= 6
void BX_CPU_C::TLB_flushNonGlobal(void)
{
#if InstrumentTLB
  InstrTLB_Increment(tlbNonGlobalFlushes);
#endif

  invalidate_prefetch_q();

  BX_CPU_THIS_PTR TLB.split_large = 0;

  for (unsigned n=0; n<BX_TLB_SIZE; n++) {
    bx_TLB_entry *tlbEntry = &BX_CPU_THIS_PTR TLB.entry[n];
    if (!(tlbEntry->accessBits & TLB_GlobalPage)) {
      tlbEntry->lpf = BX_INVALID_TLB_ENTRY;
    }
    else {
      if (tlbEntry->lpf_mask > 0xfff)
        BX_CPU_THIS_PTR TLB.split_large = 1;
    }
  }

#if BX_SUPPORT_MONITOR_MWAIT
  // invalidating of the TLB might change translation for monitored page
  // and cause subsequent MWAIT instruction to wait forever
  BX_CPU_THIS_PTR monitor.reset_monitor();
#endif
}
#endif

void BX_CPU_C::TLB_invlpg(bx_address laddr)
{
  invalidate_prefetch_q();

  BX_DEBUG(("TLB_invlpg(0x"FMT_ADDRX"): invalidate TLB entry", laddr));

#if BX_CPU_LEVEL >= 5
  bx_bool large = 0;

  if (BX_CPU_THIS_PTR TLB.split_large) {
    // make sure INVLPG handles correctly large pages
    for (unsigned n=0; n<BX_TLB_SIZE; n++) {
      bx_TLB_entry *tlbEntry = &BX_CPU_THIS_PTR TLB.entry[n];
      bx_address lpf_mask = tlbEntry->lpf_mask;
      if ((laddr & ~lpf_mask) == (tlbEntry->lpf & ~lpf_mask)) {
        tlbEntry->lpf = BX_INVALID_TLB_ENTRY;
      }
      else {
        if (lpf_mask > 0xfff) large = 1;
      }
    }

    BX_CPU_THIS_PTR TLB.split_large = large;
  }
  else
#endif
  {
    unsigned TLB_index = BX_TLB_INDEX_OF(laddr, 0);
    bx_address lpf = LPFOf(laddr);
    bx_TLB_entry *tlbEntry = &BX_CPU_THIS_PTR TLB.entry[TLB_index];
    if (TLB_LPFOf(tlbEntry->lpf) == lpf) {
      tlbEntry->lpf = BX_INVALID_TLB_ENTRY;
    }
  }

#if BX_SUPPORT_MONITOR_MWAIT
  // invalidating of the TLB entry might change translation for monitored
  // page and cause subsequent MWAIT instruction to wait forever
  BX_CPU_THIS_PTR monitor.reset_monitor();
#endif
}

void BX_CPP_AttrRegparmN(1) BX_CPU_C::INVLPG(bxInstruction_c* i)
{
  if (!real_mode() && CPL!=0) {
    BX_ERROR(("INVLPG: priveledge check failed, generate #GP(0)"));
    exception(BX_GP_EXCEPTION, 0);
  }

  bx_address eaddr = BX_CPU_CALL_METHODR(i->ResolveModrm, (i));
  bx_address laddr = get_laddr(i->seg(), eaddr);

#if BX_SUPPORT_VMX
  VMexit_INVLPG(i, laddr);
#endif

#if BX_SUPPORT_X86_64
  if (! IsCanonical(laddr)) {
    BX_ERROR(("INVLPG: non-canonical access !"));
    exception(int_number(i->seg()), 0);
  }
#endif

  BX_INSTR_TLB_CNTRL(BX_CPU_ID, BX_INSTR_INVLPG, laddr);
  TLB_invlpg(laddr);
}

// error checking order - page not present, reserved bits, protection
#define ERROR_NOT_PRESENT       0x00
#define ERROR_PROTECTION        0x01
#define ERROR_RESERVED          0x08
#define ERROR_CODE_ACCESS       0x10

void BX_CPU_C::page_fault(unsigned fault, bx_address laddr, unsigned user, unsigned rw)
{
  unsigned error_code = fault;
  unsigned isWrite = rw & 1;

  error_code |= (user << 2) | (isWrite << 1);
#if BX_SUPPORT_X86_64
  if (BX_CPU_THIS_PTR cr4.get_PAE() && BX_CPU_THIS_PTR efer.get_NXE() && rw == BX_EXECUTE)
    error_code |= ERROR_CODE_ACCESS; // I/D = 1
#endif

#if BX_SUPPORT_VMX
  VMexit_Event(0, BX_HARDWARE_EXCEPTION, BX_PF_EXCEPTION, error_code, 1, laddr); // before the CR2 was modified
#endif

  BX_CPU_THIS_PTR cr2 = laddr;

#if BX_SUPPORT_X86_64
  BX_DEBUG(("page fault for address %08x%08x @ %08x%08x",
             GET32H(laddr), GET32L(laddr), GET32H(RIP), GET32L(RIP)));
#else
  BX_DEBUG(("page fault for address %08x @ %08x", laddr, EIP));
#endif

  exception(BX_PF_EXCEPTION, error_code);
}

#define BX_LEVEL_PML4 3
#define BX_LEVEL_PDPE 2
#define BX_LEVEL_PDE  1
#define BX_LEVEL_PTE  0

static const char *bx_paging_level[4] = { "PTE", "PDE", "PDPE", "PML4" };

#if BX_CPU_LEVEL >= 6

int BX_CPU_C::check_entry_PAE(const char *s, Bit64u entry, Bit64u reserved, unsigned rw, bx_bool *nx_fault)
{
  if (!(entry & 0x1)) {
    BX_DEBUG(("%s: entry not present", s));
    return ERROR_NOT_PRESENT;
  }

  if (entry & reserved) {
    BX_DEBUG(("%s: reserved bit is set %08x:%08x", s, GET32H(entry), GET32L(entry)));
    return ERROR_RESERVED | ERROR_PROTECTION;
  }

#if BX_SUPPORT_X86_64
  if (! long_mode()) {
    if (entry & BX_CONST64(0x7ff0000000000000)) {
      BX_DEBUG(("%s: reserved bit is set %08x:%08x", s, GET32H(entry), GET32L(entry)));
      return ERROR_RESERVED | ERROR_PROTECTION;
    }
  }

  if (entry & PAGE_DIRECTORY_NX_BIT) {
    if (! BX_CPU_THIS_PTR efer.get_NXE()) {
      BX_DEBUG(("%s: NX bit set when EFER.NXE is disabled", s));
      return ERROR_RESERVED | ERROR_PROTECTION;
    }
    if (rw == BX_EXECUTE) {
      BX_DEBUG(("%s: non-executable page fault occured", s));
      *nx_fault = 1;
    }
  }
#endif

  return -1;
}

//                Format of a Long Mode Non-Leaf Entry
// -----------------------------------------------------------
// 00    | Present (P)
// 01    | R/W
// 02    | U/S
// 03    | Page-Level Write-Through (PWT)
// 04    | Page-Level Cache-Disable (PCD)
// 05    | Accessed (A)
// 06    | (ignored)
// 07    | Page Size (PS), must be 0 if no Large Page on the level
// 11-08 | (ignored)
// PA-12 | Physical address of 4-KByte aligned page-directory-pointer table
// 51-PA | Reserved (must be zero)
// 62-52 | (ignored)
// 63    | Execute-Disable (XD) (if EFER.NXE=1, reserved otherwise)
// -----------------------------------------------------------

#define PAGING_PAE_RESERVED_BITS (BX_PAGING_PHY_ADDRESS_RESERVED_BITS)

//       Format of a PDPTE that References a 1-GByte Page
// -----------------------------------------------------------
// 00    | Present (P)
// 01    | R/W
// 02    | U/S
// 03    | Page-Level Write-Through (PWT)
// 04    | Page-Level Cache-Disable (PCD)
// 05    | Accessed (A)
// 06    | (ignored)
// 07    | Page Size, must be 1 to indicate a 1-GByte Page
// 08    | Global (G) (if CR4.PGE=1, ignored otherwise)
// 11-09 | (ignored)
// 12    | PAT (if PAT is supported, reserved otherwise)
// 29-13 | Reserved (must be zero)
// PA-30 | Physical address of the 1-Gbyte Page
// 51-PA | Reserved (must be zero)
// 62-52 | (ignored)
// 63    | Execute-Disable (XD) (if EFER.NXE=1, reserved otherwise)
// -----------------------------------------------------------

#define PAGING_PAE_PDPTE1G_RESERVED_BITS \
    (BX_PAGING_PHY_ADDRESS_RESERVED_BITS | BX_CONST64(0x3FFFE000))

//        Format of a PAE PDE that Maps a 2-MByte Page
// -----------------------------------------------------------
// 00    | Present (P)
// 01    | R/W
// 02    | U/S
// 03    | Page-Level Write-Through (PWT)
// 04    | Page-Level Cache-Disable (PCD)
// 05    | Accessed (A)
// 06    | Dirty (D)
// 07    | Page Size (PS), must be 1 to indicate a 2-MByte Page
// 08    | Global (G) (if CR4.PGE=1, ignored otherwise)
// 11-09 | (ignored)
// 12    | PAT (if PAT is supported, reserved otherwise)
// 20-13 | Reserved (must be zero)
// PA-21 | Physical address of the 2-MByte page
// 51-PA | Reserved (must be zero)
// 62-52 | ignored in long mode, reserved (must be 0) in legacy PAE mode
// 63    | Execute-Disable (XD) (if EFER.NXE=1, reserved otherwise)
// -----------------------------------------------------------

#define PAGING_PAE_PDE2M_RESERVED_BITS \
    (BX_PAGING_PHY_ADDRESS_RESERVED_BITS | BX_CONST64(0x001FE000))

//        Format of a PAE PTE that Maps a 4-KByte Page
// -----------------------------------------------------------
// 00    | Present (P)
// 01    | R/W
// 02    | U/S
// 03    | Page-Level Write-Through (PWT)
// 04    | Page-Level Cache-Disable (PCD)
// 05    | Accessed (A)
// 06    | Dirty (D)
// 07    | PAT (if PAT is supported, reserved otherwise)
// 08    | Global (G) (if CR4.PGE=1, ignored otherwise)
// 11-09 | (ignored)
// PA-12 | Physical address of the 4-KByte page
// 51-PA | Reserved (must be zero)
// 62-52 | ignored in long mode, reserved (must be 0) in legacy PAE mode
// 63    | Execute-Disable (XD) (if EFER.NXE=1, reserved otherwise)
// -----------------------------------------------------------

#if BX_SUPPORT_X86_64

// Translate a linear address to a physical address in long mode
bx_phy_address BX_CPU_C::translate_linear_long_mode(bx_address laddr, Bit32u &lpf_mask, Bit32u &combined_access, unsigned curr_pl, unsigned rw)
{
  bx_phy_address entry_addr[4];
  bx_phy_address ppf = BX_CPU_THIS_PTR cr3 & BX_CR3_PAGING_MASK;
  Bit64u entry[4];
  bx_bool nx_fault = 0;
  unsigned pl = (curr_pl == 3);
  int leaf = BX_LEVEL_PTE;
  combined_access = 0x06;

  for (leaf = BX_LEVEL_PML4;; --leaf) {
    entry_addr[leaf] = ppf + ((laddr >> (9 + 9*leaf)) & 0xff8);
#if BX_SUPPORT_VMX >= 2
    if (BX_CPU_THIS_PTR in_vmx_guest) {
      if (SECONDARY_VMEXEC_CONTROL(VMX_VM_EXEC_CTRL3_EPT_ENABLE))
        entry_addr[leaf] = translate_guest_physical(entry_addr[leaf], laddr, 1, 1, rw);
    }
#endif
    access_read_physical(entry_addr[leaf], 8, &entry[leaf]);
    BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, entry_addr[leaf], 8, (BX_PTE_ACCESS + (leaf<<4)) | BX_READ, (Bit8u*)(&entry[leaf]));

    Bit64u curr_entry = entry[leaf];
    int fault = check_entry_PAE(bx_paging_level[leaf], curr_entry, PAGING_PAE_RESERVED_BITS, rw, &nx_fault);
    if (fault >= 0)
      page_fault(fault, laddr, pl, rw);

    combined_access &= curr_entry & 0x06; // U/S and R/W
    ppf = curr_entry & BX_CONST64(0x000ffffffffff000);

    if (leaf == BX_LEVEL_PTE) break;

    if (curr_entry & 0x80) {
      if (leaf > (BX_LEVEL_PDE + bx_cpuid_support_1g_paging())) {
        BX_DEBUG(("%s: PS bit set !"));
        page_fault(ERROR_RESERVED | ERROR_PROTECTION, laddr, pl, rw);
      }

      if (leaf == BX_LEVEL_PDPE) {
        if (curr_entry & PAGING_PAE_PDPTE1G_RESERVED_BITS) {
           BX_DEBUG(("PAE PDPE1G: reserved bit is set: PDPE=%08x:%08x", GET32H(curr_entry), GET32L(curr_entry)));
           page_fault(ERROR_RESERVED | ERROR_PROTECTION, laddr, pl, rw);
        }

        // Make up the physical page frame address.
        ppf = (bx_phy_address)((curr_entry & BX_CONST64(0x000fffffc0000000)) | (laddr & 0x3ffff000));
        lpf_mask = 0x3fffffff;
        break;
      }

      if (leaf == BX_LEVEL_PDE) {
        if (curr_entry & PAGING_PAE_PDE2M_RESERVED_BITS) {
          BX_DEBUG(("PAE PDE2M: reserved bit is set PDE=%08x:%08x", GET32H(curr_entry), GET32L(curr_entry)));
          page_fault(ERROR_RESERVED | ERROR_PROTECTION, laddr, pl, rw);
        }

        // Make up the physical page frame address.
        ppf = (bx_phy_address)((curr_entry & BX_CONST64(0x000fffffffe00000)) | (laddr & 0x001ff000));
        lpf_mask = 0x1fffff;
        break;
      }
    }
  }

  bx_bool isWrite = (rw & 1); // write or r-m-w

  unsigned priv_index = (BX_CPU_THIS_PTR cr0.get_WP() << 4) | // bit 4
                        (pl<<3) |                             // bit 3
                        (combined_access | isWrite);          // bit 2,1,0

  if (!priv_check[priv_index] || nx_fault)
    page_fault(ERROR_PROTECTION, laddr, pl, rw);

  if (BX_CPU_THIS_PTR cr4.get_PGE())
    combined_access |= (entry[leaf] & 0x100); // G

  // Update A bit if needed.
  for (int level=BX_LEVEL_PML4; level > leaf; level--) {
    if (!(entry[level] & 0x20)) {
      entry[level] |= 0x20;
      access_write_physical(entry_addr[level], 8, &entry[level]);
      BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, entry_addr[level], 8,
            (BX_PTE_ACCESS + (level<<4)) | BX_WRITE, (Bit8u*)(&entry[level]));
    }
  }

  // Update A/D bits if needed.
  if (!(entry[leaf] & 0x20) || (isWrite && !(entry[leaf] & 0x40))) {
    entry[leaf] |= (0x20 | (isWrite<<6)); // Update A and possibly D bits
    access_write_physical(entry_addr[leaf], 8, &entry[leaf]);
    BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, entry_addr[leaf], 8, 
            (BX_PTE_ACCESS + (leaf<<4)) | BX_WRITE, (Bit8u*)(&entry[leaf]));
  }

  return ppf;
}

#endif

//          Format of Legacy PAE PDPTR entry (PDPTE)
// -----------------------------------------------------------
// 00    | Present (P)
// 02-01 | Reserved (must be zero)
// 03    | Page-Level Write-Through (PWT) (486+), 0=reserved otherwise
// 04    | Page-Level Cache-Disable (PCD) (486+), 0=reserved otherwise
// 08-05 | Reserved (must be zero)
// 11-09 | (ignored)
// PA-12 | Physical address of 4-KByte aligned page directory
// 63-PA | Reserved (must be zero)
// -----------------------------------------------------------

#define PAGING_PAE_PDPTE_RESERVED_BITS \
    (BX_PAGING_PHY_ADDRESS_RESERVED_BITS | BX_CONST64(0xFFF00000000001E6))

bx_bool BX_CPP_AttrRegparmN(1) BX_CPU_C::CheckPDPTR(bx_phy_address cr3_val)
{
  cr3_val &= 0xffffffe0;
#if BX_SUPPORT_VMX >= 2
  if (BX_CPU_THIS_PTR in_vmx_guest) {
    if (SECONDARY_VMEXEC_CONTROL(VMX_VM_EXEC_CTRL3_EPT_ENABLE))
      cr3_val = translate_guest_physical(cr3_val, 0, 0, 0, BX_READ);
  }
#endif

  Bit64u pdptr[4];
  int n;

  for (n=0; n<4; n++) {
    // read and check PDPTE entries
    bx_phy_address pdpe_entry_addr = (bx_phy_address) (cr3_val | (n << 3));
    access_read_physical(pdpe_entry_addr, 8, &(pdptr[n]));
    BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, pdpe_entry_addr, 8,
              (BX_PDPTR0_ACCESS + (n<<4)) | BX_READ, (Bit8u*) &(pdptr[n]));

    if (pdptr[n] & 0x1) {
       if (pdptr[n] & PAGING_PAE_PDPTE_RESERVED_BITS) return 0;
    }
  }

  // load new PDPTRs
  for (n=0; n<4; n++)
    BX_CPU_THIS_PTR PDPTR_CACHE.entry[n] = pdptr[n];

  BX_CPU_THIS_PTR PDPTR_CACHE.valid = 1;

  return 1; /* PDPTRs are fine */
}

#if BX_SUPPORT_VMX >= 2
bx_bool BX_CPP_AttrRegparmN(1) BX_CPU_C::CheckPDPTR(Bit64u *pdptr)
{
  for (int n=0; n<4; n++) {
     if (pdptr[n] & 0x1) {
        if (pdptr[n] & PAGING_PAE_PDPTE_RESERVED_BITS) return 0;
     }
  }

  return 1; /* PDPTRs are fine */
}
#endif

// Translate a linear address to a physical address in PAE paging mode
bx_phy_address BX_CPU_C::translate_linear_PAE(bx_address laddr, Bit32u &lpf_mask, Bit32u &combined_access, unsigned curr_pl, unsigned rw)
{
  bx_phy_address entry_addr[3], ppf;
  Bit64u entry[3];
  bx_bool nx_fault = 0;
  unsigned pl = (curr_pl == 3);
  int leaf = BX_LEVEL_PTE;
  combined_access = 0x06;

#if BX_SUPPORT_X86_64
  if (long_mode()) {
    return translate_linear_long_mode(laddr, lpf_mask, combined_access, curr_pl, rw);
  }
#endif

  if (! BX_CPU_THIS_PTR PDPTR_CACHE.valid) {
    BX_PANIC(("PDPTR_CACHE not valid !"));
    if (! CheckPDPTR(BX_CPU_THIS_PTR cr3)) {
      BX_ERROR(("translate_linear_PAE(): PDPTR check failed !"));
      exception(BX_GP_EXCEPTION, 0);
    }
  }

  entry[BX_LEVEL_PDPE] = BX_CPU_THIS_PTR PDPTR_CACHE.entry[(laddr >> 30) & 3];

  int fault = check_entry_PAE("PDPE", entry[BX_LEVEL_PDPE], PAGING_PAE_PDPTE_RESERVED_BITS, rw, &nx_fault);
  if (fault >= 0)
    page_fault(fault, laddr, pl, rw);

  entry_addr[BX_LEVEL_PDE] = (bx_phy_address)((entry[BX_LEVEL_PDPE] & BX_CONST64(0x000ffffffffff000))
                         | ((laddr & 0x3fe00000) >> 18));
#if BX_SUPPORT_VMX >= 2
  if (BX_CPU_THIS_PTR in_vmx_guest) {
    if (SECONDARY_VMEXEC_CONTROL(VMX_VM_EXEC_CTRL3_EPT_ENABLE))
      entry_addr[BX_LEVEL_PDE] = translate_guest_physical(entry_addr[BX_LEVEL_PDE], laddr, 1, 1, rw);
  }
#endif
  access_read_physical(entry_addr[BX_LEVEL_PDE], 8, &entry[BX_LEVEL_PDE]);
  BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, entry_addr[BX_LEVEL_PDE], 8, BX_PDE_ACCESS | BX_READ, (Bit8u*)(&entry[BX_LEVEL_PDE]));

  fault = check_entry_PAE("PDE", entry[BX_LEVEL_PDE], PAGING_PAE_RESERVED_BITS, rw, &nx_fault);
  if (fault >= 0)
    page_fault(fault, laddr, pl, rw);

  combined_access &= entry[BX_LEVEL_PDE] & 0x06; // U/S and R/W

  // Ignore CR4.PSE in PAE mode
  if (entry[BX_LEVEL_PDE] & 0x80) {
    if (entry[BX_LEVEL_PDE] & PAGING_PAE_PDE2M_RESERVED_BITS) {
      BX_DEBUG(("PAE PDE2M: reserved bit is set PDE=%08x:%08x", GET32H(entry[BX_LEVEL_PDE]), GET32L(entry[BX_LEVEL_PDE])));
      page_fault(ERROR_RESERVED | ERROR_PROTECTION, laddr, pl, rw);
    }

    ppf = (bx_phy_address)((entry[BX_LEVEL_PDE] & BX_CONST64(0x000fffffffe00000)) | (laddr & 0x001ff000));
    lpf_mask = 0x1fffff;
    leaf = BX_LEVEL_PDE;
  }
  else {
    // 4k pages, Get page table entry.
    entry_addr[BX_LEVEL_PTE] = (bx_phy_address)((entry[BX_LEVEL_PDE] & BX_CONST64(0x000ffffffffff000)) |
                           ((laddr & 0x001ff000) >> 9));
#if BX_SUPPORT_VMX >= 2
    if (BX_CPU_THIS_PTR in_vmx_guest) {
      if (SECONDARY_VMEXEC_CONTROL(VMX_VM_EXEC_CTRL3_EPT_ENABLE))
        entry_addr[BX_LEVEL_PTE] = translate_guest_physical(entry_addr[BX_LEVEL_PTE], laddr, 1, 1, rw);
    }
#endif
    access_read_physical(entry_addr[BX_LEVEL_PTE], 8, &entry[BX_LEVEL_PTE]);
    BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, entry_addr[BX_LEVEL_PTE], 8, BX_PTE_ACCESS | BX_READ, (Bit8u*)(&entry[BX_LEVEL_PTE]));

    fault = check_entry_PAE("PTE", entry[BX_LEVEL_PTE], PAGING_PAE_RESERVED_BITS, rw, &nx_fault);
    if (fault >= 0)
      page_fault(fault, laddr, pl, rw);

    combined_access &= entry[BX_LEVEL_PTE] & 0x06; // U/S and R/W

    // Make up the physical page frame address.
    ppf = (bx_phy_address)(entry[leaf] & BX_CONST64(0x000ffffffffff000));
    lpf_mask = 0xfff;
  }

  bx_bool isWrite = (rw & 1); // write or r-m-w

  unsigned priv_index = (BX_CPU_THIS_PTR cr0.get_WP() << 4) | // bit 4
                        (pl<<3) |                             // bit 3
                        (combined_access | isWrite);          // bit 2,1,0

  if (!priv_check[priv_index] || nx_fault)
    page_fault(ERROR_PROTECTION, laddr, pl, rw);

  if (BX_CPU_THIS_PTR cr4.get_PGE())
    combined_access |= (entry[leaf] & 0x100);     // G

  if (leaf == BX_LEVEL_PTE) {
    // Update PDE A bit if needed.
    if (!(entry[BX_LEVEL_PDE] & 0x20)) {
      entry[BX_LEVEL_PDE] |= 0x20;
      access_write_physical(entry_addr[BX_LEVEL_PDE], 8, &entry[BX_LEVEL_PDE]);
      BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, entry_addr[BX_LEVEL_PDE], 8,
             (BX_PDE_ACCESS | BX_WRITE), (Bit8u*)(&entry[BX_LEVEL_PDE]));
    }
  }

  // Update A/D bits if needed.
  if (!(entry[leaf] & 0x20) || (isWrite && !(entry[leaf] & 0x40))) {
    entry[leaf] |= (0x20 | (isWrite<<6)); // Update A and possibly D bits
    access_write_physical(entry_addr[leaf], 8, &entry[leaf]);
    BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, entry_addr[leaf], 8,
             (BX_PTE_ACCESS + (leaf<<4)) | BX_WRITE, (Bit8u*)(&entry[leaf]));
  }

  return ppf;
}

#endif

//           Format of a PDE that Maps a 4-MByte Page
// -----------------------------------------------------------
// 00    | Present (P)
// 01    | R/W
// 02    | U/S
// 03    | Page-Level Write-Through (PWT)
// 04    | Page-Level Cache-Disable (PCD)
// 05    | Accessed (A)
// 06    | Dirty (D)
// 07    | Page size, must be 1 to indicate 4-Mbyte page
// 08    | Global (G) (if CR4.PGE=1, ignored otherwise)
// 11-09 | (ignored)
// 12    | PAT (if PAT is supported, reserved otherwise)
// PA-13 | Bits PA-32 of physical address of the 4-MByte page
// 21-PA | Reserved (must be zero)
// 31-22 | Bits 31-22 of physical address of the 4-MByte page
// -----------------------------------------------------------

#define PAGING_PDE4M_RESERVED_BITS \
    (((1 << (41-BX_PHY_ADDRESS_WIDTH))-1) << (13 + BX_PHY_ADDRESS_WIDTH - 32))

// Translate a linear address to a physical address
bx_phy_address BX_CPU_C::translate_linear(bx_address laddr, unsigned curr_pl, unsigned rw)
{
  Bit32u combined_access = 0x06;
  Bit32u lpf_mask = 0xfff; // 4K pages
  unsigned priv_index;

  // note - we assume physical memory < 4gig so for brevity & speed, we'll use
  // 32 bit entries although cr3 is expanded to 64 bits.
  bx_phy_address paddress, ppf, poffset = PAGE_OFFSET(laddr);
  bx_bool isWrite = rw & 1; // write or r-m-w
  unsigned pl = (curr_pl == 3);

  InstrTLB_Increment(tlbLookups);
  InstrTLB_Stats();

  bx_address lpf = LPFOf(laddr);
  unsigned TLB_index = BX_TLB_INDEX_OF(lpf, 0);
  bx_TLB_entry *tlbEntry = &BX_CPU_THIS_PTR TLB.entry[TLB_index];

  // already looked up TLB for code access
  if (TLB_LPFOf(tlbEntry->lpf) == lpf)
  {
    paddress = tlbEntry->ppf | poffset;

    bx_bool isExecute = (rw == BX_EXECUTE);
    if (! (tlbEntry->accessBits & ((isExecute<<2) | (isWrite<<1) | pl)))
      return paddress;

    // The current access does not have permission according to the info
    // in our TLB cache entry.  Re-walk the page tables, in case there is
    // updated information in the memory image, and let the long path code
    // generate an exception if one is warranted.
  }

  if(BX_CPU_THIS_PTR cr0.get_PG())
  {
    InstrTLB_Increment(tlbMisses);

    BX_DEBUG(("page walk for address 0x" FMT_LIN_ADDRX, laddr));

#if BX_CPU_LEVEL >= 6
    if (BX_CPU_THIS_PTR cr4.get_PAE()) {
      ppf = translate_linear_PAE(laddr, lpf_mask, combined_access, curr_pl, rw);
    }
    else
#endif 
    {
      // CR4.PAE==0 (and EFER.LMA==0)
      Bit32u pde, pte, cr3_masked = BX_CPU_THIS_PTR cr3 & BX_CR3_PAGING_MASK;

      bx_phy_address pde_addr = (bx_phy_address) (cr3_masked | ((laddr & 0xffc00000) >> 20));
#if BX_SUPPORT_VMX >= 2
      if (BX_CPU_THIS_PTR in_vmx_guest) {
        if (SECONDARY_VMEXEC_CONTROL(VMX_VM_EXEC_CTRL3_EPT_ENABLE))
          pde_addr = translate_guest_physical(pde_addr, laddr, 1, 1, rw);
      }
#endif
      access_read_physical(pde_addr, 4, &pde);
      BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, pde_addr, 4, BX_PDE_ACCESS | BX_READ, (Bit8u*)(&pde));

      if (!(pde & 0x1)) {
        BX_DEBUG(("PDE: entry not present"));
        page_fault(ERROR_NOT_PRESENT, laddr, pl, rw);
      }

#if BX_CPU_LEVEL >= 5
      if ((pde & 0x80) && BX_CPU_THIS_PTR cr4.get_PSE()) {
        // 4M paging, only if CR4.PSE enabled, ignore PDE.PS otherwise
        if (pde & PAGING_PDE4M_RESERVED_BITS) {
          BX_DEBUG(("PSE PDE4M: reserved bit is set: PDE=0x%08x", pde));
          page_fault(ERROR_RESERVED | ERROR_PROTECTION, laddr, pl, rw);
        }

        // Combined access is just access from the pde (no pte involved).
        combined_access = pde & 0x06; // U/S and R/W

        priv_index =
          (BX_CPU_THIS_PTR cr0.get_WP() << 4) |  // bit 4
          (pl<<3) |                              // bit 3
          (combined_access | isWrite);           // bit 2,1,0

        if (!priv_check[priv_index])
          page_fault(ERROR_PROTECTION, laddr, pl, rw);

#if BX_CPU_LEVEL >= 6
        if (BX_CPU_THIS_PTR cr4.get_PGE())
          combined_access |= pde & 0x100;        // G
#endif

        // Update PDE A/D bits if needed.
        if (!(pde & 0x20) || (isWrite && !(pde & 0x40))) {
          pde |= (0x20 | (isWrite<<6)); // Update A and possibly D bits
          access_write_physical(pde_addr, 4, &pde);
          BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, pde_addr, 4, BX_PDE_ACCESS | BX_WRITE, (Bit8u*)(&pde));
        }

        // make up the physical frame number
        ppf = (pde & 0xffc00000) | (laddr & 0x003ff000);
#if BX_PHY_ADDRESS_WIDTH > 32
        ppf |= ((bx_phy_address)(pde & 0x003fe000)) << 19;
#endif
        lpf_mask = 0x3fffff;
      }
      else // else normal 4K page...
#endif
      {
        // Get page table entry
        bx_phy_address pte_addr = (bx_phy_address)((pde & 0xfffff000) | ((laddr & 0x003ff000) >> 10));
#if BX_SUPPORT_VMX >= 2
        if (BX_CPU_THIS_PTR in_vmx_guest) {
          if (SECONDARY_VMEXEC_CONTROL(VMX_VM_EXEC_CTRL3_EPT_ENABLE))
            pte_addr = translate_guest_physical(pte_addr, laddr, 1, 1, rw);
        }
#endif
        access_read_physical(pte_addr, 4, &pte);
        BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, pte_addr, 4, BX_PTE_ACCESS | BX_READ, (Bit8u*)(&pte));

        if (!(pte & 0x1)) {
          BX_DEBUG(("PTE: entry not present"));
          page_fault(ERROR_NOT_PRESENT, laddr, pl, rw);
        }

        // 386 and 486+ have different behaviour for combining
        // privilege from PDE and PTE.
#if BX_CPU_LEVEL == 3
        combined_access  = (pde | pte) & 0x04; // U/S
        combined_access |= (pde & pte) & 0x02; // R/W
#else // 486+
        combined_access  = (pde & pte) & 0x06; // U/S and R/W
#endif

        priv_index =
#if BX_CPU_LEVEL >= 4
          (BX_CPU_THIS_PTR cr0.get_WP() << 4) |  // bit 4
#endif
          (pl<<3) |                              // bit 3
          (combined_access | isWrite);           // bit 2,1,0

        if (!priv_check[priv_index])
          page_fault(ERROR_PROTECTION, laddr, pl, rw);

#if BX_CPU_LEVEL >= 6
        if (BX_CPU_THIS_PTR cr4.get_PGE())
          combined_access |= (pte & 0x100);      // G
#endif

        // Update PDE A bit if needed.
        if (!(pde & 0x20)) {
          pde |= 0x20;
          access_write_physical(pde_addr, 4, &pde);
          BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, pde_addr, 4, BX_PDE_ACCESS | BX_WRITE, (Bit8u*)(&pde));
        }

        // Update PTE A/D bits if needed.
        if (!(pte & 0x20) || (isWrite && !(pte & 0x40))) {
          pte |= (0x20 | (isWrite<<6)); // Update A and possibly D bits
          access_write_physical(pte_addr, 4, &pte);
          BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, pte_addr, 4, BX_PTE_ACCESS | BX_WRITE, (Bit8u*)(&pte));
        }

        // Make up the physical page frame address.
        ppf = pte & 0xfffff000;
      }
    }

#if BX_CPU_LEVEL >= 5
    if (lpf_mask > 0xfff)
      BX_CPU_THIS_PTR TLB.split_large = 1;
#endif
  }
  else {
    // no paging
    ppf = (bx_phy_address) lpf;
  }

#if BX_SUPPORT_VMX >= 2
  if (BX_CPU_THIS_PTR in_vmx_guest) {
    if (SECONDARY_VMEXEC_CONTROL(VMX_VM_EXEC_CTRL3_EPT_ENABLE)) {
      ppf = translate_guest_physical(ppf, laddr, 1, 0, rw);
    }
  }
#endif

  // Calculate physical memory address and fill in TLB cache entry
  paddress = ppf | poffset;

  // direct memory access is NOT allowed by default
  tlbEntry->lpf = lpf | TLB_HostPtr;
  tlbEntry->lpf_mask = lpf_mask;
  tlbEntry->ppf = ppf;
  tlbEntry->accessBits = 0;

  if ((combined_access & 4) == 0) { // System
    tlbEntry->accessBits |= TLB_SysOnly;
    if (! isWrite)
      tlbEntry->accessBits |= TLB_ReadOnly;
  }
  else {
    // Current operation is a read or a page is read only
    // Not efficient handling of system write to user read only page:
    // hopefully it is very rare case, optimize later
    if (! isWrite || (combined_access & 2) == 0) {
       tlbEntry->accessBits |= TLB_ReadOnly;
    }
  }

#if BX_CPU_LEVEL >= 6
  if (combined_access & 0x100) // Global bit
    tlbEntry->accessBits |= TLB_GlobalPage;
#endif

#if BX_SUPPORT_X86_64
  // EFER.NXE change won't flush TLB
  if (BX_CPU_THIS_PTR cr4.get_PAE() && rw != BX_EXECUTE)
    tlbEntry->accessBits |= TLB_NoExecute;
#endif

  // Attempt to get a host pointer to this physical page. Put that
  // pointer in the TLB cache. Note if the request is vetoed, NULL
  // will be returned, and it's OK to OR zero in anyways.
  tlbEntry->hostPageAddr = BX_CPU_THIS_PTR getHostMemAddr(ppf, rw);
  if (tlbEntry->hostPageAddr) {
    // All access allowed also via direct pointer
#if BX_X86_DEBUGGER
    if (! hwbreakpoint_check(laddr))
#endif
       tlbEntry->lpf = lpf; // allow direct access with HostPtr
  }

  return paddress;
}

#if BX_SUPPORT_VMX >= 2

/* EPT access type */
#define BX_EPT_READ    0x01
#define BX_EPT_WRITE   0x02
#define BX_EPT_EXECUTE 0x04

/* EPT access mask */
#define BX_EPT_ENTRY_NOT_PRESENT        0x00
#define BX_EPT_ENTRY_READ_ONLY          0x01
#define BX_EPT_ENTRY_WRITE_ONLY         0x02
#define BX_EPT_ENTRY_READ_WRITE         0x03
#define BX_EPT_ENTRY_EXECUTE_ONLY       0x04
#define BX_EPT_ENTRY_READ_EXECUTE       0x05
#define BX_EPT_ENTRY_WRITE_EXECUTE      0x06
#define BX_EPT_ENTRY_READ_WRITE_EXECUTE 0x07

//                   Format of a EPT Entry
// -----------------------------------------------------------
// 00    | Read access
// 01    | Write access
// 02    | Execute Access
// 05-03 | EPT Memory type (for leaf entries, reserved otherwise)
// 06    | Ignore PAT memory type (for leaf entries, reserved otherwise)
// 07    | Page Size, must be 1 to indicate a Large Page
// 11-08 | (ignored)
// PA-12 | Physical address
// 51-PA | Reserved (must be zero)
// 63-52 | (ignored)
// -----------------------------------------------------------

#define PAGING_EPT_RESERVED_BITS (BX_PAGING_PHY_ADDRESS_RESERVED_BITS)

bx_phy_address BX_CPU_C::translate_guest_physical(bx_phy_address guest_paddr, bx_address guest_laddr, bx_bool guest_laddr_valid, bx_bool is_page_walk, unsigned rw)
{
  VMCS_CACHE *vm = &BX_CPU_THIS_PTR vmcs;
  bx_phy_address entry_addr[4], ppf = 0, pbase = LPFOf(vm->eptptr);
  Bit64u entry[4];
  int leaf = BX_LEVEL_PTE;
  Bit32u combined_access = 0x7, access_mask = 0;

  BX_DEBUG(("EPT walk for guest paddr 0x" FMT_ADDRX, guest_paddr));

  if (rw == BX_EXECUTE) access_mask |= BX_EPT_EXECUTE;
  if (rw & 1) access_mask |= BX_EPT_WRITE; // write or r-m-w
  if (rw == BX_READ) access_mask |= BX_EPT_READ;

  Bit32u vmexit_reason = 0, vmexit_qualification = access_mask;

  for (leaf = BX_LEVEL_PML4;; --leaf) {
    entry_addr[leaf] = pbase + ((guest_paddr >> (9 + 9*leaf)) & 0xff8);
    access_read_physical(entry_addr[leaf], 8, &entry[leaf]);
    BX_DBG_PHY_MEMORY_ACCESS(BX_CPU_ID, entry_addr[leaf], 8,
          (BX_EPT_PTE_ACCESS + (leaf<<4)) | BX_READ, (Bit8u*)(&entry[leaf]));

    Bit64u curr_entry = entry[leaf];
    Bit32u curr_access_mask = curr_entry & 0x7;

    combined_access &= curr_access_mask;

    if (curr_access_mask == BX_EPT_ENTRY_NOT_PRESENT) {
      BX_DEBUG(("EPT %s: not present", bx_paging_level[leaf]));
      vmexit_reason = VMX_VMEXIT_EPT_VIOLATION;
      break;
    }

    if (curr_access_mask == BX_EPT_ENTRY_WRITE_ONLY || curr_access_mask == BX_EPT_ENTRY_WRITE_EXECUTE) {
      BX_DEBUG(("EPT %s: EPT misconfiguration mask=%d",
        bx_paging_level[leaf], curr_access_mask));
      vmexit_reason = VMX_VMEXIT_EPT_MISCONFIGURATION;
      break;
    }

    extern bx_bool isMemTypeValidMTRR(unsigned memtype);
    if (! isMemTypeValidMTRR((curr_entry >> 3) & 7)) {
      BX_DEBUG(("EPT %s: EPT misconfiguration memtype=%d", bx_paging_level[leaf], (curr_entry >> 3) & 7));
      vmexit_reason = VMX_VMEXIT_EPT_MISCONFIGURATION;
      break;
    }

    if (curr_entry & PAGING_EPT_RESERVED_BITS) {
      BX_DEBUG(("EPT %s: reserved bit is set %08x:%08x",
        bx_paging_level[leaf], GET32H(curr_entry), GET32L(curr_entry)));
      vmexit_reason = VMX_VMEXIT_EPT_MISCONFIGURATION;
      break;
    }

    pbase = curr_entry & BX_CONST64(0x000ffffffffff000);

    if (leaf == BX_LEVEL_PTE) {
      // Make up the physical page frame address.
      ppf = (bx_phy_address)(curr_entry & BX_CONST64(0x000ffffffffff000));
      break;
    }

    if (curr_entry & 0x80) {
      if (leaf > (BX_LEVEL_PDE + bx_cpuid_support_1g_paging())) {
        BX_DEBUG(("EPT %s: PS bit set !"));
        vmexit_reason = VMX_VMEXIT_EPT_VIOLATION;
        break;
      }

      if (leaf == BX_LEVEL_PDPE) {
        if (curr_entry & PAGING_PAE_PDPTE1G_RESERVED_BITS) {
           BX_DEBUG(("EPT PDPE1G: reserved bit is set: PDPE=%08x:%08x", GET32H(curr_entry), GET32L(curr_entry)));
           vmexit_reason = VMX_VMEXIT_EPT_VIOLATION;
           break;
        }

        // Make up the physical page frame address.
        ppf = (bx_phy_address)((curr_entry & BX_CONST64(0x000fffffc0000000)) | (guest_paddr & 0x3ffff000));
        break;
      }

      if (leaf == BX_LEVEL_PDE) {
        if (curr_entry & PAGING_PAE_PDE2M_RESERVED_BITS) {
          BX_DEBUG(("EPT PDE2M: reserved bit is set PDE=%08x:%08x", GET32H(curr_entry), GET32L(curr_entry)));
          vmexit_reason = VMX_VMEXIT_EPT_VIOLATION;
          break;
        }

        // Make up the physical page frame address.
        ppf = (bx_phy_address)((curr_entry & BX_CONST64(0x000fffffffe00000)) | (guest_paddr & 0x001ff000));
        break;
      }
    }
  }

  if ((access_mask & combined_access) != access_mask) {
    vmexit_reason = VMX_VMEXIT_EPT_VIOLATION;
  }

  if (vmexit_reason) {
    BX_ERROR(("VMEXIT: EPT %s for guest paddr 0x" FMT_ADDRX " laddr " FMT_ADDRX,
      (vmexit_reason == VMX_VMEXIT_EPT_VIOLATION) ? "violation" : "misconfig", guest_paddr, guest_laddr));
    VMwrite64(VMCS_64BIT_GUEST_PHYSICAL_ADDR, guest_paddr);
    if (guest_laddr_valid) {
      VMwrite64(VMCS_GUEST_LINEAR_ADDR, guest_laddr);
      vmexit_qualification |= 0x80;
      if (is_page_walk) vmexit_qualification |= 0x100;
    }
    VMexit(0, vmexit_reason, vmexit_qualification | (combined_access << 3));
  }

  Bit32u page_offset = PAGE_OFFSET(guest_paddr);
  return ppf | page_offset;
}

#endif

#if BX_DEBUGGER || BX_DISASM || BX_INSTRUMENTATION || BX_GDBSTUB

#if BX_DEBUGGER

void dbg_print_paging_pte(int level, Bit64u entry)
{
  dbg_printf("%4s: 0x%08x%08x", bx_paging_level[level], GET32H(entry), GET32L(entry));

  if (level == BX_LEVEL_PTE) {
    dbg_printf("    %s %s %s",
      (entry & 0x0100) ? "G" : "g",
      (entry & 0x0080) ? "PAT" : "pat",
      (entry & 0x0040) ? "D" : "d");
  }
  else {
    if (entry & 0x80) {
      dbg_printf(" PS %s %s %s",
        (entry & 0x0100) ? "G" : "g",
        (entry & 0x1000) ? "PAT" : "pat",
        (entry & 0x0040) ? "D" : "d");
    }
    else {
      dbg_printf(" ps        ");
    }
  }

  dbg_printf(" %s %s %s %s %s %s\n",
    (entry & 0x20) ? "A" : "a",
    (entry & 0x10) ? "PCD" : "pcd",
    (entry & 0x08) ? "PWT" : "pwt",
    (entry & 0x04) ? "U" : "S",
    (entry & 0x02) ? "W" : "R",
    (entry & 0x01) ? "P" : "p");
}

#if BX_SUPPORT_VMX >= 2
void dbg_print_ept_paging_pte(int level, Bit64u entry)
{
  dbg_printf("EPT %4s: 0x%08x%08x", bx_paging_level[level], GET32H(entry), GET32L(entry));

  if (level != BX_LEVEL_PTE && (entry & 0x80))
    dbg_printf(" PS");
  else
    dbg_printf("   ");

  dbg_printf(" %s %s %s\n",
    (entry & 0x04) ? "E" : "e",
    (entry & 0x02) ? "W" : "w",
    (entry & 0x01) ? "R" : "r");
}
#endif

#endif // BX_DEBUGGER

#if BX_SUPPORT_VMX >= 2
bx_bool BX_CPU_C::dbg_translate_guest_physical(bx_phy_address guest_paddr, bx_phy_address *phy, bx_bool verbose)
{
  VMCS_CACHE *vm = &BX_CPU_THIS_PTR vmcs;
  bx_phy_address pt_address = LPFOf(vm->eptptr);
  Bit64u offset_mask = BX_CONST64(0x0000ffffffffffff);

  for (int level = 3; level >= 0; --level) {
    Bit64u pte;
    pt_address += ((guest_paddr >> (9 + 9*level)) & 0xff8);
    offset_mask >>= 9;
    BX_MEM(0)->readPhysicalPage(BX_CPU_THIS, pt_address, 8, &pte);
#if BX_DEBUGGER
    if (verbose)
      dbg_print_ept_paging_pte(level, pte);
#endif
    switch(pte & 7) {
    case BX_EPT_ENTRY_NOT_PRESENT:
    case BX_EPT_ENTRY_WRITE_ONLY:
    case BX_EPT_ENTRY_WRITE_EXECUTE:
      return 0;
    }
    if (pte & BX_PAGING_PHY_ADDRESS_RESERVED_BITS)
      return 0;

    pt_address = bx_phy_address(pte & BX_CONST64(0x000ffffffffff000));

    if (level == BX_LEVEL_PTE) break;

    if (pte & 0x80) {
       if (level > (BX_LEVEL_PDE + bx_cpuid_support_1g_paging()))
         return 0;

        pt_address &= BX_CONST64(0x000fffffffffe000);
        if (pt_address & offset_mask) return 0;
        break;
      }
  }

  *phy = pt_address + (bx_phy_address)(guest_paddr & offset_mask);
  return 1;
}
#endif

bx_bool BX_CPU_C::dbg_xlate_linear2phy(bx_address laddr, bx_phy_address *phy, bx_bool verbose)
{
  if (! BX_CPU_THIS_PTR cr0.get_PG()) {
    *phy = (bx_phy_address) laddr;
    return 1;
  }

  bx_phy_address paddress;
  bx_phy_address pt_address = BX_CPU_THIS_PTR cr3 & BX_CR3_PAGING_MASK;

  // see if page is in the TLB first
  if (! verbose) {
    bx_address lpf = LPFOf(laddr);
    unsigned TLB_index = BX_TLB_INDEX_OF(lpf, 0);
    bx_TLB_entry *tlbEntry  = &BX_CPU_THIS_PTR TLB.entry[TLB_index];

    if (TLB_LPFOf(tlbEntry->lpf) == lpf) {
      paddress = tlbEntry->ppf | PAGE_OFFSET(laddr);
      *phy = paddress;
      return 1;
    }
  }

#if BX_CPU_LEVEL >= 6
  if (BX_CPU_THIS_PTR cr4.get_PAE()) {
    Bit64u offset_mask = BX_CONST64(0x0000ffffffffffff);

    int level = 3;
    if (! long_mode()) {
      if (! BX_CPU_THIS_PTR PDPTR_CACHE.valid)
         goto page_fault;
      pt_address = BX_CPU_THIS_PTR PDPTR_CACHE.entry[(laddr >> 30) & 3] & BX_CONST64(0x000ffffffffff000);
      offset_mask >>= 18;
      level = 1;
    }

    for (; level >= 0; --level) {
      Bit64u pte;
      pt_address += ((laddr >> (9 + 9*level)) & 0xff8);
      offset_mask >>= 9;
#if BX_SUPPORT_VMX >= 2
      if (BX_CPU_THIS_PTR in_vmx_guest) {
        if (SECONDARY_VMEXEC_CONTROL(VMX_VM_EXEC_CTRL3_EPT_ENABLE)) {
          if (! dbg_translate_guest_physical(pt_address, &pt_address, verbose))
            goto page_fault;
        }
      }
#endif
      BX_MEM(0)->readPhysicalPage(BX_CPU_THIS, pt_address, 8, &pte);
#if BX_DEBUGGER
      if (verbose)
        dbg_print_paging_pte(level, pte);
#endif
      if(!(pte & 1))
        goto page_fault;
      if (pte & BX_PAGING_PHY_ADDRESS_RESERVED_BITS)
        goto page_fault;
      pt_address = bx_phy_address(pte & BX_CONST64(0x000ffffffffff000));
      if (level == BX_LEVEL_PTE) break;
      if (pte & 0x80) {
        // 2M page
        if (level == BX_LEVEL_PDE) {
          pt_address &= BX_CONST64(0x000fffffffffe000);
          if (pt_address & offset_mask)
            goto page_fault;
          break;
        }
        // 1G page
        if (bx_cpuid_support_1g_paging() && level == BX_LEVEL_PDPE && long_mode()) {
          pt_address &= BX_CONST64(0x000fffffffffe000);
          if (pt_address & offset_mask)
            goto page_fault;
          break;
        }
        goto page_fault;
      }
    }
    paddress = pt_address + (bx_phy_address)(laddr & offset_mask);
  }
  else   // not PAE
#endif
  {
    Bit32u offset_mask = 0xfff;
    for (int level = 1; level >= 0; --level) {
      Bit32u pte;
      pt_address += ((laddr >> (10 + 10*level)) & 0xffc);
#if BX_SUPPORT_VMX >= 2
      if (BX_CPU_THIS_PTR in_vmx_guest) {
        if (SECONDARY_VMEXEC_CONTROL(VMX_VM_EXEC_CTRL3_EPT_ENABLE)) {
          if (! dbg_translate_guest_physical(pt_address, &pt_address, verbose))
            goto page_fault;
        }
      }
#endif
      BX_MEM(0)->readPhysicalPage(BX_CPU_THIS, pt_address, 4, &pte);
#if BX_DEBUGGER
      if (verbose)
        dbg_print_paging_pte(level, pte);
#endif
      if (!(pte & 1)) 
        goto page_fault;
      pt_address = pte & 0xfffff000;
      if (level == BX_LEVEL_PDE && (pte & 0x80) != 0 && BX_CPU_THIS_PTR cr4.get_PSE()) {
        offset_mask = 0x3fffff;
        pt_address = pte & 0xffc00000;
#if BX_PHY_ADDRESS_WIDTH > 32
        pt_address += ((bx_phy_address)(pte & 0x003fe000)) << 19;
#endif
        break;
      }
    }
    paddress = pt_address + (bx_phy_address)(laddr & offset_mask);
  }

#if BX_SUPPORT_VMX >= 2
  if (BX_CPU_THIS_PTR in_vmx_guest) {
    if (SECONDARY_VMEXEC_CONTROL(VMX_VM_EXEC_CTRL3_EPT_ENABLE)) {
      if (! dbg_translate_guest_physical(paddress, &paddress, verbose))
        goto page_fault;
    }
  }
#endif

  *phy = paddress;
  return 1;

page_fault:
  *phy = 0;
  return 0;
}
#endif

void BX_CPU_C::access_write_linear(bx_address laddr, unsigned len, unsigned curr_pl, void *data)
{
#if BX_X86_DEBUGGER
  hwbreakpoint_match(laddr, len, BX_WRITE);
#endif

  Bit32u pageOffset = PAGE_OFFSET(laddr);

  /* check for reference across multiple pages */
  if ((pageOffset + len) <= 4096) {
    // Access within single page.
    BX_CPU_THIS_PTR address_xlation.paddress1 =
        dtranslate_linear(laddr, curr_pl, BX_WRITE);
    BX_CPU_THIS_PTR address_xlation.pages     = 1;

    BX_INSTR_LIN_ACCESS(BX_CPU_ID, laddr, BX_CPU_THIS_PTR address_xlation.paddress1, len, BX_WRITE);
    BX_DBG_LIN_MEMORY_ACCESS(BX_CPU_ID, laddr, BX_CPU_THIS_PTR address_xlation.paddress1,
                          len, curr_pl, BX_WRITE, (Bit8u*) data);

    access_write_physical(BX_CPU_THIS_PTR address_xlation.paddress1, len, data);
  }
  else {
    // access across 2 pages
    BX_CPU_THIS_PTR address_xlation.paddress1 =
        dtranslate_linear(laddr, curr_pl, BX_WRITE);
    BX_CPU_THIS_PTR address_xlation.len1 = 4096 - pageOffset;
    BX_CPU_THIS_PTR address_xlation.len2 = len -
        BX_CPU_THIS_PTR address_xlation.len1;
    BX_CPU_THIS_PTR address_xlation.pages     = 2;
    bx_address laddr2 = laddr + BX_CPU_THIS_PTR address_xlation.len1;
    BX_CPU_THIS_PTR address_xlation.paddress2 =
        dtranslate_linear(laddr2, curr_pl, BX_WRITE);

#ifdef BX_LITTLE_ENDIAN
    BX_INSTR_LIN_ACCESS(BX_CPU_ID, laddr,
        BX_CPU_THIS_PTR address_xlation.paddress1,
        BX_CPU_THIS_PTR address_xlation.len1, BX_WRITE);
    BX_DBG_LIN_MEMORY_ACCESS(BX_CPU_ID, laddr,
        BX_CPU_THIS_PTR address_xlation.paddress1,
        BX_CPU_THIS_PTR address_xlation.len1, curr_pl, 
        BX_WRITE, (Bit8u*) data);
    access_write_physical(BX_CPU_THIS_PTR address_xlation.paddress1,
        BX_CPU_THIS_PTR address_xlation.len1, data);
    BX_INSTR_LIN_ACCESS(BX_CPU_ID, laddr2, BX_CPU_THIS_PTR address_xlation.paddress2,
        BX_CPU_THIS_PTR address_xlation.len2, BX_WRITE);
    BX_DBG_LIN_MEMORY_ACCESS(BX_CPU_ID, laddr2, BX_CPU_THIS_PTR address_xlation.paddress2,
        BX_CPU_THIS_PTR address_xlation.len2, curr_pl, 
        BX_WRITE, ((Bit8u*)data) + BX_CPU_THIS_PTR address_xlation.len1);
    access_write_physical(BX_CPU_THIS_PTR address_xlation.paddress2,
        BX_CPU_THIS_PTR address_xlation.len2,
        ((Bit8u*)data) + BX_CPU_THIS_PTR address_xlation.len1);
#else // BX_BIG_ENDIAN
    BX_INSTR_LIN_ACCESS(BX_CPU_ID, laddr,
        BX_CPU_THIS_PTR address_xlation.paddress1,
        BX_CPU_THIS_PTR address_xlation.len1, BX_WRITE);
    BX_DBG_LIN_MEMORY_ACCESS(BX_CPU_ID, laddr,
        BX_CPU_THIS_PTR address_xlation.paddress1,
        BX_CPU_THIS_PTR address_xlation.len1, curr_pl, 
        BX_WRITE, ((Bit8u*)data) + (len - BX_CPU_THIS_PTR address_xlation.len1));
    access_write_physical(BX_CPU_THIS_PTR address_xlation.paddress1,
        BX_CPU_THIS_PTR address_xlation.len1,
        ((Bit8u*)data) + (len - BX_CPU_THIS_PTR address_xlation.len1));
    BX_INSTR_LIN_ACCESS(BX_CPU_ID, laddr2, BX_CPU_THIS_PTR address_xlation.paddress2,
        BX_CPU_THIS_PTR address_xlation.len2, BX_WRITE);
    BX_DBG_LIN_MEMORY_ACCESS(BX_CPU_ID, laddr2, BX_CPU_THIS_PTR address_xlation.paddress2,
        BX_CPU_THIS_PTR address_xlation.len2, curr_pl, 
        BX_WRITE, (Bit8u*) data);
    access_write_physical(BX_CPU_THIS_PTR address_xlation.paddress2,
        BX_CPU_THIS_PTR address_xlation.len2, data);
#endif
  }
}

void BX_CPU_C::access_read_linear(bx_address laddr, unsigned len, unsigned curr_pl, unsigned xlate_rw, void *data)
{
  BX_ASSERT(xlate_rw == BX_READ || xlate_rw == BX_RW);

#if BX_X86_DEBUGGER
  hwbreakpoint_match(laddr, len, xlate_rw);
#endif

  Bit32u pageOffset = PAGE_OFFSET(laddr);

  /* check for reference across multiple pages */
  if ((pageOffset + len) <= 4096) {
    // Access within single page.
    BX_CPU_THIS_PTR address_xlation.paddress1 =
        dtranslate_linear(laddr, curr_pl, xlate_rw);
    BX_CPU_THIS_PTR address_xlation.pages     = 1;
    access_read_physical(BX_CPU_THIS_PTR address_xlation.paddress1, len, data);
    BX_INSTR_LIN_ACCESS(BX_CPU_ID, laddr,
        BX_CPU_THIS_PTR address_xlation.paddress1, len, xlate_rw);
    BX_DBG_LIN_MEMORY_ACCESS(BX_CPU_ID, laddr,
        BX_CPU_THIS_PTR address_xlation.paddress1, len, curr_pl,
        BX_READ, (Bit8u*) data);
  }
  else {
    // access across 2 pages
    BX_CPU_THIS_PTR address_xlation.paddress1 =
        dtranslate_linear(laddr, curr_pl, xlate_rw);
    BX_CPU_THIS_PTR address_xlation.len1 = 4096 - pageOffset;
    BX_CPU_THIS_PTR address_xlation.len2 = len -
        BX_CPU_THIS_PTR address_xlation.len1;
    BX_CPU_THIS_PTR address_xlation.pages     = 2;
    bx_address laddr2 = laddr + BX_CPU_THIS_PTR address_xlation.len1;
    BX_CPU_THIS_PTR address_xlation.paddress2 =
        dtranslate_linear(laddr2, curr_pl, xlate_rw);

#ifdef BX_LITTLE_ENDIAN
    access_read_physical(BX_CPU_THIS_PTR address_xlation.paddress1,
        BX_CPU_THIS_PTR address_xlation.len1, data);
    BX_INSTR_LIN_ACCESS(BX_CPU_ID, laddr,
        BX_CPU_THIS_PTR address_xlation.paddress1,
        BX_CPU_THIS_PTR address_xlation.len1, xlate_rw);
    BX_DBG_LIN_MEMORY_ACCESS(BX_CPU_ID, laddr,
        BX_CPU_THIS_PTR address_xlation.paddress1,
        BX_CPU_THIS_PTR address_xlation.len1, curr_pl,
        BX_READ, (Bit8u*) data);
    access_read_physical(BX_CPU_THIS_PTR address_xlation.paddress2,
        BX_CPU_THIS_PTR address_xlation.len2,
        ((Bit8u*)data) + BX_CPU_THIS_PTR address_xlation.len1);
    BX_INSTR_LIN_ACCESS(BX_CPU_ID, laddr2, BX_CPU_THIS_PTR address_xlation.paddress2,
        BX_CPU_THIS_PTR address_xlation.len2, xlate_rw);
    BX_DBG_LIN_MEMORY_ACCESS(BX_CPU_ID, laddr2, BX_CPU_THIS_PTR address_xlation.paddress2,
        BX_CPU_THIS_PTR address_xlation.len2, curr_pl,
        BX_READ, ((Bit8u*)data) + BX_CPU_THIS_PTR address_xlation.len1);
#else // BX_BIG_ENDIAN
    access_read_physical(BX_CPU_THIS_PTR address_xlation.paddress1,
        BX_CPU_THIS_PTR address_xlation.len1,
        ((Bit8u*)data) + (len - BX_CPU_THIS_PTR address_xlation.len1));
    BX_INSTR_LIN_ACCESS(BX_CPU_ID, laddr,
        BX_CPU_THIS_PTR address_xlation.paddress1,
        BX_CPU_THIS_PTR address_xlation.len1, xlate_rw);
    BX_DBG_LIN_MEMORY_ACCESS(BX_CPU_ID, laddr,
        BX_CPU_THIS_PTR address_xlation.paddress1,
        BX_CPU_THIS_PTR address_xlation.len1, curr_pl,
        BX_READ, ((Bit8u*)data) + (len - BX_CPU_THIS_PTR address_xlation.len1));
    access_read_physical(BX_CPU_THIS_PTR address_xlation.paddress2,
        BX_CPU_THIS_PTR address_xlation.len2, data);
    BX_INSTR_LIN_ACCESS(BX_CPU_ID, laddr2, BX_CPU_THIS_PTR address_xlation.paddress2,
        BX_CPU_THIS_PTR address_xlation.len2, xlate_rw);
    BX_DBG_LIN_MEMORY_ACCESS(BX_CPU_ID, laddr2, BX_CPU_THIS_PTR address_xlation.paddress2,
        BX_CPU_THIS_PTR address_xlation.len2, curr_pl,
        BX_READ, (Bit8u*) data);
#endif
  }
}

void BX_CPU_C::access_write_physical(bx_phy_address paddr, unsigned len, void *data)
{
#if BX_SUPPORT_VMX >= 2
  if (is_virtual_apic_page(paddr)) {
    VMX_Virtual_Apic_Write(paddr, len, data);
    return;
  }
#endif

#if BX_SUPPORT_APIC
  if (BX_CPU_THIS_PTR lapic.is_selected(paddr)) {
    BX_CPU_THIS_PTR lapic.write(paddr, data, len);
    return;
  }
#endif

  BX_MEM(0)->writePhysicalPage(BX_CPU_THIS, paddr, len, data);
}

void BX_CPU_C::access_read_physical(bx_phy_address paddr, unsigned len, void *data)
{
#if BX_SUPPORT_VMX >= 2
  if (is_virtual_apic_page(paddr)) {
    VMX_Virtual_Apic_Read(paddr, len, data);
    return;
  }
#endif

#if BX_SUPPORT_APIC
  if (BX_CPU_THIS_PTR lapic.is_selected(paddr)) {
    BX_CPU_THIS_PTR lapic.read(paddr, data, len);
    return;
  }
#endif

  BX_MEM(0)->readPhysicalPage(BX_CPU_THIS, paddr, len, data);
}

bx_hostpageaddr_t BX_CPU_C::getHostMemAddr(bx_phy_address ppf, unsigned rw)
{
#if BX_SUPPORT_VMX >= 2
  if (is_virtual_apic_page(ppf))
    return 0; // Do not allow direct access to virtual apic page
#endif

#if BX_SUPPORT_APIC
  if (BX_CPU_THIS_PTR lapic.is_selected(ppf))
    return 0; // Vetoed!  APIC address space
#endif

  return (bx_hostpageaddr_t) BX_MEM(0)->getHostMemAddr(BX_CPU_THIS, ppf, rw);
}
