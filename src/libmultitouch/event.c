/*
 *  event.c
 *  irtouchd event function 
 *
 *  Created by David Keller on 09/11/08.
 *  Copyright 2008 EFREI. All rights reserved.
 *
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <multitouch.h>

mt_event_t *
mt_event_init
            (uint16_t touch_count)
{
    mt_event_t * event;

    event = calloc(1, sizeof(event->info) 
                + sizeof(*event->touchset) * touch_count);
    assert(event != 0);

    event->info.touch_count = touch_count;

    return event;
}

void
mt_event_destroy
            (mt_event_t * event)
{
    assert(event != 0);

    free(event);
}

const void *
mt_event_serialize
            (const mt_event_t * event)
{
    return event;
}

size_t
mt_event_get_length
            (const mt_event_t * event)
{
    return sizeof(event->info) 
                + sizeof(*event->touchset) * event->info.touch_count;
}

mt_event_t *
mt_event_copy
            (const mt_event_t * event)
{
    mt_event_t * event_copy;

    assert(event != 0);

    event_copy = mt_event_init(event->info.touch_count);
    memcpy(event_copy, event, mt_event_get_length(event));

    return event_copy;
}
