#include <assert.h>
#include "include/private/reflecs.h"

/** Callback that is invoked when a row is moved in the table->rows array */
static
void move_row(
    EcsArray *array,
    const EcsArrayParams *params,
    void *to,
    void *from,
    void *ctx)
{
    EcsWorld *world = params->ctx;
    uint32_t table_index = (uintptr_t)ctx;
    EcsTable *table = ecs_array_get(
        world->table_db, &table_arr_params, table_index);
    uint32_t new_index = ecs_array_get_index(array, params, to);
    EcsHandle handle = *(EcsHandle*)to;
    EcsRow row = {.family_id = table->family_id, .index = new_index};
    ecs_map_set64(world->entity_index, handle, ecs_from_row(row));
}

static
void activate_table_systems(
    EcsWorld *world,
    EcsTable *table,
    EcsArray *systems,
    bool activate)
{
    if (systems) {
        EcsIter it = ecs_array_iter(systems, &handle_arr_params);
        while (ecs_iter_hasnext(&it)) {
            EcsHandle system = *(EcsHandle*)ecs_iter_next(&it);
            ecs_system_activate_table(world, system, table, activate);
        }
    }
}

/** Notify systems that a table has changed its active state */
static
void activate_table(
    EcsWorld *world,
    EcsTable *table,
    bool activate)
{
    activate_table_systems(world, table, table->periodic_systems, activate);
    activate_table_systems(world, table, table->init_systems, activate);
    activate_table_systems(world, table, table->deinit_systems, activate);
}

/* -- Private functions -- */

EcsResult ecs_table_init_w_size(
    EcsWorld *world,
    EcsTable *table,
    EcsArray *family,
    uint32_t size)
{
    table->family = family;

    table->periodic_systems = NULL;
    table->init_systems = NULL;
    table->deinit_systems = NULL;

    table->row_params.element_size = size + sizeof(EcsHandle);
    table->row_params.move_action = move_row;
    table->row_params.move_ctx = (void*)(uintptr_t)ecs_array_get_index(
        world->table_db, &table_arr_params, table);
    table->row_params.ctx = world;

    table->rows = ecs_array_new(
        &table->row_params, ECS_TABLE_INITIAL_ROW_COUNT);

    return EcsOk;
}

EcsResult ecs_table_init(
    EcsWorld *world,
    EcsTable *table)
{
    EcsArray *family = ecs_map_get(world->family_index, table->family_id);
    bool prefab_set = false;

    assert(family != NULL);

    EcsIter it = ecs_array_iter(family, &handle_arr_params);
    uint32_t column = 0;
    uint32_t total_size = 0;
    table->columns = malloc(sizeof(uint16_t) * ecs_array_count(family));

    while (ecs_iter_hasnext(&it)) {
        EcsHandle h = *(EcsHandle*)ecs_iter_next(&it);
        EcsComponent *type = ecs_get_ptr(world, h, EcsComponent_h);
        uint32_t size;
        if (type) {
            size = type->size;
        } else {
            if (ecs_get_ptr(world, h, EcsPrefab_h)) {
                assert(prefab_set == false);
                ecs_map_set(world->prefab_index, table->family_id, h);
                prefab_set = true;
                size = 0;
            } else {
                return EcsError;
            }
        }

        table->columns[column] = size;
        total_size += size;
        column ++;
    }

    ecs_table_init_w_size(world, table, family, total_size);

    return EcsOk;
}

uint32_t ecs_table_insert(
    EcsWorld *world,
    EcsTable *table,
    EcsHandle handle)
{
    void *row = ecs_array_add(&table->rows, &table->row_params);
    *(EcsHandle*)row = handle;
    uint32_t index = ecs_array_count(table->rows) - 1;

    if (!index) {
        activate_table(world, table, true);
    }

    return index;
}

void ecs_table_delete(
    EcsWorld *world,
    EcsTable *table,
    uint32_t index)
{
    uint32_t count = ecs_array_remove_index(
        table->rows, &table->row_params, index);
    if (!count) {
        activate_table(world, table, false);
    }
}

void* ecs_table_get(
    EcsTable *table,
    uint32_t index)
{
    return ecs_array_get(table->rows, &table->row_params, index);
}

uint32_t ecs_table_column_offset(
    EcsTable *table,
    EcsHandle component)
{
    EcsIter it = ecs_array_iter(table->family, &handle_arr_params);
    uint32_t i = 0, offset = 0;

    while (ecs_iter_hasnext(&it)) {
        EcsHandle h = *(EcsHandle*)ecs_iter_next(&it);
        if (h == component) {
            return offset;
        }
        offset += table->columns[i];
        i ++;
    }

    return -1;
}

void ecs_table_deinit(
    EcsWorld *world,
    EcsTable *table)
{
    if (table->deinit_systems) {
        EcsHandle *buffer = ecs_array_buffer(table->deinit_systems);
        uint32_t i, count = ecs_array_count(table->deinit_systems);
        for (i = 0; i < count; i ++) {
            ecs_run_system(world, buffer[i], NULL);
        }
    }
}
