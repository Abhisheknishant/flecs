#ifndef FLECS_TYPES_PRIVATE_H
#define FLECS_TYPES_PRIVATE_H

#include <stdlib.h>

#include "../flecs.h"
#include "../util/array.h"
#include "../util/map.h"

#define ECS_WORLD_INITIAL_TABLE_COUNT (2)
#define ECS_WORLD_INITIAL_ENTITY_COUNT (2)
#define ECS_WORLD_INITIAL_STAGING_COUNT (0)
#define ECS_WORLD_INITIAL_COL_SYSTEM_COUNT (1)
#define ECS_WORLD_INITIAL_OTHER_SYSTEM_COUNT (0)
#define ECS_WORLD_INITIAL_ADD_SYSTEM_COUNT (0)
#define ECS_WORLD_INITIAL_REMOVE_SYSTEM_COUNT (0)
#define ECS_WORLD_INITIAL_SET_SYSTEM_COUNT (0)
#define ECS_WORLD_INITIAL_PREFAB_COUNT (0)
#define ECS_MAP_INITIAL_NODE_COUNT (4)
#define ECS_TABLE_INITIAL_ROW_COUNT (0)
#define ECS_SYSTEM_INITIAL_TABLE_COUNT (0)
#define ECS_MAX_JOBS_PER_WORKER (16)

#define ECS_WORLD_MAGIC (0x65637377)
#define ECS_THREAD_MAGIC (0x65637374)


/* -- Builtin component types -- */

/** Metadata of an explicitly created type (identified by an entity id) */
typedef struct EcsTypeComponent {
    ecs_type_t type;    /* Preserved nested families */
    ecs_type_t resolved;  /* Resolved nested families */
} EcsTypeComponent;

/** Metadata of a component */
typedef struct EcsComponent {
    uint32_t size;
} EcsComponent;

/** Type that is used by systems to indicate where to fetch a component from */
typedef enum EcsSystemExprElemKind {
    EcsFromSelf,            /* Get component from self (default) */
    EcsFromContainer,       /* Get component from container */
    EcsFromSystem,          /* Get component from system */
    EcsFromId,              /* Get entity handle by id */
    EcsFromSingleton,       /* Get singleton component */
    EcsFromEntity           /* Get component from other entity */
} EcsSystemExprElemKind;

/** Type describing an operator used in an signature of a system signature */
typedef enum EcsSystemExprOperKind {
    EcsOperAnd = 0,
    EcsOperOr = 1,
    EcsOperNot = 2,
    EcsOperOptional = 3,
    EcsOperLast = 4
} EcsSystemExprOperKind;

/** Callback used by the system signature expression parser */
typedef int (*ecs_parse_action_t)(
    ecs_world_t *world,
    EcsSystemExprElemKind elem_kind,
    EcsSystemExprOperKind oper_kind,
    const char *component,
    const char *source,
    void *ctx);

/** Type that describes a single column in the system signature */
typedef struct ecs_system_column_t {
    EcsSystemExprElemKind kind;       /* Element kind (Entity, Component) */
    EcsSystemExprOperKind oper_kind;  /* Operator kind (AND, OR, NOT) */
    union {
        ecs_type_t type;             /* Used for OR operator */
        ecs_entity_t component;      /* Used for AND operator */
    } is;
    ecs_entity_t source;            /* Source entity (used with FromEntity) */
} ecs_system_column_t;

/** Type that stores a reference to components of external entities (prefabs) */
typedef struct ecs_system_ref_t {
    ecs_entity_t entity;
    ecs_entity_t component;
} ecs_system_ref_t;

/** Base type for a system */
typedef struct EcsSystem {
    ecs_system_action_t action;    /* Callback to be invoked for matching rows */
    const char *signature;         /* Signature with which system was created */
    ecs_array_t *columns;          /* Column components */
    ecs_type_t not_from_entity;    /* Exclude components from entity */
    ecs_type_t not_from_component; /* Exclude components from components */
    ecs_type_t and_from_entity;   /* Which components are required from entity */
    ecs_type_t and_from_system;    /* Used to auto-add components to system */
    EcsSystemKind kind;            /* Kind of system */
    float time_spent;              /* Time spent on running system */
    bool enabled;                  /* Is system enabled or not */
} EcsSystem;

/** A column system is a system that is ran periodically (default = every frame)
 * on all entities that match the system signature expression. Column systems
 * are prematched with tables (archetypes) that match the system signature
 * expression. Each time a column system is invoked, it iterates over the 
 * matched list of tables (the 'tables' member). 
 *
 * For each table, the system stores a list of the components that were matched
 * with the system. This list may differ from the component list of the table,
 * when OR expressions or optional expressions are used.
 * 
 * A column system keeps track of tables that are empty. These tables are stored
 * in the 'inactive_tables' array. This prevents the system from iterating over
 * tables in the main loop that have no data.
 * 
 * For each table, a column system stores an index that translates between the
 * a column in the system signature, and the matched table. This information is
 * stored, alongside with an index that identifies the table, in the 'tables'
 * member. This is an array of an array of integers, per table.
 * 
 * Additionally, the 'tables' member contains information on where a component
 * should be fetched from. By default, components are fetched from an entity,
 * but a system may specify that a component must be resolved from a container,
 * or must be fetched from a prefab. In this case, the index to lookup a table
 * column from system column contains a negative number, which identifies an
 * element in the 'refs' array.
 * 
 * The 'refs' array contains elements of type 'EcsRef', and stores references
 * to external entities. References can vary per table, but not per entity/row,
 * as prefabs / containers are part of the entity type, which in turn 
 * identifies the table in which the entity is stored.
 * 
 * The 'period' and 'time_passed' members are used for periodic systems. An
 * application may specify that a system should only run at a specific interval, 
 * like once per second. This interval is stored in the 'period' member. Each
 * time the system is evaluated but not ran, the delta_time is added to the 
 * time_passed member, until it exceeds 'period'. In that case, the system is
 * ran, and 'time_passed' is decreased by 'period'. 
 */
typedef struct EcsColSystem {
    EcsSystem base;
    ecs_entity_t entity;          /* Entity id of system, used for ordering */
    ecs_array_t *components;      /* Computed component list per matched table */
    ecs_array_t *inactive_tables; /* Inactive tables */
    ecs_array_t *jobs;            /* Jobs for this system */
    ecs_array_t *tables;          /* Table index + refs index + column offsets */
    ecs_array_t *refs;            /* Columns that point to other entities */
    ecs_array_params_t table_params; /* Parameters for tables array */
    ecs_array_params_t component_params; /* Parameters for components array */
    ecs_array_params_t ref_params; /* Parameters for tables array */
    float period;              /* Minimum period inbetween system invocations */
    float time_passed;         /* Time passed since last invocation */
} EcsColSystem;

/** A row system is a system that is ran on 1..n entities for which a certain 
 * operation has been invoked. The system kind determines on what kind of
 * operation the row system is invoked. Example operations are ecs_add,
 * ecs_remove and ecs_set. */
typedef struct EcsRowSystem {
    EcsSystem base;
    ecs_array_t *components;       /* Components in order of signature */
} EcsRowSystem;


/* -- Private types -- */

/** A table column describes a single column in a table (archetype) */
typedef struct ecs_table_column_t {
    ecs_array_t *data;               /* Column data */
    uint16_t size;                /* Column size (saves component lookups) */
} ecs_table_column_t;

/** A table is the Flecs equivalent of an archetype. Tables store all entities
 * with a specific set of components. Tables are automatically created when an
 * entity has a set of components not previously observed before. When a new
 * table is created, it is automatically matched with existing column systems */
typedef struct ecs_table_t {
    ecs_array_t *type;               /* Reference to type_index entry */
    ecs_table_column_t *columns;      /* Columns storing components of array */
    ecs_array_t *frame_systems;      /* Frame systems matched with table */
    ecs_type_t type_id;              /* Identifies table type in type_index */
 } ecs_table_t;
 
/** The ecs_row_t struct is a 64-bit value that describes in which table
 * (identified by a type_id) is stored, at which index. Entries in the 
 * world::entity_index are of type ecs_row_t. */
typedef struct ecs_row_t {
    ecs_type_t type_id;           /* Identifies a type (and table) in world */
    int32_t index;                /* Index of the entity in its table */
} ecs_row_t;

/** Supporting type that internal functions pass around to ensure that data
 * related to an entity is only looked up once. */
typedef struct ecs_entity_info_t {
    ecs_entity_t entity;
    ecs_type_t type_id;
    uint32_t index;
    ecs_table_t *table;
    ecs_table_column_t *columns;
} ecs_entity_info_t;

/** A stage is a data structure in which delta's are stored until it is safe to
 * merge those delta's with the main world stage. A stage allows flecs systems
 * to arbitrarily add/remove/set components and create/delete entities while
 * iterating. Additionally, worker threads have their own stage that lets them
 * mutate the state of entities without requiring locks. */
typedef struct ecs_stage_t {
    /* If this is not main stage, 
     * changes to the entity index 
     * are buffered here */
    ecs_map_t *entity_index;        /* Entity lookup table for (table, row) */

    /* If this is not a thread
     * stage, these are the same
     * as the main stage */
    ecs_map_t *table_index;         /* Index for table stage */
    ecs_array_t *tables;            /* Tables created while >1 threads running */
    ecs_map_t *type_index;          /* Types created while >1 threads running */

    
    /* These occur only in
     * temporary stages, and
     * not on the main stage */
    ecs_map_t *data_stage;          /* Arrays with staged component values */
    ecs_map_t *remove_merge;        /* All removed components before merge */
} ecs_stage_t;

/** A type describing a unit of work to be executed by a worker thread. */ 
typedef struct ecs_job_t {
    ecs_entity_t system;             /* System handle */
    EcsColSystem *system_data;    /* System to run */
    uint32_t offset;              /* Start index in row chunk */
    uint32_t limit;               /* Total number of rows to process */
} ecs_job_t;

/** A type desribing a worker thread. When a system is invoked by a worker
 * thread, it receives a pointer to an ecs_thread_t instead of a pointer to an 
 * ecs_world_t (provided by the ecs_rows_t type). When this ecs_thread_t is passed down
 * into the flecs API, the API functions are able to tell whether this is an
 * ecs_thread_t or an ecs_world_t by looking at the 'magic' number. This allows the
 * API to transparently resolve the stage to which updates should be written,
 * without requiring different API calls when working in multi threaded mode. */
typedef struct ecs_thread_t {
    uint32_t magic;               /* Magic number to verify thread pointer */
    uint32_t job_count;           /* Number of jobs scheduled for thread */
    ecs_world_t *world;              /* Reference to world */
    ecs_job_t *jobs[ECS_MAX_JOBS_PER_WORKER]; /* Array with jobs */
    ecs_stage_t *stage;              /* Stage for thread */
    ecs_os_thread_t thread;          /* Thread handle */
} ecs_thread_t;

/** The world stores and manages all ECS data. An application can have more than
 * one world, but data is not shared between worlds. */
struct ecs_world_t {
    uint32_t magic;               /* Magic number to verify world pointer */
    float delta_time;           /* Time passed to or computed by ecs_progress */
    void *context;                /* Application context */
    

    /* -- Column systems -- */

    ecs_array_t *on_load_systems;  
    ecs_array_t *post_load_systems;  
    ecs_array_t *pre_update_systems;  
    ecs_array_t *on_update_systems;   
    ecs_array_t *on_validate_systems; 
    ecs_array_t *post_update_systems; 
    ecs_array_t *pre_store_systems; 
    ecs_array_t *on_store_systems;   
    ecs_array_t *on_demand_systems;  
    ecs_array_t *inactive_systems;   


    /* -- Row systems -- */

    ecs_array_t *add_systems;        /* Systems invoked on ecs_stage_add */
    ecs_array_t *remove_systems;     /* Systems invoked on ecs_stage_remove */
    ecs_array_t *set_systems;        /* Systems invoked on ecs_set */


    /* -- Tasks -- */

    ecs_array_t *tasks;              /* Periodic actions not invoked on entities */
    ecs_array_t *fini_tasks;         /* Tasks to execute on ecs_fini */


    /* -- Lookup Indices -- */

    ecs_map_t *prefab_index;          /* Index to find prefabs in families */
    ecs_map_t *type_sys_add_index;    /* Index to find add row systems for type */
    ecs_map_t *type_sys_remove_index; /* Index to find remove row systems for type*/
    ecs_map_t *type_sys_set_index;    /* Index to find set row systems for type */
    ecs_map_t *type_handles;          /* Handles to named families */


    /* -- Staging -- */

    ecs_stage_t main_stage;          /* Main storage */
    ecs_stage_t temp_stage;          /* Stage for when processing systems */
    ecs_array_t *worker_stages;      /* Stages for worker threads */


    /* -- Multithreading -- */

    ecs_array_t *worker_threads;     /* Worker threads */
    ecs_os_cond_t thread_cond;       /* Signal that worker threads can start */
    ecs_os_mutex_t thread_mutex;     /* Mutex for thread condition */
    ecs_os_cond_t job_cond;          /* Signal that worker thread job is done */
    ecs_os_mutex_t job_mutex;        /* Mutex for protecting job counter */
    uint32_t jobs_finished;          /* Number of jobs finished */
    uint32_t threads_running;        /* Number of threads running */

    ecs_entity_t last_handle;        /* Last issued handle */


    /* -- Handles to builtin components families -- */

    ecs_type_t t_component;
    ecs_type_t t_type;
    ecs_type_t t_prefab;
    ecs_type_t t_row_system;
    ecs_type_t t_col_system;


    /* -- Time management -- */

    uint32_t tick;                /* Number of computed frames by world */
    ecs_time_t frame_start;  /* Starting timestamp of frame */
    float frame_time;             /* Time spent processing a frame */
    float system_time;            /* Time spent processing systems */
    float merge_time;             /* Time spent on merging */
    float target_fps;             /* Target fps */
    float fps_sleep;              /* Sleep time to prevent fps overshoot */


    /* -- Settings from command line arguments -- */

    int arg_fps;
    int arg_threads;


    /* -- World state -- */

    bool valid_schedule;          /* Is job schedule still valid */
    bool quit_workers;            /* Signals worker threads to quit */
    bool in_progress;             /* Is world being progressed */
    bool is_merging;              /* Is world currently being merged */
    bool auto_merge;              /* Are stages auto-merged by ecs_progress */
    bool measure_frame_time;      /* Time spent on each frame */
    bool measure_system_time;     /* Time spent by each system */
    bool should_quit;             /* Did a system signal that app should quit */
};


/* Parameters for various array types */
extern const ecs_array_params_t handle_arr_params;
extern const ecs_array_params_t stage_arr_params;
extern const ecs_array_params_t table_arr_params;
extern const ecs_array_params_t thread_arr_params;
extern const ecs_array_params_t job_arr_params;
extern const ecs_array_params_t column_arr_params;


#endif
