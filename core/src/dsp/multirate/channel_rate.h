#pragma once
#include <algorithm>
#include <cmath>

namespace dsp::multirate {
    inline double channelSampleRate(double inputRate, double minimumRate, unsigned int maxDecimation) {
        double outputRate = inputRate;
        unsigned int decimation = 1;
        minimumRate = std::max(1.0, minimumRate);
        while (decimation <= maxDecimation / 2 && outputRate / 2 >= minimumRate) {
            outputRate /= 2;
            decimation *= 2;
        }
        if (minimumRate < outputRate / 2) { return std::min(inputRate, std::ceil(minimumRate)); }
        return outputRate;
    }
}
