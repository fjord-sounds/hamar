#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <complex>
#include <memory>
#include <algorithm>

// Zero-latency convolution for the soundboard: the first block of the impulse response runs as a plain FIR, the rest
// as a uniformly partitioned convolution in the frequency domain (overlap-save) whose one block of latency is exactly
// the offset of the tail. The impulse response lives in a separately built Kernel, so a new one can be prepared on
// another thread and swapped in without allocating on the audio thread.
namespace hamar::dsp
{
    class Convolver
    {
    public:
        // A ready-to-use impulse response: FIR head + spectra of the tail partitions
        struct Kernel
        {
            int block = 128, partitions = 0;
            bool hasHead = true;                               // false: the first block is silent (skip the FIR)
            std::vector<float> head;                           // block taps
            std::vector<std::complex<float>> spectra;          // partitions x (block + 1)

            void build (const float* ir, int length, int blockSize, juce::dsp::FFT& fft)
            {
                block = blockSize;
                head.assign ((size_t) block, 0.0f);
                hasHead = false;
                for (int i = 0; i < std::min (length, block); ++i) { head[(size_t) i] = ir[i]; hasHead |= ir[i] != 0.0f; }
                const int tail = std::max (0, length - block);
                partitions = (tail + block - 1) / block;
                spectra.assign ((size_t) partitions * (size_t) (block + 1), {});
                std::vector<float> work ((size_t) block * 4, 0.0f);
                for (int p = 0; p < partitions; ++p)
                {
                    std::fill (work.begin(), work.end(), 0.0f);
                    for (int i = 0; i < block; ++i)
                    {
                        const int src = block + p * block + i;
                        if (src < length) work[(size_t) i] = ir[src];
                    }
                    fft.performRealOnlyForwardTransform (work.data(), true);
                    auto* dst = spectra.data() + (size_t) p * (size_t) (block + 1);
                    for (int k = 0; k <= block; ++k) dst[k] = { work[(size_t) (2 * k)], work[(size_t) (2 * k + 1)] };
                }
            }
        };

        static int blockFor (double sampleRate) { return sampleRate <= 50000.0 ? 128 : sampleRate <= 100000.0 ? 256 : 512; }

        // maxPartitions: the longest response that will ever be loaded
        void prepare (int blockSize, int maxPartitions)
        {
            block = blockSize;
            int order = 0;
            while ((1 << order) < 2 * block) ++order;
            fft = std::make_unique<juce::dsp::FFT> (order);
            history.assign ((size_t) block * 2, 0.0f);
            input.assign ((size_t) block * 2, 0.0f);
            work.assign ((size_t) block * 4, 0.0f);
            acc.assign ((size_t) block + 1, {});
            fdl.assign ((size_t) std::max (1, maxPartitions) * (size_t) (block + 1), {});
            maxParts = std::max (1, maxPartitions);
            outCur.assign ((size_t) block, 0.0f);
            outNext.assign ((size_t) block, 0.0f);
            reset();
        }

        juce::dsp::FFT& getFft() { return *fft; }

        void reset()
        {
            std::fill (history.begin(), history.end(), 0.0f);
            std::fill (input.begin(), input.end(), 0.0f);
            std::fill (fdl.begin(), fdl.end(), std::complex<float> {});
            std::fill (outCur.begin(), outCur.end(), 0.0f);
            std::fill (outNext.begin(), outNext.end(), 0.0f);
            pos = 0;
            fdlIndex = 0;
            histPos = 0;
        }

        void setKernel (const Kernel* k) { kernel = k; }

        float process (float x)
        {
            // FIR head over a doubled history (contiguous reads)
            history[(size_t) histPos] = x;
            history[(size_t) (histPos + block)] = x;
            float y = outCur[(size_t) pos];
            if (kernel != nullptr && kernel->hasHead)
            {
                const float* h = kernel->head.data();
                const float* xs = history.data() + histPos + block;   // xs[0] = newest, xs[-k] = k samples ago
                float sum = 0.0f;
                for (int k = 0; k < block; ++k) sum += h[k] * xs[-k];
                y += sum;
            }
            histPos = (histPos + 1) % block;

            input[(size_t) (block + pos)] = x;
            if (++pos == block)
            {
                pos = 0;
                runBlock();
                std::copy (input.begin() + block, input.end(), input.begin());   // overlap-save: keep the last block
            }
            return y;
        }

    private:
        void runBlock()
        {
            std::copy (input.begin(), input.end(), work.begin());
            std::fill (work.begin() + 2 * block, work.end(), 0.0f);
            fft->performRealOnlyForwardTransform (work.data(), true);
            fdlIndex = (fdlIndex + maxParts - 1) % maxParts;
            auto* slot = fdl.data() + (size_t) fdlIndex * (size_t) (block + 1);
            for (int k = 0; k <= block; ++k) slot[k] = { work[(size_t) (2 * k)], work[(size_t) (2 * k + 1)] };

            std::fill (acc.begin(), acc.end(), std::complex<float> {});
            if (kernel != nullptr)
            {
                const int parts = std::min (kernel->partitions, maxParts);
                for (int p = 0; p < parts; ++p)
                {
                    const auto* x = fdl.data() + (size_t) ((fdlIndex + p) % maxParts) * (size_t) (block + 1);
                    const auto* h = kernel->spectra.data() + (size_t) p * (size_t) (block + 1);
                    for (int k = 0; k <= block; ++k) acc[(size_t) k] += x[k] * h[k];
                }
            }
            for (int k = 0; k <= block; ++k) { work[(size_t) (2 * k)] = acc[(size_t) k].real(); work[(size_t) (2 * k + 1)] = acc[(size_t) k].imag(); }
            fft->performRealOnlyInverseTransform (work.data());
            // The second half is the valid (non-wrapped) part; it belongs to the next block (the tail starts one block late)
            for (int i = 0; i < block; ++i) outCur[(size_t) i] = work[(size_t) (block + i)];
        }

        std::unique_ptr<juce::dsp::FFT> fft;
        const Kernel* kernel = nullptr;
        int block = 128, maxParts = 1, pos = 0, fdlIndex = 0, histPos = 0;
        std::vector<float> history, input, work, outCur, outNext;
        std::vector<std::complex<float>> fdl, acc;
    };
}
