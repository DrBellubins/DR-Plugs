#pragma once

#include <vector>
#include <memory>
#include <cmath>

// SimpleFDN
// Minimal placeholder Feedback Delay Network:
// - N delay lines with fixed taps and scattering matrix (identity or simple permutation).
// - Currently NOT integrated into NewDelayReverb core path; provided for future expansion.
// - You can route diffusion output into FDN and then back to main delay later.
//
// This is intentionally basic to match the "implement basic functionality" request.
class SimpleFDN
{
public:
    SimpleFDN()
    {
    }

    void Prepare(double sampleRate)
    {
        sr = sampleRate;
        setup(4); // minimal 4-line FDN
    }

    float ProcessSample(float inputSample)
    {
        if (lines.empty())
        {
            return inputSample;
        }

        // Inject input into first line
        lines[0].push(inputSample);

        // Read and scatter
        float sum = 0.0f;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            sum += lines[i].read();
            // Simple permutation: feed forward a bit
            const float fb = lines[i].read() * 0.25f;
            size_t next = (i + 1) % lines.size();
            lines[next].push(fb);
        }

        // Output sum scaled
        return 0.5f * sum;
    }

private:
    struct Line
    {
        std::vector<float> buf;
        int w = 0;
        int d = 100; // samples

        void ensure(int size)
        {
            if (static_cast<int>(buf.size()) < size)
            {
                buf.resize(size, 0.0f);
                w = 0;
            }
        }

        void push(float x)
        {
            if (buf.empty())
            {
                return;
            }
            buf[w] = x;
            w = (w + 1) % static_cast<int>(buf.size());
        }

        float read() const
        {
            if (buf.empty())
            {
                return 0.0f;
            }
            int size = static_cast<int>(buf.size());
            int r = w - d;
            while (r < 0) r += size;
            r %= size;
            return buf[r];
        }
    };

    double sr = 48000.0;
    std::vector<Line> lines;

    void setup(int n)
    {
        lines.clear();
        lines.resize(static_cast<size_t>(std::max(1, n)));

        for (size_t i = 0; i < lines.size(); ++i)
        {
            lines[i].ensure(1024);
            lines[i].d = 80 + static_cast<int>(i) * 40;
        }
    }
};