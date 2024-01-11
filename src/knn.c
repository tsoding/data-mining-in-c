#include <stdio.h>
#include <zlib.h>

#define NOB_IMPLEMENTATION
#include "nob.h"

Nob_String_View deflate_sv(Nob_String_View sv)
{
    void *output = nob_temp_alloc(sv.count*2);

    z_stream defstream = {0};
    defstream.avail_in = (uInt)sv.count;//(uInt)strlen(a)+1; // size of input, string + terminator
    defstream.next_in = (Bytef *)sv.data; // input char array
    defstream.avail_out = (uInt)sv.count*2; // size of output
    defstream.next_out = (Bytef *)output; // output char array

    deflateInit(&defstream, Z_BEST_COMPRESSION);
    deflate(&defstream, Z_FINISH);
    deflateEnd(&defstream);

    return nob_sv_from_parts(output, defstream.total_out);
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

float ncd(Nob_String_View a, Nob_String_View b, float cb)
{
    Nob_String_View ab = nob_sv_from_cstr(nob_temp_sprintf(SV_Fmt SV_Fmt, SV_Arg(a), SV_Arg(b)));
    float ca = deflate_sv(a).count;
    float cab = deflate_sv(ab).count;
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

size_t klassify_sample(Samples train, Nob_String_View text, size_t k)
{
    NCDs ncds = {0};

    float cb = deflate_sv(text).count;
    for (size_t i = 0; i < train.count; ++i) {
        float distance = ncd(train.items[i].text, text, cb);
        nob_temp_reset();
        nob_da_append(&ncds, ((NCD) {
            .distance = distance,
            .klass = train.items[i].klass,
        }));
        printf("\rClassifying %zu/%zu", i, train.count);
    }
    printf("\n");

    qsort(ncds.items, ncds.count, sizeof(*ncds.items), compare_ncds);

    size_t klass_freq[NOB_ARRAY_LEN(klass_names)] = {0};
    for (size_t i = 0; i < ncds.count && i < k; ++i) {
        klass_freq[ncds.items[i].klass] += 1;
    }

    size_t predicted_klass = 0;
    for (size_t i = 1; i < NOB_ARRAY_LEN(klass_names); ++i) {
        if (klass_freq[predicted_klass] < klass_freq[i]) {
            predicted_klass = i;
        }
    }

    return predicted_klass;
}

int main(int argc, char **argv)
{
    const char *program = nob_shift_args(&argc, &argv);

    if (argc <= 0) {
        nob_log(NOB_ERROR, "Usage: %s <train.csv> <test.csv>", program);
        nob_log(NOB_ERROR, "ERROR: no train file is provided");
        return 1;
    }
    const char *train_path = nob_shift_args(&argc, &argv);
    Nob_String_Builder train_content = {0};
    if (!nob_read_entire_file(train_path, &train_content)) return 1;
    Samples train_samples = parse_samples(nob_sv_from_parts(train_content.items, train_content.count));

    if (argc <= 0) {
        nob_log(NOB_ERROR, "Usage: %s <train.csv> <test.csv>", program);
        nob_log(NOB_ERROR, "ERROR: no test file is provided");
        return 1;
    }
    const char *test_path = nob_shift_args(&argc, &argv);
    Nob_String_Builder test_content = {0};
    if (!nob_read_entire_file(test_path, &test_content)) return 1;
    Samples test_samples = parse_samples(nob_sv_from_parts(test_content.items, test_content.count));

    // const char *text = "Investigation into why a panel blew off a Boeing Max 9 jet focuses on missing bolts. Federal regulators are extending the grounding of some Boeing jets after an Alaska Airlines plane lost a side panel last week.";
    //const char *text = "Tennessee Titans fire coach Mike Vrabel after back-to-back losing seasons The Tennessee Titans have fired coach Mike Vrabel after six seasons with the franchise having won only six of the past 24 games.";
    // const char *text = "Stock market today: Asian shares retreat after a lackluster day on Wall St, but Tokyo jumps 2%. Asian shares retreated Wednesday after a lackluster session on Wall Street, though Tokyo broke ranks, gaining more than 2% as a weaker yen lifted stock prices for export manufacturers.";
    // const char *text = "NASA postpones landing astronauts on the moon until at least 2026. Astronauts will have to wait until next year before flying to the moon and at least two years before landing on it.";
    //const char *text = "Taters the cat steals the show in first video sent by laser from deep space. An orange tabby cat named Taters stars in the first video sent by laser from deep space, stealing the show as he chases a red laser light.";
    // const char *text = "Startup firm Patronus creates diagnostic tool to catch genAI mistakes Patronus' SimpleSafetyTests checks outputs from AI chatbots and other LLM-based tools to detect anomalies. The goal is to evaluate whether a model is going to fail — or is already failing.";
    const char *text = "Spam suspension hits Sohu.com shares (FT.com),FT.com - Shares in Sohu.com, a leading US-listed Chinese internet portal, fell more than 10 per cent on Friday after China's biggest mobile phone network operator imposed a one-year suspension on its multimedia messaging services because of customers being sent spam.";
    size_t klass = klassify_sample(train_samples, nob_sv_from_cstr(text), 3);
    nob_log(NOB_INFO, "Text: %s", text);
    nob_log(NOB_INFO, "Klass: %s", klass_names[klass]);

    // Sample sample = test_samples.items[0];
    // size_t predicted_klass = klassify_sample(train_samples, sample.text, 2);

    // nob_log(NOB_INFO, "Text: "SV_Fmt, SV_Arg(sample.text));
    // nob_log(NOB_INFO, "Predicted klass: %s", klass_names[predicted_klass]);
    // nob_log(NOB_INFO, "Actual klass: %s", klass_names[sample.klass]);

    return 0;
}
