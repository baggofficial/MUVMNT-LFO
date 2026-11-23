#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "Envelope.h"

class FreeLfoProcessor : public juce::AudioProcessor
{
public:
    FreeLfoProcessor();
    ~FreeLfoProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Free LFO Tool"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Message-thread API for the editor
    Envelope getEnvelope() const;
    void setEnvelope(const Envelope& e);

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> uiPhase { 0.f };

    // Input signal min/max per LFO-phase bin (audio thread writes, UI reads).
    // One LFO cycle is split into waveBins slots, so the display lines up with the curve.
    static constexpr int waveBins = 512;
    std::array<std::atomic<float>, waveBins> waveMin {}, waveMax {};
    // Same, for the optional sidechain input (e.g. the kick that triggers the pump).
    std::array<std::atomic<float>, waveBins> scMin {}, scMax {};
    std::atomic<bool> sidechainActive { false };

    static const std::array<const char*, 15> divisionNames;
    static const std::array<double, 15> divisionBeats; // quarter-note beats per LFO cycle

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void syncTable();

    Envelope envelope; // authoritative copy, guarded by envLock
    mutable juce::SpinLock envLock;

    std::array<float, Envelope::tableSize + 1> sharedTable {};
    std::array<float, Envelope::tableSize + 1> audioTable {};
    std::atomic<int> tableVersion { 0 };
    int audioTableVersion = -1;

    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pDivision = nullptr;
    std::atomic<float>* pHz = nullptr;
    std::atomic<float>* pDepth = nullptr;
    std::atomic<float>* pSmooth = nullptr;
    std::atomic<float>* pOffset = nullptr;
    std::atomic<float>* pRetrigger = nullptr;

    double freePhase = 0.0;
    float smoothedGain = 1.f;
    int lastWaveBin = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreeLfoProcessor)
};
