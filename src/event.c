/* SPDX-License-Identifier: GPL-2.0-or-later */
/* openinput: the sources and the event queue. */

#include "openinput/input.h"

#define MAX_SOURCES 16
#define QUEUE 256u

static struct oi_source *sources[MAX_SOURCES];
static unsigned nsources;
static struct oi_event queue[QUEUE];
static unsigned head, tail;

int oi_register(struct oi_source *src, const char *name)
{
    if (nsources == MAX_SOURCES) {
        return -1;
    }
    src->name = name;
    src->index = (oa_u8)nsources;
    sources[nsources++] = src;
    return 0;
}

void oi_emit(struct oi_source *src, oa_u16 page, oa_u16 usage, oa_s32 value)
{
    struct oi_event *e;

    if (((tail + 1u) % QUEUE) == head) {
        return; /* full: drop */
    }
    e = &queue[tail];
    e->page = page;
    e->usage = usage;
    e->value = value;
    e->source = src->index;
    tail = (tail + 1u) % QUEUE;
}

void oi_poll(void)
{
    unsigned i;

    for (i = 0; i < nsources; i++) {
        sources[i]->poll(sources[i]);
    }
}

int oi_read(struct oi_event *ev)
{
    if (head == tail) {
        return 0;
    }
    *ev = queue[head];
    head = (head + 1u) % QUEUE;
    return 1;
}

void oi_set_leds(unsigned leds)
{
    unsigned i;

    for (i = 0; i < nsources; i++) {
        if (sources[i]->set_leds) {
            sources[i]->set_leds(sources[i], leds);
        }
    }
}

unsigned oi_source_count(void)
{
    return nsources;
}

const char *oi_source_name(unsigned source)
{
    return source < nsources ? sources[source]->name : "";
}
