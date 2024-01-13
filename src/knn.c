#include <stdio.h>
#include <zlib.h>

#include <sys/sysinfo.h>
#include <pthread.h>

#define NOB_IMPLEMENTATION
#include "nob.h"
#define ARENA_IMPLEMENTATION
#include "arena.h"

#define K 2

static __thread z_stream defstream = {0};
static __thread bool defstream_init = false;

// Stolen from https://gist.github.com/arq5x/5315739
Nob_String_View deflate_sv(Arena *arena, Nob_String_View sv)
{
    defstream.avail_in = (uInt)sv.count;
    defstream.next_in = (Bytef *)sv.data;

    int ret = defstream_init ? deflateReset(&defstream) : deflateInit(&defstream, Z_BEST_COMPRESSION);
    defstream_init = true;
    assert(ret == Z_OK && "Failed to initialize zlib deflate stream");
    size_t output_size = (size_t)deflateBound(&defstream, defstream.avail_in);
    void *output = arena_alloc(arena, output_size);
    defstream.avail_out = (uInt)output_size;
    defstream.next_out = (Bytef *)output;

    assert(deflate(&defstream, Z_FINISH) == Z_STREAM_END && "Probably `avail_in` is zero");

    return nob_sv_from_parts(output, defstream.total_out);
}

void deflate_end() {
    deflateEnd(&defstream);
}

typedef struct {
    size_t klass;
    Nob_String_View text;
} Sample;

typedef struct {
    Sample *items;
    size_t count;
    size_t capacity;
} Samples;

Samples parse_samples(Nob_String_View content)
{
    size_t lines_count = 0;
    Samples samples = {0};
    for (; content.count > 0; ++lines_count) {
        Nob_String_View line = nob_sv_chop_by_delim(&content, '\n');
        if (lines_count == 0) continue; // ignore the header

        Nob_String_View klass = nob_sv_chop_by_delim(&line, ',');
        size_t klass_index = *klass.data - '0' - 1;

        nob_da_append(&samples, ((Sample) {
            .klass = klass_index,
            .text = line,
        }));
    }
    return samples;
}

const char *klass_names[] = {"World", "Sports", "Business", "Sci/Tech"};

typedef struct {
    float distance;
    size_t klass;
} NCD;

typedef struct {
    NCD *items;
    size_t count;
    size_t capacity;
} NCDs;

float ncd(Arena *arena, Nob_String_View a, Nob_String_View b, float cb)
{
    Nob_String_View ab = nob_sv_from_cstr(arena_sprintf(arena, SV_Fmt" "SV_Fmt, SV_Arg(a), SV_Arg(b)));
    float ca = deflate_sv(arena, a).count;
    float cab = deflate_sv(arena, ab).count;
    float mn = ca; if (mn > cb) mn = cb;
    float mx = ca; if (mx < cb) mx = cb;
    return (cab - mn)/mx;
}

int compare_ncds(const void *a, const void *b)
{
    const NCD *na = a;
    const NCD *nb = b;
    if (na->distance < nb->distance) return -1;
    if (na->distance > nb->distance) return 1;
    return 0;
}

typedef struct {
    Sample *train;
    size_t train_count;
    Nob_String_View text;

    NCDs ncds;
    Arena arena;
} Klassify_State;

void *klassify_thread(void *params)
{
    Klassify_State *state = params;

    float cb = deflate_sv(&state->arena, state->text).count;
    for (size_t i = 0; i < state->train_count; ++i) {
        float distance = ncd(&state->arena, state->train[i].text, state->text, cb);
        arena_reset(&state->arena);
        nob_da_append(&state->ncds, ((NCD) {
            .distance = distance,
            .klass = state->train[i].klass,
        }));
    }
    deflate_end();

    return NULL;
}

typedef struct {
    size_t nprocs;
    size_t chunk_size;
    size_t chunk_rem;

    Samples train_samples;

    pthread_t *threads;
    Klassify_State *states;

    NCDs ncds;
} Klass_Predictor;

void klass_predictor_init(Klass_Predictor *kp, Samples train_samples)
{
    kp->nprocs = get_nproc();
    kp->chunk_size = train_samples.count/kp->nprocs;
    kp->chunk_rem = train_samples.count%kp->nprocs;
    kp->train_samples = train_samples;

    kp->threads = malloc(kp->nprocs*sizeof(pthread_t));
    assert(kp->threads != NULL);
    memset(kp->threads, 0, kp->nprocs*sizeof(pthread_t));
    kp->states = malloc(kp->nprocs*sizeof(Klassify_State));
    assert(kp->states != NULL);
    memset(kp->states, 0, kp->nprocs*sizeof(Klassify_State));
}

size_t klass_predictor_predict(Klass_Predictor *kp, Nob_String_View text, size_t k)
{
    for (size_t i = 0; i < kp->nprocs; ++i) {
        kp->states[i].train = kp->train_samples.items + i*kp->chunk_size;
        kp->states[i].train_count = kp->chunk_size;
        if (i == kp->nprocs - 1) kp->states[i].train_count += kp->chunk_rem;
        kp->states[i].text = text;
        kp->states[i].ncds.count = 0;
        arena_reset(&kp->states[i].arena);
        if (pthread_create(&kp->threads[i], NULL, klassify_thread, &kp->states[i]) != 0) {
            nob_log(NOB_ERROR, "Could not create thread");
            exit(1);
        }
    }

    kp->ncds.count = 0;
    for (size_t i = 0; i < kp->nprocs; ++i) {
        if (pthread_join(kp->threads[i], NULL) != 0) {
            nob_log(NOB_ERROR, "Could not join thread");
            exit(1);
        }
        nob_da_append_many(&kp->ncds, kp->states[i].ncds.items, kp->states[i].ncds.count);
    }
    qsort(kp->ncds.items, kp->ncds.count, sizeof(*kp->ncds.items), compare_ncds);

    size_t klass_freq[NOB_ARRAY_LEN(klass_names)] = {0};
    for (size_t i = 0; i < k && i < kp->ncds.count; ++i) {
        klass_freq[kp->ncds.items[i].klass] += 1;
    }

    size_t predicted_klass = 0;
    for (size_t i = 1; i < NOB_ARRAY_LEN(klass_names); ++i) {
        if (klass_freq[predicted_klass] < klass_freq[i]) {
            predicted_klass = i;
        }
    }

    return predicted_klass;
}

char buffer[512];

double clock_get_secs(void)
{
    struct timespec ts = {0};
    int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    assert(ret == 0);
    return (double)ts.tv_sec + ts.tv_nsec*1e-9;
}

void usage(const char *program)
{
    nob_log(NOB_ERROR, "Usage: %s <train.csv> [test.csv]", program);
}

void interactive_mode(Klass_Predictor *kp)
{
    nob_log(NOB_INFO, "Provide News Title:");
    while (true) {
        fgets(buffer, sizeof(buffer), stdin);
        double begin = clock_get_secs();
        size_t predicted_klass = klass_predictor_predict(kp, nob_sv_from_cstr(buffer), K);
        double end = clock_get_secs();
        nob_log(NOB_INFO, "Topic: %s (%.3lfsecs)", klass_names[predicted_klass], end - begin);
    }
}

int main(int argc, char **argv)
{
    const char *program = nob_shift_args(&argc, &argv);

    if (argc <= 0) {
        usage(program);
        nob_log(NOB_ERROR, "ERROR: no train file is provided");
        return 1;
    }
    const char *train_path = nob_shift_args(&argc, &argv);
    Nob_String_Builder train_content = {0};
    if (!nob_read_entire_file(train_path, &train_content)) return 1;
    Samples train_samples = parse_samples(nob_sv_from_parts(train_content.items, train_content.count));

    Klass_Predictor kp = {0};
    klass_predictor_init(&kp, train_samples);

    if (argc <= 0) {
        interactive_mode(&kp);
    } else {
        const char *test_path = nob_shift_args(&argc, &argv);
        Nob_String_Builder test_content = {0};
        if (!nob_read_entire_file(test_path, &test_content)) return 1;
        Samples test_samples = parse_samples(nob_sv_from_parts(test_content.items, test_content.count));

        size_t success = 0;
        for (size_t i = 0; i < test_samples.count; ++i) {
            Nob_String_View text = test_samples.items[i].text;
            size_t actual_klass = test_samples.items[i].klass;

            double begin = clock_get_secs();
            size_t predicted_klass = klass_predictor_predict(&kp, text, K);
            double end = clock_get_secs();
            if (predicted_klass == actual_klass) success += 1;
            nob_log(NOB_INFO, "Text: "SV_Fmt, SV_Arg(text));
            nob_log(NOB_INFO, "Predicted Topic: %s", klass_names[predicted_klass]);
            nob_log(NOB_INFO, "Actual Topic: %s", klass_names[actual_klass]);
            nob_log(NOB_INFO, "Elapsed Time: %.3lfsecs", end - begin);
            nob_log(NOB_INFO, "Success: %zu/%zu (%f)", success, test_samples.count, (float)success/test_samples.count);
            nob_log(NOB_INFO, "Progress: %zu/%zu (%f)", i + 1, test_samples.count, (float)(i + 1)/test_samples.count);
            nob_log(NOB_INFO, "Success rate: %zu/%zu (%f)", success, i + 1, (float)success/(i + 1));
            nob_log(NOB_INFO, "");
        }
    }

    return 0;
}
