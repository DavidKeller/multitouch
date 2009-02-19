/*
 *  input.c
 *  irtouchd input function 
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

struct _input_t
{
    char * id;

    enum
    {
        INPUT_POLLING_STARTED,
        INPUT_POLLING_STOPPED
    }
    state;

    const mt_input_driver_t * driver;
    mt_input_driver_data_t * driver_data;

    mt_device_packet_process_t packet_process;
    void * extra_data;

    pthread_t polling_thread;
    int driver_must_stop_polling;

    peach_set_t * listeners;
    pthread_mutex_t listeners_lock;

    mt_chain_t * post_processing_chain;
};

typedef struct
{
    mt_device_packet_process_t process;
    void * data;
}
_listener_t;


static int
_polling_thread_run
            (mt_input_t * input);

static int
_polling_thread_stop
            (mt_input_t * input);

static int
_input_driver_commit
            (const mt_input_t * input,
            const mt_packet_t * packet);

static int
_give_packet_to_listeners
            (const mt_input_t * input,
            const char * from,
            const mt_packet_t * packet);

static int
_give_packet_to_listener
            (_listener_t * listener,
            va_list arguments);

static int
_driver_must_stop_polling
            (const mt_input_t * input);

static void *
_polling_thread
            (void * argument);

static void
_lock_listeners
            (const mt_input_t * input);

static void
_unlock_listeners
            (const mt_input_t * input);


mt_input_t *
mt_input_init
            (const char * input_id,
            const mt_input_driver_t * driver,
            const peach_hash_t * options)
{
    mt_input_t * input;

    assert(input_id != 0);
    assert(driver != 0);

    input = calloc(1, sizeof(*input));
    assert(input != 0);

    input->state = INPUT_POLLING_STOPPED;

    input->id = strdup(input_id);
    input->driver = driver;
    pthread_mutex_init(&input->listeners_lock, 0);

    if ((*input->driver->init)(input, &input->driver_data,
                options) != 0)
        goto clean;

    input->listeners = peach_set_init(10, 4);

    input->post_processing_chain = mt_chain_init(input,
                (mt_device_packet_process_t)_give_packet_to_listeners);

    _polling_thread_run(input);

    return input;

clean:
    pthread_mutex_destroy(&input->listeners_lock);
    free(input->id);
    free(input);

    return 0;
}

void
mt_input_destroy
            (mt_input_t * input)
{
    assert(input != 0);

    if (input->state == INPUT_POLLING_STARTED)
        _polling_thread_stop(input);

    (*input->driver->destroy)(input, input->driver_data);

    peach_set_destroy(input->listeners, (peach_set_clean_t)free);

    free(input->id);
    pthread_mutex_destroy(&input->listeners_lock);

    mt_chain_destroy(input->post_processing_chain);
    free(input);
}

int
mt_input_polling_start(mt_input_t * input)
{
    if (input->state == INPUT_POLLING_STARTED) 
        goto exit_with_failure;

    _polling_thread_run(input);

    return 0;

exit_with_failure:
    return -1;
}

int
mt_input_polling_stop(mt_input_t * input)
{
    if (input->state == INPUT_POLLING_STOPPED)
        goto exit_with_failure;

    _polling_thread_stop(input);

exit_with_failure:
    return -1;
}

const char *
mt_input_get_id
            (const mt_input_t * input)
{
    assert(input != 0);

    return input->id;
}

int 
mt_input_push_post_processing_engine
            (mt_input_t * input,
            const mt_chain_layer_driver_t * layer_driver,
            const peach_hash_t * options)
{
    if (input->state == INPUT_POLLING_STARTED) {

        peach_log_debug(1, "Input '%s': could not push post processing engine "
                "while input is polling.",
                mt_input_get_id(input));

        goto exit_with_failure;
    }

    return mt_chain_push_layer(input->post_processing_chain, 
                layer_driver, options);

exit_with_failure:
    return -1;
}

void
mt_input_bind
            (mt_input_t * input,
            mt_device_packet_process_t process,
            void * data)
{
    _listener_t * listener;
    assert(process != 0);

    listener = malloc(sizeof(*listener));
    assert(listener != 0);

    listener->process = process;
    listener->data = data;

    _lock_listeners(input);
    peach_set_add(input->listeners, listener);
    _unlock_listeners(input);
}

static peach_hash_t * _drivers = 0;

void
mt_input_driver_loader_init(void)
{
    assert(_drivers == 0);

    _drivers = peach_hash_init(20);
}

void
mt_input_driver_loader_destroy(void)
{
    assert(_drivers != 0);

    peach_hash_destroy(_drivers, 0);
}

int
mt_input_driver_register
            (const char * name,
            const mt_input_driver_t * driver)
{
    assert(name != 0);
    assert(driver != 0);
    assert(_drivers != 0);

    peach_log_debug(1, "Input: registering '%s' driver.\n",
                name);
   
    return peach_hash_add(_drivers, name, strlen(name),
                (mt_input_driver_t *)driver);
}

int
mt_input_driver_unregister
            (const char * name)
{
    assert(name != 0);
    assert(_drivers != 0);

    peach_log_debug(1, "Input: unregistering '%s' driver.\n",
                name);
   
    if (peach_hash_remove(_drivers, name, strlen(name)) == 0)
        goto exit_with_failure;

    return 0;

exit_with_failure:
    return -1;
}

const mt_input_driver_t *
mt_input_driver_get
            (const char * name)
{
    assert(name != 0);

    return peach_hash_get(_drivers, name, strlen(name));
}

static int
_polling_thread_run
            (mt_input_t * input)
{
    pthread_attr_t polling_thread_attribute;
    int result;

    pthread_attr_init(&polling_thread_attribute);
    pthread_attr_setdetachstate(&polling_thread_attribute,
                PTHREAD_CREATE_JOINABLE);

    input->driver_must_stop_polling = 0;

    if ((result = pthread_create(&input->polling_thread, 
                &polling_thread_attribute, _polling_thread, input)) 
                != 0) {
        char error_message_buffer [80];

        peach_log_debug(1, "Input '%s': could not create polling thread: (%d\n", 
                    input->id, strerror_r(errno, error_message_buffer, 
                    sizeof(error_message_buffer)));
    }

    pthread_attr_destroy(&polling_thread_attribute);

    input->state = INPUT_POLLING_STARTED;

    return result;
}

static int
_polling_thread_stop
            (mt_input_t * input)
{
    input->driver_must_stop_polling = 1;

    if (pthread_kill(input->polling_thread, SIGUSR1) != 0)
        goto exit_with_failure;

    if (pthread_join(input->polling_thread, 0) != 0)
        goto exit_with_failure;

    input->state = INPUT_POLLING_STOPPED;

    return 0;

exit_with_failure:
    return -1;
}

static int
_input_driver_commit
            (const mt_input_t * input,
            const mt_packet_t * packet)
{
    return mt_chain_transmit(input->post_processing_chain, 
                mt_input_get_id(input), packet);
}

static int
_give_packet_to_listeners
            (const mt_input_t * input,
            const char * from,
            const mt_packet_t * packet)
{
    int result;

    _lock_listeners(input);

    result = peach_set_foreach(input->listeners, 
                (peach_set_predicate_t)_give_packet_to_listener, from, packet);

    _unlock_listeners(input);

    return result;
}

static int
_give_packet_to_listener
            (_listener_t * listener,
            va_list arguments)
{
    const char * input_id;
    const mt_packet_t * packet;

    input_id = va_arg(arguments, const char *);
    packet = va_arg(arguments, const mt_packet_t *);

    peach_log_debug(3, "Input '%s': sending packet to listener.\n",
                input_id);

    return (*listener->process)(listener->data, input_id, packet);
}


static int
_driver_must_stop_polling
            (const mt_input_t * input)
{
    return input->driver_must_stop_polling;
}

static void *
_polling_thread
            (void * argument)
{
    sigset_t blocked_signal;
    mt_input_t * input;

    input = argument;

    sigemptyset(&blocked_signal);
    sigaddset(&blocked_signal, SIGTERM);
    sigaddset(&blocked_signal, SIGKILL);

    if (pthread_sigmask(SIG_BLOCK, &blocked_signal, 0) != 0) {
        char error_message_buffer [80];
        peach_log_debug(1, "Input '%s': could not block SIGTERM & SIGKILL on "
                    "the polling thread: '%s'\n", input->id, strerror_r(errno, 
                    error_message_buffer, sizeof(error_message_buffer)));

        goto exit;
    }

    (*input->driver->run)(input, input->driver_data, _input_driver_commit,
                _driver_must_stop_polling);

exit:
    pthread_exit(0);
}

static void
_lock_listeners
            (const mt_input_t * input)
{
    pthread_mutex_lock(&((mt_input_t *)input)->listeners_lock);
}

static void
_unlock_listeners
            (const mt_input_t * input)
{
    pthread_mutex_unlock(&((mt_input_t *)input)->listeners_lock);
}

