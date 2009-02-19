/*
 *  packet.c
 *  irtouchd packet function 
 *
 *  Created by David Keller on 09/11/08.
 *  Copyright 2008 EFREI. All rights reserved.
 *
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <multitouch.h>

mt_packet_t *
mt_packet_init_event
            (mt_event_t * event,
            void (*destructor)(mt_event_t * event))
{
    mt_packet_t * packet;

    packet = calloc(1, sizeof(*packet));
    assert(packet != 0);

    packet->type = PACKET_EVENT;
    packet->content.event.event = event;
    packet->content.event.destructor = destructor;

    return packet;
}

mt_packet_t *
mt_packet_init_raw
            (void * data,
            size_t length,
            void (*destructor)(void * data))
{
    mt_packet_t * packet;

    packet = calloc(1, sizeof(*packet));
    assert(packet != 0);

    packet->type = PACKET_RAW;
    packet->content.raw.data = data;
    packet->content.raw.length = length;
    packet->content.raw.destructor = destructor;

    return packet;
}

void
mt_packet_destroy
            (mt_packet_t * packet)
{
    assert(packet != 0);

    if (packet->type == PACKET_RAW) {
        if (packet->content.raw.destructor != 0)
            (*packet->content.raw.destructor)(packet->content.raw.data);
    } else {
        if (packet->content.event.destructor != 0)
            (*packet->content.event.destructor)(packet->content.event.event);
    }

    free(packet);
}

const void *
mt_packet_serialize
            (const mt_packet_t * packet)
{
    const void * data;

    assert(packet != 0);

    if (packet->type == PACKET_EVENT)
        data = packet->content.event.event;
    else
        data = packet->content.raw.data;

    return data;
}

size_t
mt_packet_get_length
            (const mt_packet_t * packet)
{
    size_t length;

    assert(packet != 0);

    if (packet->type == PACKET_EVENT)
        length = sizeof(packet->content.event.event->info) 
                + sizeof(*packet->content.event.event->touchset) 
                * packet->content.event.event->info.touch_count;
    else
        length = packet->content.raw.length;

    return length;
}

mt_packet_t *
mt_packet_copy
            (const mt_packet_t * packet)
{
    mt_packet_t * packet_copy;

    assert(packet != 0);

    packet_copy = malloc(sizeof(*packet_copy));
    assert(packet_copy != 0);

    packet_copy->type = packet->type;
    if (packet->type == PACKET_EVENT) {
        packet_copy->content.event.event 
                = mt_event_copy(packet->content.event.event);
        packet_copy->content.event.destructor = mt_event_destroy;
    } else { 
        packet_copy->content.raw.data = malloc(packet->content.raw.length);
        assert(packet_copy->content.raw.data != 0);

        packet_copy->content.raw.length = packet->content.raw.length;

        memcpy(packet_copy->content.raw.data, packet->content.raw.data,
                packet->content.raw.length);

        packet_copy->content.raw.destructor = free;
    }

    return packet_copy;
}
