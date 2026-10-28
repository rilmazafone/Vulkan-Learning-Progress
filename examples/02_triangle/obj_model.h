#ifndef OBJ_MODEL_H
#define OBJ_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ObjVertex {
    float position[3];
    float normal[3];
} ObjVertex;

typedef struct ObjModel {
    ObjVertex *vertices;
    uint32_t *indices;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t triangle_count;
    float center[3];
    float radius;
} ObjModel;

bool obj_model_load(const char *path,
                    ObjModel *model,
                    char *error,
                    size_t error_capacity);

void obj_model_destroy(ObjModel *model);

#endif
