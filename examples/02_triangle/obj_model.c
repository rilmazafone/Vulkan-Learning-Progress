#include "obj_model.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Vec3 {
    float x;
    float y;
    float z;
} Vec3;

typedef struct FaceVertex {
    Vec3 position;
    Vec3 normal;
} FaceVertex;

static void set_error(char *error,
                      size_t error_capacity,
                      const char *message)
{
    if (error != NULL && error_capacity > 0) {
        (void)snprintf(error, error_capacity, "%s", message);
    }
}

static bool grow_array(void **data,
                       size_t *capacity,
                       size_t required,
                       size_t item_size)
{
    if (required <= *capacity) {
        return true;
    }

    size_t new_capacity = *capacity == 0 ? 1024 : *capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2) {
            return false;
        }
        new_capacity *= 2;
    }
    if (new_capacity > SIZE_MAX / item_size) {
        return false;
    }

    void *new_data = realloc(*data, new_capacity * item_size);
    if (new_data == NULL) {
        return false;
    }
    *data = new_data;
    *capacity = new_capacity;
    return true;
}

static bool parse_float_token(const char *text, float *value)
{
    char *end = NULL;
    const float parsed = strtof(text, &end);
    if (end == text || *end != '\0') {
        return false;
    }
    *value = parsed;
    return true;
}

static bool parse_vec3_tokens(Vec3 *value)
{
    const char *x_text = strtok(NULL, " \t\r\n");
    const char *y_text = strtok(NULL, " \t\r\n");
    const char *z_text = strtok(NULL, " \t\r\n");
    return x_text != NULL && y_text != NULL && z_text != NULL &&
           parse_float_token(x_text, &value->x) &&
           parse_float_token(y_text, &value->y) &&
           parse_float_token(z_text, &value->z);
}

static bool resolve_obj_index(long raw_index,
                              size_t item_count,
                              uint32_t *result)
{
    long resolved = 0;
    if (raw_index > 0) {
        resolved = raw_index - 1;
    } else if (raw_index < 0) {
        if (item_count > (size_t)LONG_MAX) {
            return false;
        }
        resolved = (long)item_count + raw_index;
    } else {
        return false;
    }

    if (resolved < 0 || (unsigned long)resolved >= item_count ||
        (unsigned long)resolved > UINT32_MAX) {
        return false;
    }
    *result = (uint32_t)resolved;
    return true;
}

static bool parse_face_vertex(char *token,
                              const Vec3 *positions,
                              size_t position_count,
                              const Vec3 *normals,
                              size_t normal_count,
                              FaceVertex *result)
{
    char *normal_text = NULL;
    char *slash = strchr(token, '/');
    if (slash != NULL) {
        *slash = '\0';
        char *after_slash = slash + 1;
        char *second_slash = strchr(after_slash, '/');
        if (second_slash != NULL) {
            normal_text = second_slash + 1;
            if (*normal_text == '\0') {
                normal_text = NULL;
            }
        }
    }

    char *end = NULL;
    const long position_index = strtol(token, &end, 10);
    if (end == token || *end != '\0') {
        return false;
    }

    uint32_t resolved_position = 0;
    if (!resolve_obj_index(position_index,
                           position_count,
                           &resolved_position)) {
        return false;
    }
    result->position = positions[resolved_position];
    result->normal = (Vec3){0.0F, 0.0F, 0.0F};

    if (normal_text != NULL) {
        const long normal_index = strtol(normal_text, &end, 10);
        if (end == normal_text || *end != '\0') {
            return false;
        }

        uint32_t resolved_normal = 0;
        if (!resolve_obj_index(normal_index, normal_count, &resolved_normal)) {
            return false;
        }
        result->normal = normals[resolved_normal];
    }
    return true;
}

static bool append_face_vertex(ObjModel *model,
                               size_t *vertex_capacity,
                               size_t *index_capacity,
                               FaceVertex face_vertex)
{
    if (model->vertex_count == UINT32_MAX) {
        return false;
    }
    const size_t required = (size_t)model->vertex_count + 1U;
    if (!grow_array((void **)&model->vertices,
                    vertex_capacity,
                    required,
                    sizeof(*model->vertices)) ||
        !grow_array((void **)&model->indices,
                    index_capacity,
                    required,
                    sizeof(*model->indices))) {
        return false;
    }

    ObjVertex *vertex = &model->vertices[model->vertex_count];
    vertex->position[0] = face_vertex.position.x;
    vertex->position[1] = face_vertex.position.y;
    vertex->position[2] = face_vertex.position.z;
    vertex->normal[0] = face_vertex.normal.x;
    vertex->normal[1] = face_vertex.normal.y;
    vertex->normal[2] = face_vertex.normal.z;
    model->indices[model->vertex_count] = model->vertex_count;
    ++model->vertex_count;
    model->index_count = model->vertex_count;
    return true;
}

static Vec3 subtract_vec3(Vec3 left, Vec3 right)
{
    return (Vec3){
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

static Vec3 cross_vec3(Vec3 left, Vec3 right)
{
    return (Vec3){
        (left.y * right.z) - (left.z * right.y),
        (left.z * right.x) - (left.x * right.z),
        (left.x * right.y) - (left.y * right.x),
    };
}

static float length_squared_vec3(Vec3 value)
{
    return (value.x * value.x) + (value.y * value.y) +
           (value.z * value.z);
}

static void fill_missing_normals(ObjModel *model)
{
    for (uint32_t index = 0; index + 2U < model->index_count; index += 3U) {
        ObjVertex *first = &model->vertices[model->indices[index]];
        ObjVertex *second = &model->vertices[model->indices[index + 1U]];
        ObjVertex *third = &model->vertices[model->indices[index + 2U]];

        const Vec3 first_position = {
            first->position[0], first->position[1], first->position[2]};
        const Vec3 second_position = {
            second->position[0], second->position[1], second->position[2]};
        const Vec3 third_position = {
            third->position[0], third->position[1], third->position[2]};
        const Vec3 face_normal = cross_vec3(
            subtract_vec3(second_position, first_position),
            subtract_vec3(third_position, first_position));

        ObjVertex *face_vertices[] = {first, second, third};
        for (size_t vertex_index = 0;
             vertex_index < sizeof(face_vertices) / sizeof(face_vertices[0]);
             ++vertex_index) {
            ObjVertex *vertex = face_vertices[vertex_index];
            const Vec3 normal = {
                vertex->normal[0], vertex->normal[1], vertex->normal[2]};
            if (length_squared_vec3(normal) == 0.0F) {
                vertex->normal[0] = face_normal.x;
                vertex->normal[1] = face_normal.y;
                vertex->normal[2] = face_normal.z;
            }
        }
    }
}

bool obj_model_load(const char *path,
                    ObjModel *model,
                    char *error,
                    size_t error_capacity)
{
    memset(model, 0, sizeof(*model));

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        set_error(error, error_capacity, "Could not open the OBJ file.");
        return false;
    }

    Vec3 *positions = NULL;
    Vec3 *normals = NULL;
    size_t position_count = 0;
    size_t normal_count = 0;
    size_t position_capacity = 0;
    size_t normal_capacity = 0;
    size_t vertex_capacity = 0;
    size_t index_capacity = 0;
    size_t line_number = 0;
    bool success = false;
    bool have_bounds = false;
    Vec3 minimum = {0.0F, 0.0F, 0.0F};
    Vec3 maximum = {0.0F, 0.0F, 0.0F};

    char line[4096];
    while (fgets(line, (int)sizeof(line), file) != NULL) {
        ++line_number;
        const size_t line_length = strlen(line);
        if (line_length + 1U == sizeof(line) && line[line_length - 1U] != '\n' &&
            fgetc(file) != EOF) {
            set_error(error, error_capacity, "An OBJ line is too long.");
            goto cleanup;
        }

        char *comment = strchr(line, '#');
        if (comment != NULL) {
            *comment = '\0';
        }

        char *keyword = strtok(line, " \t\r\n");
        if (keyword == NULL) {
            continue;
        }

        if (strcmp(keyword, "v") == 0) {
            Vec3 position = {0.0F, 0.0F, 0.0F};
            if (!parse_vec3_tokens(&position)) {
                set_error(error, error_capacity, "Malformed OBJ vertex.");
                goto cleanup;
            }
            if (!grow_array((void **)&positions,
                            &position_capacity,
                            position_count + 1U,
                            sizeof(*positions))) {
                set_error(error, error_capacity, "Out of memory reading vertices.");
                goto cleanup;
            }
            positions[position_count] = position;
            ++position_count;

            if (!have_bounds) {
                minimum = position;
                maximum = position;
                have_bounds = true;
            } else {
                if (position.x < minimum.x) {
                    minimum.x = position.x;
                }
                if (position.y < minimum.y) {
                    minimum.y = position.y;
                }
                if (position.z < minimum.z) {
                    minimum.z = position.z;
                }
                if (position.x > maximum.x) {
                    maximum.x = position.x;
                }
                if (position.y > maximum.y) {
                    maximum.y = position.y;
                }
                if (position.z > maximum.z) {
                    maximum.z = position.z;
                }
            }
        } else if (strcmp(keyword, "vn") == 0) {
            Vec3 normal = {0.0F, 0.0F, 0.0F};
            if (!parse_vec3_tokens(&normal)) {
                set_error(error, error_capacity, "Malformed OBJ normal.");
                goto cleanup;
            }
            if (!grow_array((void **)&normals,
                            &normal_capacity,
                            normal_count + 1U,
                            sizeof(*normals))) {
                set_error(error, error_capacity, "Out of memory reading normals.");
                goto cleanup;
            }
            normals[normal_count] = normal;
            ++normal_count;
        } else if (strcmp(keyword, "f") == 0) {
            FaceVertex *face_vertices = NULL;
            size_t face_count = 0;
            size_t face_capacity = 0;
            char *face_token = NULL;
            bool face_valid = true;

            while ((face_token = strtok(NULL, " \t\r\n")) != NULL) {
                FaceVertex face_vertex = {0};
                if (!parse_face_vertex(face_token,
                                       positions,
                                       position_count,
                                       normals,
                                       normal_count,
                                       &face_vertex) ||
                    !grow_array((void **)&face_vertices,
                                &face_capacity,
                                face_count + 1U,
                                sizeof(*face_vertices))) {
                    face_valid = false;
                    break;
                }
                face_vertices[face_count] = face_vertex;
                ++face_count;
            }

            if (!face_valid || face_count < 3U) {
                free(face_vertices);
                if (error != NULL && error_capacity > 0) {
                    (void)snprintf(error,
                                   error_capacity,
                                   "Malformed OBJ face on line %zu.",
                                   line_number);
                }
                goto cleanup;
            }

            for (size_t corner = 1U; corner + 1U < face_count; ++corner) {
                if (!append_face_vertex(model,
                                        &vertex_capacity,
                                        &index_capacity,
                                        face_vertices[0]) ||
                    !append_face_vertex(model,
                                        &vertex_capacity,
                                        &index_capacity,
                                        face_vertices[corner]) ||
                    !append_face_vertex(model,
                                        &vertex_capacity,
                                        &index_capacity,
                                        face_vertices[corner + 1U])) {
                    free(face_vertices);
                    set_error(error,
                              error_capacity,
                              "Out of memory expanding OBJ faces.");
                    goto cleanup;
                }
            }
            free(face_vertices);
        }
    }

    if (model->vertex_count == 0 || !have_bounds) {
        set_error(error, error_capacity, "OBJ file contains no drawable faces.");
        goto cleanup;
    }

    fill_missing_normals(model);
    model->triangle_count = model->index_count / 3U;
    model->center[0] = 0.5F * (minimum.x + maximum.x);
    model->center[1] = 0.5F * (minimum.y + maximum.y);
    model->center[2] = 0.5F * (minimum.z + maximum.z);

    const float half_x = 0.5F * (maximum.x - minimum.x);
    const float half_y = 0.5F * (maximum.y - minimum.y);
    const float half_z = 0.5F * (maximum.z - minimum.z);
    model->radius =
        sqrtf((half_x * half_x) + (half_y * half_y) + (half_z * half_z));
    if (model->radius <= 0.0F) {
        set_error(error, error_capacity, "OBJ model has zero bounds.");
        goto cleanup;
    }
    success = true;

cleanup:
    free(positions);
    free(normals);
    (void)fclose(file);
    if (!success) {
        obj_model_destroy(model);
    }
    return success;
}

void obj_model_destroy(ObjModel *model)
{
    if (model == NULL) {
        return;
    }
    free(model->vertices);
    free(model->indices);
    memset(model, 0, sizeof(*model));
}
