#ifndef P2K_QEMU_COMPAT_H
#define P2K_QEMU_COMPAT_H

/* QEMU 10.2 moved the address-space API from include/exec to include/system. */
#if __has_include("system/address-spaces.h")
#include "system/address-spaces.h"
#else
#include "exec/address-spaces.h"
#endif

#if __has_include("system/memory.h")
#include "system/memory.h"
#else
#include "exec/memory.h"
#endif

/* QEMU 10.2 moved the port-I/O API alongside the other system APIs. */
#if __has_include("system/ioport.h")
#include "system/ioport.h"
#else
#include "exec/ioport.h"
#endif

/* QEMU 10.2 made TypeInfo class-init data read-only. */
#if QEMU_VERSION_MAJOR > 10 || \
    (QEMU_VERSION_MAJOR == 10 && QEMU_VERSION_MINOR >= 2)
#define P2K_CLASS_INIT_DATA const void *
#else
#define P2K_CLASS_INIT_DATA void *
#endif

#if QEMU_VERSION_MAJOR > 10 || \
    (QEMU_VERSION_MAJOR == 10 && QEMU_VERSION_MINOR >= 2)
#define P2K_PIT_STATE(pit) ((PITCommonState *)(pit))
#else
#define P2K_PIT_STATE(pit) ((ISADevice *)(pit))
#endif

#endif
