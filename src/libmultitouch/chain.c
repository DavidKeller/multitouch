/*
 *  chain.c
 *  irtouchd chain function 
 *
 *  Created by David Keller on 09/11/08.
 *  Copyright 2008 EFREI. All rights reserved.
 *
 */

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <peach.h>

#include <multitouch.h>

struct _chain_t
{
    peach_stack_t * layers_stack;

    struct
    {
        void * data;
        mt_device_packet_process_t process;
    }
    listener;
};

typedef union 
{
    struct
    {
        mt_chain_t * chain;
        int (*process)(mt_chain_t * chain, const char * from, 
                const mt_packet_t * packet);
    }
    chain;

    struct
    {
        mt_chain_layer_t * layer;
        int (*process)(mt_chain_layer_t * layer, 
                const char * from, const mt_packet_t * packet);
    }
    layer;
}
_upper_layer_t;

struct _chain_layer_t
{
    mt_chain_layer_driver_data_t * driver_data;
    const mt_chain_layer_driver_t * driver;

    _upper_layer_t upper_layer;
};

static int
_give_packet_to_listener
            (mt_chain_t * chain,
            const char * from,
            const mt_packet_t * packet);

static void
_layer_destroy
            (mt_chain_layer_t * chain_layer);

static int
_to_upper_layer
            (mt_chain_layer_t * layer,
            const char * from,
            const mt_packet_t * packet);
static int
_process_packet
            (mt_chain_layer_t * layer,
            const char * from,
            const mt_packet_t * packet);

static int 
_default_driver_init
            (mt_chain_layer_driver_data_t ** driver_data, 
            const peach_hash_t * options);

static int 
_default_driver_destroy
            (mt_chain_layer_driver_data_t * driver_data);

static int
_default_driver_process
            (mt_chain_layer_t * layer,
            mt_chain_layer_driver_data_t * driver_data,
            const char * from,
            const mt_packet_t * packet,
            mt_chain_driver_accept_t process);


static peach_hash_t * _layer_drivers = 0;
static mt_chain_layer_driver_t _default_driver =
{
    .init = _default_driver_init,
    .destroy = _default_driver_destroy,
    .process = _default_driver_process
};


mt_chain_t *
mt_chain_init(void * listener, mt_device_packet_process_t listener_process)
{
    mt_chain_t * chain;
    mt_chain_layer_t * default_layer;

    chain = malloc(sizeof(*chain));
    assert(chain != 0);

    chain->layers_stack = peach_stack_init(1, 4);

    default_layer = malloc(sizeof(*default_layer));
    assert(default_layer != 0);

    default_layer->driver = &_default_driver;
    default_layer->upper_layer.chain.chain = chain;
    default_layer->upper_layer.chain.process = _give_packet_to_listener;

    peach_stack_push(chain->layers_stack, default_layer);

    chain->listener.data = listener;
    chain->listener.process = listener_process;

    return chain;
}

void 
mt_chain_destroy
            (mt_chain_t * chain)
{
    peach_stack_destroy(chain->layers_stack, (peach_stack_clean_t)_layer_destroy);

    free(chain);
}

int
mt_chain_transmit
            (mt_chain_t * chain,
            const char * from,
            const mt_packet_t * packet)
{
    mt_chain_layer_t * heighest_layer;

    heighest_layer = peach_stack_top(chain->layers_stack);

    return _process_packet(heighest_layer, from, packet);
}

int
mt_chain_push_layer
            (mt_chain_t * chain,
            const mt_chain_layer_driver_t * layer_driver,
            const peach_hash_t * options)
{
    mt_chain_layer_t * layer;

    assert(chain != 0);
    assert(layer_driver != 0);

    layer = malloc(sizeof(*layer));
    assert(layer != 0);

    layer->driver = layer_driver;
    layer->upper_layer.layer.layer 
                = (mt_chain_layer_t *)peach_stack_top(chain->layers_stack);
    layer->upper_layer.layer.process = _process_packet;

    if ((*layer_driver->init)(&layer->driver_data, options) != 0)
        goto clean;

    peach_stack_push(chain->layers_stack, layer);

    return 0;

clean:
    free(layer);

    return -1;
}

int
mt_chain_pop_layer(mt_chain_t * chain)
{
    if (peach_stack_get_length(chain->layers_stack) == 1)
        goto exit_with_failure;

    _layer_destroy(peach_stack_pop(chain->layers_stack));

exit_with_failure:
    return -1;
}

void
mt_chain_layer_driver_loader_init(void)
{
    assert(_layer_drivers == 0);

    _layer_drivers = peach_hash_init(20);
}

void
mt_chain_layer_driver_loader_destroy(void)
{
    assert(_layer_drivers != 0);

    peach_hash_destroy(_layer_drivers, 0);
}

int
mt_chain_layer_driver_register
            (const char * name,
            const mt_chain_layer_driver_t * layer_driver)
{
    assert(name != 0);
    assert(layer_driver != 0);
    assert(_layer_drivers != 0);
   
    peach_log_debug(1, "Chain: registering '%s' driver.\n",
                name);
   
    return peach_hash_add(_layer_drivers, name, strlen(name),
                (mt_chain_layer_driver_t *)layer_driver);
}

int
mt_chain_layer_driver_unregister
            (const char * name)
{
    assert(name != 0);
    assert(_layer_drivers != 0);

    peach_log_debug(1, "Chain: unregistering '%s' driver.\n",
                name);
   
    if (peach_hash_remove(_layer_drivers, name, strlen(name)) == 0)
        goto exit_with_failure;

    return 0;

exit_with_failure:
    return -1;
}

const mt_chain_layer_driver_t *
mt_chain_layer_driver_get
            (const char * name)
{
    assert(name != 0);

    return peach_hash_get(_layer_drivers, name, strlen(name));
}

static int
_to_upper_layer
            (mt_chain_layer_t * layer,
            const char * from,
            const mt_packet_t * packet)
{
    return (*layer->upper_layer.layer.process)(layer->upper_layer.layer.layer,
                from, packet);
}

static int
_process_packet
            (mt_chain_layer_t * layer,
            const char * from,
            const mt_packet_t * packet)
{
    return (*layer->driver->process)(layer, layer->driver_data, from, packet,
                _to_upper_layer);
}


static int
_give_packet_to_listener
            (mt_chain_t * chain,
            const char * from,
            const mt_packet_t * packet)
{
    return (*chain->listener.process)(chain->listener.data, from, packet);
}


static void
_layer_destroy
            (mt_chain_layer_t * layer)
{
   (*layer->driver->destroy)(layer->driver_data);
   free(layer);
}

static int 
_default_driver_init
            (mt_chain_layer_driver_data_t ** driver_data, 
            const peach_hash_t * options)
{
    return 0;
}

static int 
_default_driver_destroy
            (mt_chain_layer_driver_data_t * layer_driver_data)
{
    return 0;
}

static int
_default_driver_process
            (mt_chain_layer_t * layer,
            mt_chain_layer_driver_data_t * driver_data,
            const char * from,
            const mt_packet_t * packet,
            mt_chain_driver_accept_t accept)
{
    return (*accept)(layer, from, packet);
}

