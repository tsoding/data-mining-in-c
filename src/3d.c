#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <float.h>

#include "raylib.h"
#include "raymath.h"

#define IMAGE

#define NOB_IMPLEMENTATION
#include "nob.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#define K 4
#define SAMPLE_SIZE 0.25
#define SAMPLE_COLOR RED
#define MEAN_SIZE (2*SAMPLE_SIZE)
#define MEAN_COLOR WHITE
#define CAMERA_SPEED SAMPLE_SIZE

#include "common.c"

typedef struct {
    Vector3 *items;
    size_t count;
    size_t capacity;
} Samples3D;

static void generate_cluster(Vector3 center, float radius, size_t count, Samples3D *samples)
{
    for (size_t i = 0; i < count; ++i) {
        float mag = rand_float()*radius;
        float theta = rand_float()*2*PI;
        float phi = rand_float()*2*PI;
        Vector3 sample = {
            .x = sinf(theta)*cosf(phi)*mag,
            .y = sinf(theta)*sinf(phi)*mag,
            .z = cosf(theta)*mag,
        };
        nob_da_append(samples, Vector3Add(sample, center));
    }
}

static Samples3D set = {0};
static Samples3D clusters[K] = {0};
static Vector3 means[K] = {0};

void generate_new_state(float cluster_radius, size_t cluster_count)
{
#ifndef IMAGE
    generate_cluster((Vector3) {0, 0, 0}, cluster_radius, cluster_count, &set);
    generate_cluster((Vector3) {-cluster_radius, cluster_radius, 0}, cluster_radius/2, cluster_count/2, &set);
    generate_cluster((Vector3) {cluster_radius, cluster_radius, 0}, cluster_radius/2, cluster_count/2, &set);
#else
    (void) cluster_count;
#endif

    for (size_t i = 0; i < K; ++i) {
        means[i].x = Lerp(-cluster_radius, cluster_radius, rand_float());
        means[i].y = Lerp(-cluster_radius, cluster_radius, rand_float());
        means[i].z = Lerp(-cluster_radius, cluster_radius, rand_float());
    }
}

void recluster_state(void)
{
    for (size_t j = 0; j < K; ++j) {
        clusters[j].count = 0;
    }
    for (size_t i = 0; i < set.count; ++i) {
        Vector3 p = set.items[i];
        int k = -1;
        float s = FLT_MAX;
        for (size_t j = 0; j < K; ++j) {
            Vector3 m = means[j];
            float sm = Vector3LengthSqr(Vector3Subtract(p, m));
            if (sm < s) {
                s = sm;
                k = j;
            }
        }
        nob_da_append(&clusters[k], p);
    }
}

void update_means(float cluster_radius)
{
    for (size_t i = 0; i < K; ++i) {
        if (clusters[i].count > 0) {
            means[i] = Vector3Zero();
            for (size_t j = 0; j < clusters[i].count; ++j) {
                means[i] = Vector3Add(means[i], clusters[i].items[j]);
            }
            means[i].x /= clusters[i].count;
            means[i].y /= clusters[i].count;
            means[i].z /= clusters[i].count;
        } else {
            means[i].x = Lerp(-cluster_radius, cluster_radius, rand_float());
            means[i].y = Lerp(-cluster_radius, cluster_radius, rand_float());
            means[i].z = Lerp(-cluster_radius, cluster_radius, rand_float());
        }
    }
}

typedef struct {
    Color key;
} Color_Point;

int cluster_of_color(Color color, float cluster_radius)
{
    Vector3 p = {
        .x = color.r/255.0f*cluster_radius,
        .y = color.g/255.0f*cluster_radius,
        .z = color.b/255.0f*cluster_radius,
    };

    int k = -1;
    float s = FLT_MAX;
    for (size_t j = 0; j < K; ++j) {
        Vector3 m = means[j];
        float sm = Vector3LengthSqr(Vector3Subtract(p, m));
        if (sm < s) {
            s = sm;
            k = j;
        }
    }

    return k;
}

int main(int argc, char **argv)
{
    float cluster_radius = 20;
    size_t cluster_count = 200;

#ifdef IMAGE
    (void) generate_cluster;
    nob_shift_args(&argc, &argv);

    if (argc <= 0) {
        nob_log(NOB_ERROR, "No input image is provided");
        return 1;
    }

    const char *file_path = nob_shift_args(&argc, &argv);
    Image image = LoadImage(file_path);
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    Color_Point *unique_points = NULL;
    Color *points = image.data;
    size_t points_count = image.width*image.height;

    for (size_t i = 0; i < points_count; ++i) {
        ptrdiff_t index = hmgeti(unique_points, points[i]);
        if (index < 0) {
            Color_Point item = { points[i] };
            hmputs(unique_points, item);
        }
    }

    size_t unique_points_count = hmlen(unique_points);
    for (size_t i = 0; i < unique_points_count; ++i) {
        Vector3 sample = {
            .x = unique_points[i].key.r/255.0f*cluster_radius,
            .y = unique_points[i].key.g/255.0f*cluster_radius,
            .z = unique_points[i].key.b/255.0f*cluster_radius,
        };
        nob_da_append(&set, sample);
    }
#endif // IMAGE

    generate_new_state(cluster_radius, cluster_count);
    recluster_state();

    float camera_mag = 50;
    float camera_mag_vel = 0.0f;
    float camera_theta = 0.0;
    float camera_phi = 0.0;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(800, 600, "3D K-means");
    if (!IsWindowReady()) return 1;
    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_SPACE)) {
            update_means(cluster_radius);
            recluster_state();
        }

        if (IsKeyPressed(KEY_S)) {
            for (size_t i = 0; i < points_count; ++i) {
                int k = cluster_of_color(points[i], cluster_radius);
                if (k < 0) nob_log(NOB_ERROR, "Color out of cluster");
                Color color = {
                    .r = means[k].x/cluster_radius*255,
                    .g = means[k].y/cluster_radius*255,
                    .b = means[k].z/cluster_radius*255,
                    .a = 255,
                };
                points[i] = color;
            }
            ExportImage(image, "output.png");
        }

        float dt = GetFrameTime();

        camera_mag += camera_mag_vel*dt;
        if (camera_mag < 0.0f) camera_mag = 0.0f;
        camera_mag_vel -= GetMouseWheelMove()*20;
        camera_mag_vel *= 0.9;

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            Vector2 delta = GetMouseDelta();
            camera_theta -= delta.x*0.01;
            // TODO: allow vertical rotation
            camera_phi -= delta.y*0.01;
        }

        BeginDrawing();
            ClearBackground(GetColor(0x181818AA));
            Camera3D camera = {
                .position   = {
                    .x = sinf(camera_theta)*cosf(camera_phi)*camera_mag,
                    .y = sinf(camera_theta)*sinf(camera_phi)*camera_mag,
                    .z = cosf(camera_theta)*camera_mag,
                },
                .target     = {0, 0, 0},
                .up         = {0, 1, 0},
                .fovy       = 90,
                .projection = CAMERA_PERSPECTIVE,
            };
            BeginMode3D(camera);
                for (size_t i = 0; i < K; ++i) {
                    Color color = {
                        .r = means[i].x/cluster_radius*255,
                        .g = means[i].y/cluster_radius*255,
                        .b = means[i].z/cluster_radius*255,
                        .a = 255,
                    };

                    for (size_t j = 0; j < clusters[i].count; ++j) {
                        Vector3 it = clusters[i].items[j];
                        DrawCube(it, SAMPLE_SIZE, SAMPLE_SIZE, SAMPLE_SIZE, color);
                    }
                    DrawCube(means[i], MEAN_SIZE, MEAN_SIZE, MEAN_SIZE, MEAN_COLOR);
                }
            EndMode3D();
        EndDrawing();
    }
    CloseWindow();
    return 0;
}
