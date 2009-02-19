/**
 * Created by David Keller on 09/11/08.
 * Copyright 2008 EFREI. All rights reserved.
 */

#ifndef _MULTITOUCH_H_
#define _MULTITOUCH_H_

#include <stdint.h>
#include <peach.h>
#include <time.h>

typedef struct
{
    double timestamp;
    uint32_t tap_count;
    enum
    {
        INPUT_TOUCH_BEGAN,
        INPUT_TOUCH_MOVED,
        INPUT_TOUCH_STATIONARY,
        INPUT_TOUCH_KEEP_ALIVE
    }
    phase;
    struct
    {
        struct
        {
            double x;
            double y;
        }
        origin;

        struct
        {
            double width;
            double height;
        }
        size;
    }
    where;
}
mt_event_touch_t;

typedef struct
{
    struct
    {
        uint32_t flags;
        double timestamp;

        uint16_t touch_count;
    }
    info;

    mt_event_touch_t touchset [];
}
mt_event_t;

extern mt_event_t *
mt_event_init
            (uint16_t touch_count);

extern void
mt_event_destroy
            (mt_event_t * event);

extern const void *
mt_event_serialize
            (const mt_event_t * event);

extern size_t
mt_event_get_length
            (const mt_event_t * event);

extern mt_event_t *
mt_event_copy
            (const mt_event_t * event);

            
typedef struct
{
    enum
    {
        PACKET_EMPTY,
        PACKET_EVENT,
        PACKET_RAW
    }
    type;

    union
    {
        struct
        {
            size_t length;
            void * data;
            void (*destructor)(void * data);
        }
        raw;
        struct
        {
            mt_event_t * event;
            void (*destructor)(mt_event_t * event);
        }
        event;
    }
    content;
}
mt_packet_t;

extern mt_packet_t *
mt_packet_init_event
            (mt_event_t * event,
            void (*destructor)(mt_event_t * event));

extern mt_packet_t *
mt_packet_init_raw
            (void * data,
            size_t length,
            void (*destructor)(void * data));


extern void
mt_packet_destroy
            (mt_packet_t * packet);

extern const void *
mt_packet_serialize
            (const mt_packet_t * packet);

extern size_t
mt_packet_get_length
            (const mt_packet_t * packet);

extern mt_packet_t *
mt_packet_copy
            (const mt_packet_t * packet);


typedef int
(*mt_device_packet_process_t)
            (void * data,
            const char * from,
            const mt_packet_t * packet);



typedef struct _chain_t mt_chain_t;

typedef struct _chain_layer_t mt_chain_layer_t;

typedef struct _chain_layer_driver_data_t mt_chain_layer_driver_data_t;

typedef int
(*mt_chain_driver_accept_t)
            (mt_chain_layer_t * layer,
            const char * from,
            const mt_packet_t * packet);

typedef struct 
{
    int 
    (*init)
                (mt_chain_layer_driver_data_t ** driver_data, 
                const peach_hash_t * options);

    int 
    (*destroy)
                (mt_chain_layer_driver_data_t * driver_data);

    int
    (*process)
                (mt_chain_layer_t * layer,
                mt_chain_layer_driver_data_t * driver_data,
                const char * from,
                const mt_packet_t * packet,
                mt_chain_driver_accept_t accept);
}
mt_chain_layer_driver_t;


extern mt_chain_t *
mt_chain_init(void * listener, mt_device_packet_process_t listener_process);

extern void 
mt_chain_destroy
            (mt_chain_t * chain);

extern int 
mt_chain_push_layer
            (mt_chain_t * chain,
            const mt_chain_layer_driver_t * layer_driver,
            const peach_hash_t * options);

extern int
mt_chain_pop_layer(mt_chain_t * chain);

extern int 
mt_chain_transmit
            (mt_chain_t * chain,
            const char * from,
            const mt_packet_t * packet);

extern void
mt_chain_layer_driver_loader_init(void);

extern void
mt_chain_layer_driver_loader_destroy(void);

extern int
mt_chain_layer_driver_register
            (const char * name,
            const mt_chain_layer_driver_t * layer_driver);

extern int
mt_chain_layer_driver_unregister
            (const char * name);


extern const mt_chain_layer_driver_t *
mt_chain_layer_driver_get
            (const char * name);

typedef struct _input_t mt_input_t;

typedef struct _input_driver_data_t mt_input_driver_data_t;

typedef int
(*mt_input_driver_commit_t)
            (const mt_input_t * input,
            const mt_packet_t * packet);

typedef int
(*mt_input_driver_must_stop_polling_t)
            (const mt_input_t * input);

typedef struct 
{
    int 
    (*init)
                (const mt_input_t * input,
                mt_input_driver_data_t ** driver_data, 
                const peach_hash_t * options);

    int 
    (*destroy)
                (const mt_input_t * input,
                mt_input_driver_data_t * driver_data);

    int
    (*run)
                (const mt_input_t * input,
                mt_input_driver_data_t * driver_data,
                mt_input_driver_commit_t driver_commit,
                mt_input_driver_must_stop_polling_t must_stop_polling_on);
}
mt_input_driver_t;


extern mt_input_t *
mt_input_init
            (const char * input_id,
            const mt_input_driver_t * driver,
            const peach_hash_t * options);

extern void 
mt_input_destroy
            (mt_input_t * input);

extern const char *
mt_input_get_id
            (const mt_input_t * input);

extern int
mt_input_polling_start(mt_input_t * input);

extern int
mt_input_polling_stop(mt_input_t * input);

extern int 
mt_input_push_post_processing_engine
            (mt_input_t * input,
            const mt_chain_layer_driver_t * layer_driver,
            const peach_hash_t * options);

extern void
mt_input_bind
            (mt_input_t * input,
            mt_device_packet_process_t process,
            void * data);

extern void
mt_input_driver_loader_init(void);

extern void
mt_input_driver_loader_destroy(void);

extern int
mt_input_driver_register
            (const char * name,
            const mt_input_driver_t * driver);

extern int
mt_input_driver_unregister
            (const char * name);


extern const mt_input_driver_t *
mt_input_driver_get
            (const char * name);

typedef struct _output_t mt_output_t;

typedef struct _output_driver_data_t mt_output_driver_data_t;

typedef struct 
{
    int 
    (*init)
                (const mt_output_t * output,
                mt_output_driver_data_t ** driver_data, 
                const peach_hash_t * options);

    int 
    (*destroy)
                (const mt_output_t * output,
                mt_output_driver_data_t * driver_data);

    int
    (*transmit)
                (const mt_output_t * output,
                mt_output_driver_data_t * driver_data,
                const char * from,
                const mt_packet_t * packet);
}
mt_output_driver_t;

extern mt_output_t *
mt_output_init
            (const char * output_id,
            const mt_output_driver_t * driver,
            const peach_hash_t * options);

extern void 
mt_output_destroy
            (mt_output_t * output);

extern const char *
mt_output_get_id
            (const mt_output_t * output);

extern int 
mt_output_push_pre_processing_engine
            (mt_output_t * output,
            const mt_chain_layer_driver_t * layer_driver,
            const peach_hash_t * options);


extern int
mt_output_transmit
            (mt_output_t * output,
            const char * from,
            const mt_packet_t * packet);
/**
 *
 *
 */
extern void
mt_output_driver_loader_init(void);

/**
 *
 *
 *
 */
extern void
mt_output_driver_loader_destroy(void);

/**
 *
 *
 *
 */
extern int
mt_output_driver_register
            (const char * name,
            const mt_output_driver_t * driver);

/**
 *
 *
 *
 */
extern int
mt_output_driver_unregister
            (const char * name);

/**
 *
 *
 *
 */
extern const mt_output_driver_t *
mt_output_driver_get
            (const char * name);


#endif
