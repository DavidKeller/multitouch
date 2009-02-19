/*
 *  output.c
 *  irtouchd output function 
 *
 *  Created by David Keller on 09/11/08.
 *  Copyright 2008 EFREI. All rights reserved.
 *
 */

#include <stdlib.h>
#include <assert.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <peach.h>

#include <multitouch.h>

struct _output_t
{
    char * id;

    const mt_output_driver_t * driver;
    mt_output_driver_data_t * driver_data;

    pthread_t transmiting_thread;
    volatile int must_stop_transmiting;

    pthread_cond_t packet_available;
    pthread_mutex_t lock_packets;
    peach_list_t * packets_to_transmit;

    mt_chain_t * pre_processing_chain;
};

typedef struct
{
    char * from;
    mt_packet_t * packet;
}
_packet_handler_t;

static int
_transmiting_thread_run
            (mt_output_t * output);

static int
_transmiting_thread_stop
            (mt_output_t * output);

static int
_must_stop_transmiting
            (const mt_output_t * output);

static void *
_transmiting_thread
            (void * argument);

static int
_give_packet_to_driver
            (mt_output_t * output,
            const char * from,
            const mt_packet_t * packet);

static uint16_t 
_get_packets_to_transmit
            (mt_output_t * output,
            uint16_t requested_packets_count,
            _packet_handler_t * packet_handler);

static void
_packet_handler_destroy
            (_packet_handler_t * packet_handler);


mt_output_t *
mt_output_init
            (const char * output_id,
            const mt_output_driver_t * driver,
            const peach_hash_t * options)
{
    mt_output_t * output;

    assert(output_id != 0);
    assert(driver != 0);

    output = calloc(1, sizeof(*output));
    assert(output != 0);

    output->id = strdup(output_id);
    output->driver = driver;

    output->packets_to_transmit = peach_list_init();

    pthread_cond_init(&output->packet_available, 0);
    pthread_mutex_init(&output->lock_packets, 0);

    if ((*output->driver->init)(output, &output->driver_data, options) != 0)
        goto clean;

    output->pre_processing_chain = mt_chain_init(output,
                (mt_device_packet_process_t)_give_packet_to_driver);

    _transmiting_thread_run(output);

    return output;

clean:
    pthread_mutex_destroy(&output->lock_packets);
    pthread_cond_destroy(&output->packet_available);
    peach_list_destroy(output->packets_to_transmit, 
                (peach_list_clean_t)_packet_handler_destroy);
    free(output->id);
    free(output);

    return 0;
}

void
mt_output_destroy
            (mt_output_t * output)
{
    assert(output != 0);

    _transmiting_thread_stop(output);

    (*output->driver->destroy)(output, output->driver_data);

    peach_list_destroy(output->packets_to_transmit, 0);
    pthread_mutex_destroy(&output->lock_packets);
    pthread_cond_destroy(&output->packet_available);

    mt_chain_destroy(output->pre_processing_chain);

    free(output->id);
    free(output);
}

const char *
mt_output_get_id
            (const mt_output_t * output)
{
    assert(output != 0);

    return output->id;
}

extern int 
mt_output_push_pre_processing_engine
            (mt_output_t * output,
            const mt_chain_layer_driver_t * layer_driver,
            const peach_hash_t * options)
{
    return mt_chain_push_layer(output->pre_processing_chain, layer_driver, options);
}


int
mt_output_transmit
            (mt_output_t * output,
            const char * from,
            const mt_packet_t * packet)
{
    _packet_handler_t * packet_handler;

    packet_handler = malloc(sizeof(*packet_handler));
    assert(packet_handler != 0);

    packet_handler->from = strdup(from);
    packet_handler->packet = mt_packet_copy(packet);

    peach_log_debug(3, "Output '%s': queing packet from '%s'.\n",
            mt_output_get_id(output), from);

    pthread_mutex_lock(&output->lock_packets);

    peach_list_push_bottom(output->packets_to_transmit, packet_handler);
    pthread_cond_signal(&output->packet_available);

    pthread_mutex_unlock(&output->lock_packets);

    return 0;
}

static peach_hash_t * _drivers = 0;

void
mt_output_driver_loader_init(void)
{
    assert(_drivers == 0);

    _drivers = peach_hash_init(20);
}

void
mt_output_driver_loader_destroy(void)
{
    assert(_drivers != 0);

    peach_hash_destroy(_drivers, 0);
}

int
mt_output_driver_register
            (const char * name,
            const mt_output_driver_t * driver)
{
    assert(name != 0);
    assert(driver != 0);
    assert(_drivers != 0);
   
    peach_log_debug(1, "Output: registering '%s' driver.\n",
                name);
   
    return peach_hash_add(_drivers, name, strlen(name),
                (mt_output_driver_t *)driver);
}

int
mt_output_driver_unregister
            (const char * name)
{
    assert(name != 0);
    assert(_drivers != 0);

    peach_log_debug(1, "Output: unregistering '%s' driver.\n",
                name);
   
    if (peach_hash_remove(_drivers, name, strlen(name)) == 0)
        goto exit_with_failure;

    return 0;

exit_with_failure:
    return -1;
}

const mt_output_driver_t *
mt_output_driver_get
            (const char * name)
{
    assert(name != 0);

    return peach_hash_get(_drivers, name, strlen(name));
}

static int
_transmiting_thread_run
            (mt_output_t * output)
{
    pthread_attr_t transmiting_thread_attribute;
    int result;

    pthread_attr_init(&transmiting_thread_attribute);
    pthread_attr_setdetachstate(&transmiting_thread_attribute,
                PTHREAD_CREATE_JOINABLE);

    output->must_stop_transmiting = 0;

    if ((result = pthread_create(&output->transmiting_thread, 
                    &transmiting_thread_attribute, _transmiting_thread, 
                    output)) != 0) {
        char error_message_buffer [80];

        peach_log_debug(1, "Output '%s': could not create transmiting thread:"
                    " (%d\n", output->id, strerror_r(errno,
                    error_message_buffer, sizeof(error_message_buffer)));
    }

    pthread_attr_destroy(&transmiting_thread_attribute);

    return result;
}

static int
_transmiting_thread_stop
            (mt_output_t * output)
{
    output->must_stop_transmiting = 1;

    if (pthread_kill(output->transmiting_thread, SIGUSR1) != 0)
        goto exit_with_failure;

    if (pthread_cond_signal(&output->packet_available) != 0)
        goto exit_with_failure;

    if (pthread_join(output->transmiting_thread, 0) != 0)
        goto exit_with_failure;

    return 0;

exit_with_failure:
    return -1;
}


static int
_must_stop_transmiting
            (const mt_output_t * output)
{
    return output->must_stop_transmiting;
}

static void *
_transmiting_thread
            (void * argument)
{
    sigset_t blocked_signal;
    mt_output_t * output;
    _packet_handler_t packet_handler;

    output = argument;

    sigemptyset(&blocked_signal);
    sigaddset(&blocked_signal, SIGTERM);
    sigaddset(&blocked_signal, SIGKILL);

    if (pthread_sigmask(SIG_BLOCK, &blocked_signal, 0) != 0) {
        char error_message_buffer [80];

        peach_log_debug(1, "Output '%s': could not block SIGTERM & SIGKILL on the"
                    "transmiting thread: '%s'\n", output->id, strerror_r(errno,
                    error_message_buffer, sizeof(error_message_buffer)));

        goto exit;
    }

    while(_get_packets_to_transmit(output, 1, &packet_handler) == 1) {

        mt_chain_transmit(output->pre_processing_chain, packet_handler.from,
                    packet_handler.packet);

        free(packet_handler.from);
        mt_packet_destroy(packet_handler.packet);
    }

exit:
    pthread_exit(0);
}

static int
_give_packet_to_driver
            (mt_output_t * output,
            const char * from,
            const mt_packet_t * packet)
{
    return (*output->driver->transmit)(output, output->driver_data, 
            from, packet);
}

static uint16_t
_get_packets_to_transmit
            (mt_output_t * output,
            uint16_t requested_packets_count,
            _packet_handler_t * packet_handlers)
{
    uint16_t packets_count;

    pthread_mutex_lock(&output->lock_packets);

    for (packets_count = 0; _must_stop_transmiting(output) == 0 
                && requested_packets_count > 0; ) {

        _packet_handler_t * current_packet_handler;

        if ((current_packet_handler = 
                    peach_list_pop_top(output->packets_to_transmit)) != 0) {

            packet_handlers->from = current_packet_handler->from;
            packet_handlers->packet = current_packet_handler->packet;

            free(current_packet_handler);

            requested_packets_count --;
            packets_count ++;
            packet_handlers ++;
        }
        else 
            pthread_cond_wait(&output->packet_available, 
                        &output->lock_packets);
    }

    pthread_mutex_unlock(&output->lock_packets);

    return packets_count;
}

static void
_packet_handler_destroy
            (_packet_handler_t * packet_handler)
{
    free(packet_handler->from);
    mt_packet_destroy(packet_handler->packet);
    free(packet_handler);
}
