#pragma once
// A stand-in for a DAW's transport, shared by the offline tests.
#include <juce_audio_processors/juce_audio_processors.h>

constexpr double kTestBpm = 130.0; // the fake DAW runs at 130 bpm

struct FakeHost : juce::AudioPlayHead
{
    double ppq = 0.0;            // song position (quarter notes) at the start of the next block
    double bpm = kTestBpm;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setIsPlaying(playing);
        return info;
    }
};
