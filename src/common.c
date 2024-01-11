static inline float rand_float(void)
{
    return (float)rand()/RAND_MAX;
}

static Color colors[] = {
    GOLD,
    PINK,
    MAROON,
    LIME,
    SKYBLUE,
    VIOLET,
};
#define colors_count NOB_ARRAY_LEN(colors)
