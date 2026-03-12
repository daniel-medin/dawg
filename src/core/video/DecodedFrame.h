#pragma once

#include <opencv2/core/mat.hpp>

struct DecodedFrame
{
    int index = -1;
    double timestampSeconds = 0.0;
    cv::Mat bgr;

    [[nodiscard]] bool isValid() const
    {
        return index >= 0 && !bgr.empty();
    }
};

