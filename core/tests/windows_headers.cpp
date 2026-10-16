#include <algorithm>
#include <limits>
#include <Windows.h>
#include <module.h>
#include <gui/widgets/waterfall.h>
#include <dsp/multirate/rational_resampler.h>

#if defined(min) || defined(max)
#error Windows min/max macros must be disabled for SDR++ targets
#endif

static_assert(std::min(2, 5) == 2);
static_assert(std::max(2, 5) == 5);
static_assert(std::clamp(7, 2, 5) == 5);
static_assert(std::numeric_limits<int>::max() > 0);

int main() {
    return 0;
}
