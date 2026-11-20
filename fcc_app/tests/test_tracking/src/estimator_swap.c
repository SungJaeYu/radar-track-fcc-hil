#include <assert.h>
#include <string.h>

#include "tracking/track.h"

static unsigned int starts, predictions, corrections;

const char *estimator_name(void) { return "test_stub"; }

void estimator_init(struct estimator *p, float range_m, float azimuth_rad)
{
    *p = (struct estimator){.values = {range_m, azimuth_rad, 20.0f, 0.0f}};
    starts++;
}

void estimator_predict(struct estimator *p, float dt_s)
{
    p->values[0] += p->values[2] * dt_s;
    predictions++;
}

void estimator_correct(struct estimator *p, float range_m, float azimuth_rad)
{
    p->values[0] = range_m;
    p->values[1] = azimuth_rad;
    corrections++;
}

struct estimator_output estimator_get_output(const struct estimator *p)
{
    return (struct estimator_output){p->values[0], p->values[1], p->values[2], p->values[3]};
}

int main(void)
{
    struct track a = {0}, b = {0};
    track_init(&a);
    assert(track_predict(&a, 100) == TRACK_PREDICT_RESULT_EMPTY);
    assert(predictions == 0);
    track_start(&a, 1000.0f, 0.5f, 100);
    track_start(&b, 2000.0f, -0.5f, 100);
    assert(starts == 2 && strcmp(estimator_name(), "test_stub") == 0);
    assert(track_predict(&a, 200) == TRACK_PREDICT_RESULT_PREDICTED);
    assert(estimator_get_output(&a.estimator).range_m == 1002.0f);
    assert(estimator_get_output(&b.estimator).range_m == 2000.0f);
    assert(predictions == 1);
    assert(track_predict(&a, 200) == TRACK_PREDICT_RESULT_TIME_REJECTED);
    assert(predictions == 1);
    track_hit(&a, 1001.0f, 0.6f);
    track_hit(&a, 1003.0f, 0.7f);
    assert(a.state == TRACK_STATE_CONFIRMED && corrections == 2);
    assert(estimator_get_output(&a.estimator).range_m == 1003.0f);
    track_miss(&a);
    assert(a.state == TRACK_STATE_COASTING && corrections == 2);
    for (int i = 0; i < 4; ++i) track_miss(&a);
    assert(a.state == TRACK_STATE_LOST);
    track_hit(&a, 9000.0f, 0.0f);
    assert(corrections == 2);
    assert(track_predict(&a, 300) == TRACK_PREDICT_RESULT_RESET);
    assert(predictions == 1 && a.state == TRACK_STATE_EMPTY);
    track_start(&a, 3000.0f, 0.0f, 400);
    assert(starts == 3 && estimator_get_output(&a.estimator).range_m == 3000.0f);
    assert(estimator_get_output(&b.estimator).range_m == 2000.0f);
    return 0;
}
