#pragma once

#include <JuceHeader.h>

#include "Analysis.h"

#include <array>
#include <atomic>
#include <vector>

class MixTeacherAudioProcessor final : public juce::AudioProcessor
{
public:
    MixTeacherAudioProcessor();
    ~MixTeacherAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }
    mixteacher::AnalysisSnapshot getLatestAnalysisSnapshot();
    void resetAnalysis();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void applyGoodizer(juce::AudioBuffer<float>& buffer);
    juce::String getChoiceText(const char* parameterID) const;
    int getSpectrumFftSize() const;
    int getSpectrumFftOrder() const;
    void writeSampleToFifo(float sample) noexcept;
    void drainFifo();
    void appendToHistory(float sample);
    int findRecentActiveOffset(float threshold) const;
    void updateHistoryLevels(mixteacher::AnalysisSnapshot& snapshot);
    void updateSpectrum(mixteacher::AnalysisSnapshot& snapshot);
    void updateDynamics(mixteacher::AnalysisSnapshot& snapshot);
    void updateValidity(mixteacher::AnalysisSnapshot& snapshot);
    void updateTrackDetection(mixteacher::AnalysisSnapshot& snapshot);
    void updateDrumProfile(mixteacher::AnalysisSnapshot& snapshot);

    juce::AudioProcessorValueTreeState parameters;

    static constexpr int fifoCapacity = 32768;
    static constexpr int maxBlockSamples = 8192;
    static constexpr int maxFftSize = 1 << 13;

    juce::AbstractFifo sampleFifo { fifoCapacity };
    std::array<float, fifoCapacity> sampleFifoBuffer {};
    std::array<float, maxBlockSamples> monoScratch {};

    juce::dsp::FFT fft1024 { 10 };
    juce::dsp::FFT fft2048 { 11 };
    juce::dsp::FFT fft4096 { 12 };
    juce::dsp::FFT fft8192 { 13 };
    juce::dsp::WindowingFunction<float> window1024 { 1 << 10, juce::dsp::WindowingFunction<float>::hann };
    juce::dsp::WindowingFunction<float> window2048 { 1 << 11, juce::dsp::WindowingFunction<float>::hann };
    juce::dsp::WindowingFunction<float> window4096 { 1 << 12, juce::dsp::WindowingFunction<float>::hann };
    juce::dsp::WindowingFunction<float> window8192 { 1 << 13, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, maxFftSize * 2> fftBuffer {};

    std::vector<float> analysisHistory;
    size_t historyWriteIndex = 0;

    std::atomic<float> latestPeakLinear { 0.0f };
    std::atomic<float> latestRmsLinear { 0.0f };
    std::atomic<int> clippingCount { 0 };
    std::atomic<int64_t> analysedSamples { 0 };

    std::array<float, 8> goodizerLowState {};
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixTeacherAudioProcessor)
};
