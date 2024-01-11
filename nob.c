#define NOB_IMPLEMENTATION
#include "./src/nob.h"

bool build_program(const char *source_path, const char *output_path)
{
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "cc");
    nob_cmd_append(&cmd, "-Wall", "-Wextra", "-ggdb");
    nob_cmd_append(&cmd, "-I./raylib/", "-I./zlib/", "-I./stb/");
    nob_cmd_append(&cmd, "-O3");
    nob_cmd_append(&cmd, "-o", output_path);
    nob_cmd_append(&cmd, source_path);
    nob_cmd_append(&cmd, "-L./raylib/", "-L./zlib/");
    nob_cmd_append(&cmd, "-lraylib", "-lm", "-lz", "-ldl", "-lpthread");
    return nob_cmd_run_sync(cmd);
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);
    if (!nob_mkdir_if_not_exists("./build/")) return 1;
    // if (!build_program("./src/2d.c", "./build/2d")) return 1;
    // if (!build_program("./src/3d.c", "./build/3d")) return 1;
    if (!build_program("./src/knn.c", "./build/knn")) return 1;
    return 0;
}
