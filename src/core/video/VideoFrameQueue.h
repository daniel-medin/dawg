#pragma once

#include <cstddef>
#include <deque>
#include <optional>

#include "core/video/VideoFrame.h"

class VideoFrameQueue
{
public:
    explicit VideoFrameQueue(std::size_t maxSize = 3)
        : m_maxSize(maxSize > 0 ? maxSize : 1)
    {
    }

    void clear()
    {
        m_frames.clear();
    }

    [[nodiscard]] bool empty() const
    {
        return m_frames.empty();
    }

    [[nodiscard]] std::size_t size() const
    {
        return m_frames.size();
    }

    void push(VideoFrame frame)
    {
        if (m_frames.size() >= m_maxSize)
        {
            m_frames.pop_front();
        }

        m_frames.push_back(std::move(frame));
    }

    [[nodiscard]] std::optional<VideoFrame> takeFront()
    {
        if (m_frames.empty())
        {
            return std::nullopt;
        }

        auto frame = std::move(m_frames.front());
        m_frames.pop_front();
        return frame;
    }

    [[nodiscard]] std::optional<VideoFrame> peekFront() const
    {
        if (m_frames.empty())
        {
            return std::nullopt;
        }

        return m_frames.front();
    }

private:
    std::deque<VideoFrame> m_frames;
    std::size_t m_maxSize = 3;
};
