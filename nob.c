#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "./src/nob.h"

#define RAYLIB_FLAGS "-w",                 \
                     "-ggdb", "-O3",       \
                     "-pipe",              \
                     "-D_GLFW_X11",        \
                     "-DPLATFORM_DESKTOP", \
                     "-I./raylib/src",     \
                     "-I./raylib/src/external/glfw/include"

#define RAYLIB_SOURCE_PATH "raylib/src/"

static const char *raylib_filenames[] = {
    "raudio",
    "rmodels",
    "rtextures",
    "rcore",
    "rshapes",
    "utils",
    "rglfw",
    "rtext",
};

#define RAYLIB_FILE_COUNT (sizeof(raylib_filenames)/sizeof(raylib_filenames[0]))

Proc build_single_raylib_object_file(const char *name) {
    Cmd cmd = {0};

    cmd_append(&cmd, "cc", RAYLIB_FLAGS, "-c", "-o",
        temp_sprintf("raylib/%s.o", name),
        temp_sprintf(RAYLIB_SOURCE_PATH "%s.c", name),
    );

    return nob_cmd_run_async(cmd);
}

bool build_raylib(void) {

    char *input_paths[RAYLIB_FILE_COUNT+1] = {0};

    input_paths[0] = RAYLIB_SOURCE_PATH "config.h"; // Rebuild on config changes too

    for(size_t i = 1; i < RAYLIB_FILE_COUNT+1; i++)
        input_paths[i] = temp_sprintf(RAYLIB_SOURCE_PATH "%s.c", raylib_filenames[i-1]);

    if(!needs_rebuild("raylib/libraylib.a", (const char **)input_paths, RAYLIB_FILE_COUNT+1))
        return true;

    Proc async_pool[RAYLIB_FILE_COUNT] = {0};

    for(size_t i = 0; i < RAYLIB_FILE_COUNT; i++)
            async_pool[i] = build_single_raylib_object_file(raylib_filenames[i]);

    Procs procs = (Procs) {
        .items    = &async_pool[0],
        .count    = RAYLIB_FILE_COUNT,
        .capacity = RAYLIB_FILE_COUNT
    };

    if(!procs_wait(procs)) return false;

    Cmd cmd = {0};

    cmd_append(&cmd, "ar", "-rcs", "raylib/libraylib.a");
    for(size_t i = 0; i < RAYLIB_FILE_COUNT; i++)
        cmd_append(&cmd, temp_sprintf("raylib/%s.o", raylib_filenames[i]));

    if(!cmd_run_sync(cmd)) return false;

    return true;

}

Proc build_program(const char *source_path, const char *output_path)
{
    Cmd cmd = {0};
    cmd_append(&cmd, "cc");
    cmd_append(&cmd, "-Wall", "-Wextra", "-ggdb");
    cmd_append(&cmd, "-I./raylib/src/", "-I./zlib/", "-I./stb/");
    cmd_append(&cmd, "-O3");
    cmd_append(&cmd, "-o", output_path);
    cmd_append(&cmd, source_path);
    cmd_append(&cmd, "-L./raylib/", "-L./zlib/");
    cmd_append(&cmd, "-l:libraylib.a", "-lm", "-lz", "-ldl", "-lpthread");
    return cmd_run_async(cmd);
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);
    if (!mkdir_if_not_exists("./build/")) return 1;

    if (!build_raylib()) return 1;

    Procs procs = {0};
    da_append(&procs, build_program("./src/2d.c", "./build/2d"));
    da_append(&procs, build_program("./src/3d.c", "./build/3d"));
    da_append(&procs, build_program("./src/knn.c", "./build/knn"));
    if (!procs_wait(procs)) return 1;
    return 0;
}
