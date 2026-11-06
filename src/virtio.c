/* SPDX-License-Identifier: GPL-2.0-or-later */
/* openinput: virtio input devices (virtio 1.0 over PCI, polled): keyboard,
 * mouse and tablet, as QEMU and other hypervisors offer them - on ARM64
 * virtual machines often the only input there is. The device sends Linux
 * input events (type, code, value) on its event queue; LEDs go back on the
 * status queue. */

#include <string.h>

#include "openaspi/native/host.h"
#include "openinput/input.h"

#define MAX_DEV 4
#define QSIZE   64u

/* virtio PCI capability types */
#define CAP_COMMON 1u
#define CAP_NOTIFY 2u
#define CAP_DEVICE 4u

/* common configuration */
#define C_DFSEL   0x00u
#define C_DF      0x04u
#define C_GFSEL   0x08u
#define C_GF      0x0Cu
#define C_STATUS  0x14u
#define C_QSEL    0x16u
#define C_QSIZE   0x18u
#define C_QENABLE 0x1Cu
#define C_QNOTIFY 0x1Eu
#define C_QDESC   0x20u
#define C_QAVAIL  0x28u
#define C_QUSED   0x30u

#define S_ACK       1u
#define S_DRIVER    2u
#define S_DRIVER_OK 4u
#define S_FEATURES_OK 8u

/* device configuration */
#define CFG_EV_BITS  0x11u
#define CFG_ABS_INFO 0x12u

#define EV_SYN 0u
#define EV_KEY 1u
#define EV_REL 2u
#define EV_ABS 3u
#define EV_LED 0x11u

#define BTN_LEFT 0x110u

struct desc {
    oa_u32 addr_lo, addr_hi, len;
    oa_u16 flags, next;
};

struct vq {
    unsigned size;
    volatile struct desc *desc;
    volatile oa_u16 *avail;   /* flags, idx, ring[] */
    volatile oa_u8 *used;     /* flags, idx, {id, len}[] */
    oa_u64 desc_bus, avail_bus, used_bus;
    oa_u16 last_used, avail_idx;
    volatile oa_u16 *notify;
    oa_u8 *bufs;              /* 8 bytes per descriptor */
    oa_u64 bufs_bus;
};

struct vdev {
    struct oi_source src;
    volatile oa_u8 *common, *device;
    volatile oa_u8 *notify_base;
    oa_u32 notify_mult;
    struct vq ev, st;
    oa_s32 abs_min[2], abs_max[2];
    char name[24];
};

static struct vdev vdevs[MAX_DEV];
static unsigned nvdevs;

static oa_u8 r8(volatile oa_u8 *b, unsigned o) { return b[o]; }
static oa_u16 r16(volatile oa_u8 *b, unsigned o) { return *(volatile oa_u16 *)(b + o); }
static oa_u32 r32(volatile oa_u8 *b, unsigned o) { return *(volatile oa_u32 *)(b + o); }
static void w8(volatile oa_u8 *b, unsigned o, oa_u8 v) { b[o] = v; }
static void w16(volatile oa_u8 *b, unsigned o, oa_u16 v) { *(volatile oa_u16 *)(b + o) = v; }
static void w32(volatile oa_u8 *b, unsigned o, oa_u32 v) { *(volatile oa_u32 *)(b + o) = v; }

static void w64(volatile oa_u8 *b, unsigned o, oa_u64 v)
{
    w32(b, o, (oa_u32)v);
    w32(b, o + 4u, (oa_u32)(v >> 32));
}

static oa_u16 le16(const oa_u8 *p) { return (oa_u16)(p[0] | (p[1] << 8)); }

static oa_u32 le32(const oa_u8 *p)
{
    return (oa_u32)p[0] | ((oa_u32)p[1] << 8) | ((oa_u32)p[2] << 16) | ((oa_u32)p[3] << 24);
}

static oa_u64 bar_addr(oa_u32 pci, unsigned bar)
{
    oa_u32 lo = oa_host_pci_read32(pci, 0x10u + 4u * bar);

    if (lo & 1u) {
        return 0; /* I/O BAR: not used by virtio 1.0 devices here */
    }
    if ((lo & 6u) == 4u) {
        return ((oa_u64)oa_host_pci_read32(pci, 0x14u + 4u * bar) << 32) | (lo & ~0xFul);
    }
    return lo & ~0xFul;
}

static int setup_queue(struct vdev *v, unsigned index, struct vq *q, int device_writes)
{
    unsigned n, i;
    void *mem;
    oa_u64 bus;

    w16(v->common, C_QSEL, (oa_u16)index);
    n = r16(v->common, C_QSIZE);
    if (n == 0u) {
        return -1;
    }
    if (n > QSIZE) {
        n = QSIZE;
    }
    w16(v->common, C_QSIZE, (oa_u16)n);
    /* one page: descriptors, available ring at 1024, used ring at 2048;
     * the buffers in a second */
    mem = oa_host_dma_alloc(4096, 4096, 0, &bus);
    q->bufs = oa_host_dma_alloc(4096, 64, 0, &q->bufs_bus);
    if (mem == 0 || q->bufs == 0) {
        return -1;
    }
    q->size = n;
    q->desc = mem;
    q->desc_bus = bus;
    q->avail = (volatile oa_u16 *)((oa_u8 *)mem + 1024);
    q->avail_bus = bus + 1024u;
    q->used = (oa_u8 *)mem + 2048;
    q->used_bus = bus + 2048u;
    for (i = 0; i < n; i++) {
        oa_u64 b = q->bufs_bus + 8u * i;
        q->desc[i].addr_lo = (oa_u32)b;
        q->desc[i].addr_hi = (oa_u32)(b >> 32);
        q->desc[i].len = 8;
        q->desc[i].flags = device_writes ? 2u : 0u; /* VIRTQ_DESC_F_WRITE */
        q->desc[i].next = 0;
    }
    q->notify = (volatile oa_u16 *)(v->notify_base + r16(v->common, C_QNOTIFY) * v->notify_mult);
    w64(v->common, C_QDESC, q->desc_bus);
    w64(v->common, C_QAVAIL, q->avail_bus);
    w64(v->common, C_QUSED, q->used_bus);
    w16(v->common, C_QENABLE, 1);
    if (device_writes) { /* hand every buffer to the device */
        for (i = 0; i < n; i++) {
            q->avail[2u + i] = (oa_u16)i;
        }
        q->avail_idx = (oa_u16)n;
        oa_host_mb();
        q->avail[1] = q->avail_idx;
    }
    return 0;
}

static unsigned cfg_query(struct vdev *v, oa_u8 select, oa_u8 subsel)
{
    w8(v->device, 0, select);
    w8(v->device, 1, subsel);
    oa_host_mb();
    return r8(v->device, 2);
}

static oa_s32 scale(struct vdev *v, unsigned axis, oa_s32 value)
{
    oa_s32 range = v->abs_max[axis] - v->abs_min[axis];

    if (range <= 0) {
        return 0;
    }
    if (value < v->abs_min[axis]) {
        value = v->abs_min[axis];
    }
    if (value > v->abs_max[axis]) {
        value = v->abs_max[axis];
    }
    return (oa_s32)(((oa_s64)(value - v->abs_min[axis]) * OI_ABS_MAX) / range);
}

static void event(struct vdev *v, oa_u16 type, oa_u16 code, oa_s32 value)
{
    oa_u16 u;

    if (type == EV_KEY) {
        if (value == 2) {
            return; /* auto repeat: the embedding system repeats */
        }
        if (code >= BTN_LEFT && code < BTN_LEFT + 8u) {
            oi_emit(&v->src, OI_PAGE_BUTTON, (oa_u16)(code - BTN_LEFT + 1u), value != 0);
        } else if ((u = oi_evdev_to_usage(code)) != 0u) {
            oi_emit(&v->src, OI_PAGE_KEYBOARD, u, value != 0);
        }
    } else if (type == EV_REL && value) {
        if (code == 0u) {
            oi_emit(&v->src, OI_PAGE_DESKTOP, OI_DESKTOP_X, value);
        } else if (code == 1u) {
            oi_emit(&v->src, OI_PAGE_DESKTOP, OI_DESKTOP_Y, value);
        } else if (code == 8u) {
            oi_emit(&v->src, OI_PAGE_DESKTOP, OI_DESKTOP_WHEEL, value);
        }
    } else if (type == EV_ABS && code < 2u) {
        oi_emit_abs(&v->src, OI_PAGE_DESKTOP, (oa_u16)(OI_DESKTOP_X + code), scale(v, code, value));
    }
}

static void v_poll(struct oi_source *src)
{
    struct vdev *v = src->priv;
    struct vq *q = &v->ev;
    oa_u16 idx = *(volatile oa_u16 *)(q->used + 2);
    int any = 0;

    while (q->last_used != idx) {
        volatile oa_u8 *e = q->used + 4u + 8u * (q->last_used % q->size);
        oa_u32 id = *(volatile oa_u32 *)e;
        const oa_u8 *b;

        oa_host_mb();
        if (id < q->size) {
            b = q->bufs + 8u * id;
            event(v, le16(b), le16(b + 2), (oa_s32)le32(b + 4));
            q->avail[2u + q->avail_idx % q->size] = (oa_u16)id;
            q->avail_idx++;
        }
        q->last_used++;
        any = 1;
    }
    if (any) {
        oa_host_mb();
        q->avail[1] = q->avail_idx;
        oa_host_mb();
        *q->notify = 0;
    }
}

static void put_status(struct vdev *v, oa_u16 type, oa_u16 code, oa_u32 value)
{
    struct vq *q = &v->st;
    unsigned d = q->avail_idx % q->size;
    oa_u8 *b = q->bufs + 8u * d;

    b[0] = (oa_u8)type;
    b[1] = (oa_u8)(type >> 8);
    b[2] = (oa_u8)code;
    b[3] = (oa_u8)(code >> 8);
    b[4] = (oa_u8)value;
    b[5] = (oa_u8)(value >> 8);
    b[6] = (oa_u8)(value >> 16);
    b[7] = (oa_u8)(value >> 24);
    q->avail[2u + d] = (oa_u16)d; /* descriptors are reused round robin */
    q->avail_idx++;
}

static void v_leds(struct oi_source *src, unsigned leds)
{
    struct vdev *v = src->priv;

    put_status(v, EV_LED, 0, (leds & OI_LED_NUM) != 0);
    put_status(v, EV_LED, 1, (leds & OI_LED_CAPS) != 0);
    put_status(v, EV_LED, 2, (leds & OI_LED_SCROLL) != 0);
    put_status(v, EV_SYN, 0, 0);
    oa_host_mb();
    v->st.avail[1] = v->st.avail_idx;
    oa_host_mb();
    *v->st.notify = 1;
}

int oi_virtio_add(oa_u32 pci)
{
    struct vdev *v;
    oa_u32 id = oa_host_pci_read32(pci, 0);
    unsigned cap, guard = 0, i;
    int has_rel, has_abs;

    if (nvdevs == MAX_DEV || (id & 0xFFFFu) != OI_VIRTIO_VENDOR || (id >> 16) != OI_VIRTIO_INPUT ||
        !(oa_host_pci_read32(pci, 0x04) & (1ul << 20))) {
        return 0;
    }
    v = &vdevs[nvdevs];
    memset(v, 0, sizeof(*v));
    oa_host_pci_write32(pci, 0x04, (oa_host_pci_read32(pci, 0x04) & 0xFFFFu) | 0x0006u);
    cap = oa_host_pci_read32(pci, 0x34) & 0xFCu;
    while (cap && guard++ < 48u) {
        oa_u32 h = oa_host_pci_read32(pci, cap);
        if ((h & 0xFFu) == 0x09u) { /* vendor specific: virtio */
            oa_u8 type = (oa_u8)(h >> 24);
            oa_u8 bar = (oa_u8)oa_host_pci_read32(pci, cap + 4u);
            oa_u32 off = oa_host_pci_read32(pci, cap + 8u), len = oa_host_pci_read32(pci, cap + 12u);
            oa_u64 base = bar < 6u ? bar_addr(pci, bar) : 0u;
            volatile oa_u8 *m = base ? oa_host_map_mmio(base + off, len) : 0;
            if (type == CAP_COMMON) {
                v->common = m;
            } else if (type == CAP_NOTIFY) {
                v->notify_base = m;
                v->notify_mult = oa_host_pci_read32(pci, cap + 16u);
            } else if (type == CAP_DEVICE) {
                v->device = m;
            }
        }
        cap = (h >> 8) & 0xFCu;
    }
    if (v->common == 0 || v->notify_base == 0 || v->device == 0) {
        return 0;
    }
    w8(v->common, C_STATUS, 0); /* reset */
    for (i = 0; i < 1000u && r8(v->common, C_STATUS) != 0u; i++) {
        oa_host_delay_us(100);
    }
    w8(v->common, C_STATUS, S_ACK | S_DRIVER);
    w32(v->common, C_DFSEL, 1);
    if (!(r32(v->common, C_DF) & 1u)) { /* VIRTIO_F_VERSION_1 */
        w8(v->common, C_STATUS, 0x80u);
        return 0;
    }
    w32(v->common, C_GFSEL, 0);
    w32(v->common, C_GF, 0);
    w32(v->common, C_GFSEL, 1);
    w32(v->common, C_GF, 1);
    w8(v->common, C_STATUS, S_ACK | S_DRIVER | S_FEATURES_OK);
    if (!(r8(v->common, C_STATUS) & S_FEATURES_OK) || setup_queue(v, 0, &v->ev, 1) < 0 ||
        setup_queue(v, 1, &v->st, 0) < 0) {
        w8(v->common, C_STATUS, 0x80u); /* failed */
        return 0;
    }
    w8(v->common, C_STATUS, S_ACK | S_DRIVER | S_FEATURES_OK | S_DRIVER_OK);
    oa_host_mb();
    *v->ev.notify = 0;

    has_rel = cfg_query(v, CFG_EV_BITS, EV_REL) != 0u;
    has_abs = cfg_query(v, CFG_EV_BITS, EV_ABS) != 0u;
    for (i = 0; i < 2u; i++) {
        if (has_abs && cfg_query(v, CFG_ABS_INFO, (oa_u8)i) >= 8u) {
            v->abs_min[i] = (oa_s32)r32(v->device, 8);
            v->abs_max[i] = (oa_s32)r32(v->device, 12);
        }
    }
    memcpy(v->name, has_abs ? "virtio tablet" : has_rel ? "virtio mouse" : "virtio keyboard", 16);
    v->src.poll = v_poll;
    v->src.set_leds = has_rel || has_abs ? 0 : v_leds;
    v->src.priv = v;
    if (oi_register(&v->src, v->name) < 0) {
        return 0;
    }
    nvdevs++;
    return 1;
}
