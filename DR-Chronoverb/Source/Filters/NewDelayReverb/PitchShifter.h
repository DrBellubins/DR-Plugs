#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <climits>

#include "PitchShifter/PitchShiftingUtils.h"
#include "PitchShifter/ProgressiveOctaveSequence.h"
#include "PitchShifter/PingPongOctaveSequence.h"
#include "PitchShifter/RandomOctaveSequence.h"
#include "PitchShifter/GranularPitchBackend.h"
#include "PitchShifter/PhaseVocoderPitchBackend.h"
#include "PitchShifter/WSOLAPitchBackend.h"

class OctaveEchoPitchShifter
{
public:
    enum class BackendType
    {
        Granular,
        PhaseVocoder,
        WSOLA
    };

    OctaveEchoPitchShifter()
    {
        auto granular = std::make_unique<GranularPitchBackend>();
        granular->SetGrainLengthMilliseconds(35.0f);
        granular->SetJitterPercent(0.15f);
        granular->SetLookbackMultiplier(3.0f);

        auto seq = std::make_unique<ProgressiveOctaveSequence>();
        seq->SetRange(-2, 2);
        seq->SetStartOctave(0);
        seq->SetStepOctaves(1);

        SetSequence(std::move(seq));
        SetBackend(std::move(granular));
    }

    void Prepare(double newSampleRate, int maximumBlockSize)
    {
        sampleRate = newSampleRate;
        maximumBlockSizeCached = maximumBlockSize;

        if (backend != nullptr)
            backend->Prepare(sampleRate, maximumBlockSize);

        Reset();
    }

    void Reset()
    {
        pendingSequence.reset();
        hasPendingSequence = false;

        if (sequence != nullptr)
            sequence->Reset();

        if (backend != nullptr)
            backend->Reset();
    }

    void SetEnabled(bool shouldBeEnabled)
    {
        enabled.store(shouldBeEnabled, std::memory_order_release);
    }

    bool GetEnabled() const
    {
        return enabled.load(std::memory_order_acquire);
    }

    // -----------------------------------------------------------------------
    // Called at each echo boundary. This is the ONLY place pitch is allowed
    // to change — never mid-echo.
    // -----------------------------------------------------------------------
    void OnNewEchoBoundary()
    {
        // Commit a staged sequence change cleanly at the echo boundary.
        if (hasPendingSequence && pendingSequence != nullptr)
        {
            sequence = std::move(pendingSequence);
            hasPendingSequence = false;

            if (backend != nullptr)
                backend->OnEchoBoundary(sequence->GetCurrentPitchRatio());

            return; // Don't advance yet — let the new sequence start from step 0.
        }

        // Normal boundary: advance sequence first, then tell the backend.
        if (sequence != nullptr)
            sequence->AdvanceToNextEcho();

        if (backend != nullptr)
            backend->OnEchoBoundary(sequence != nullptr ? sequence->GetCurrentPitchRatio() : 1.0f);
    }

    // Mono-linked mode: right channel mirrors the left channel's ratio.
    void OnNewEchoBoundaryMirrored(float mirroredRatio)
    {
        CommitPendingSequenceIfAny();

        if (backend != nullptr)
            backend->OnEchoBoundary(mirroredRatio);
    }

    float ProcessSample(float inputSample)
    {
        if (!GetEnabled())
            return inputSample;

        if (sequence == nullptr || backend == nullptr)
            return inputSample;

        return backend->ProcessSample(inputSample, sequence->GetCurrentPitchRatio());
    }

    // Stages a new sequence; it will be committed at the next echo boundary.
    void SetSequence(std::unique_ptr<IPitchSequence> newSequence)
    {
        pendingSequence = std::move(newSequence);

        if (pendingSequence != nullptr)
            pendingSequence->Reset();

        hasPendingSequence = true;
    }

    void SetBackendType(BackendType newBackendType)
    {
        if (newBackendType == BackendType::PhaseVocoder)
        {
            auto phaseVocoder = std::make_unique<PhaseVocoderPitchBackend>();
            phaseVocoder->Prepare(sampleRate, maximumBlockSizeCached);
            SetBackend(std::move(phaseVocoder));
        }
        else if (newBackendType == BackendType::WSOLA)
        {
            auto wsola = std::make_unique<WSOLAPitchBackend>();
            wsola->Prepare(sampleRate, maximumBlockSizeCached);
            SetBackend(std::move(wsola));
        }
        else
        {
            auto granular = std::make_unique<GranularPitchBackend>();
            granular->SetGrainLengthMilliseconds(35.0f);
            granular->SetJitterPercent(0.15f);
            granular->SetLookbackMultiplier(3.0f);
            SetBackend(std::move(granular));
        }
    }

    // Commits a pending sequence immediately — only safe outside the audio thread.
    void CommitPendingSequenceNow()
    {
        if (hasPendingSequence && pendingSequence != nullptr)
        {
            sequence = std::move(pendingSequence);
            hasPendingSequence = false;

            if (backend != nullptr)
                backend->SetInitialRatio(sequence->GetCurrentPitchRatio());
        }
    }

    void CommitPendingSequenceIfAny()
    {
        if (hasPendingSequence && pendingSequence != nullptr)
        {
            sequence = std::move(pendingSequence);
            hasPendingSequence = false;

            if (backend != nullptr)
                backend->Reset();

            if (sequence != nullptr)
                sequence->Reset();

            if (backend != nullptr)
                backend->SetInitialRatio(sequence != nullptr ? sequence->GetCurrentPitchRatio() : 1.0f);
        }
    }

    void ResetSequenceAndBackendToCurrentState()
    {
        if (sequence != nullptr)
            sequence->Reset();

        if (backend != nullptr)
            backend->Reset();

        if (backend != nullptr)
            backend->SetInitialRatio(sequence != nullptr ? sequence->GetCurrentPitchRatio() : 1.0f);
    }

    void SetBackend(std::unique_ptr<IPitchShifterBackend>&& newBackend)
    {
        backend = std::move(newBackend);

        if (backend != nullptr)
        {
            backend->Prepare(sampleRate, maximumBlockSizeCached);
            backend->Reset();
            backend->SetInitialRatio(sequence != nullptr ? sequence->GetCurrentPitchRatio() : 1.0f);
        }
    }

    float GetLatencyMilliseconds() const
    {
        if (!GetEnabled() || backend == nullptr)
            return 0.0f;

        return backend->GetLatencyMilliseconds();
    }

    float GetCurrentPitchRatio() const
    {
        if (sequence == nullptr) return 1.0f;
        return sequence->GetCurrentPitchRatio();
    }

    // Debug functions
    int GetBackendUnderflowCount() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetUnderflowCount();

        return -1;
    }

    int GetBackendCausalGuardRejectCount() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetCausalGuardRejectCount();

        return -1;
    }

    float GetBackendLastBestMatchError() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetLastBestMatchError();

        return -1.0f;
    }

        float GetCurrentReadWeight() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetCurrentReadWeight();

        return -1.0f;
    }

    float GetAverageReadWeightWindow() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetAverageReadWeightWindow();

        return -1.0f;
    }

    float GetReadDistanceBehindWriteForDebug() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetReadDistanceBehindWriteForDebug();

        return -1.0f;
    }

    int GetSamplesUntilNextSegmentForDebug() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetSamplesUntilNextSegmentForDebug();

        return -1;
    }

    int GetSegmentLengthSamplesForDebug() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetSegmentLengthSamplesForDebug();

        return -1;
    }

    int GetOverlapSamplesForDebug() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetOverlapSamplesForDebug();

        return -1;
    }

    int GetSynthesisHopSamplesForDebug() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetSynthesisHopSamplesForDebug();

        return -1;
    }

    int GetStretchWriteCursorForDebug() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetStretchWriteCursorForDebug();

        return -1;
    }

    float GetStretchReadIndexForDebug() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetStretchReadIndexForDebug();

        return -1.0f;
    }

    float GetDebugMinReadWeight() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetDebugMinReadWeight();

        return -1.0f;
    }

    float GetDebugMaxReadWeight() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetDebugMaxReadWeight();

        return -1.0f;
    }

    float GetTargetReadDistanceSamples() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetTargetReadDistanceSamplesForDebug();

        return -1.0f;
    }

    int GetLastBestMatchDelta() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetLastBestMatchDelta();

        return -1;
    }

    float GetPredictedSourceIndexForDebug() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetPredictedSourceIndexForDebug();

        return -1.0f;
    }

    float GetLastChosenSourceIndexForDebug() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetLastChosenSourceIndexForDebug();

        return -1.0f;
    }

    float GetAnalysisHopForDebug() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetAnalysisHopForDebug();

        return -1.0f;
    }

    float GetStretchFactorForDebug() const
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            return wsola->GetStretchFactorForDebug();

        return -1.0f;
    }

    void ResetDebugReadWeightExtrema()
    {
        if (auto* wsola = dynamic_cast<WSOLAPitchBackend*>(backend.get()))
            wsola->ResetDebugReadWeightExtrema();
    }

private:
    double sampleRate = 48000.0;
    int maximumBlockSizeCached = 0;
    bool hasPendingSequence = false;

    std::unique_ptr<IPitchSequence>      sequence;
    std::unique_ptr<IPitchSequence>      pendingSequence;
    std::unique_ptr<IPitchShifterBackend> backend;

    std::atomic<bool> enabled { false };
};
