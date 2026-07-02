#pragma once

#include <JuceHeader.h>

#include "Analysis.h"
#include "PluginProcessor.h"

#include <memory>

class MixTeacherAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer,
                                             private juce::Button::Listener
{
public:
    explicit MixTeacherAudioProcessorEditor(MixTeacherAudioProcessor&);
    ~MixTeacherAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void buttonClicked(juce::Button*) override;

    void drawHeader(juce::Graphics&, juce::Rectangle<int>);
    void drawWaveform(juce::Graphics&, juce::Rectangle<int>);
    void drawSpectrum(juce::Graphics&, juce::Rectangle<int>);
    void drawProblemSnapshot(juce::Graphics&, juce::Rectangle<int>);
    void drawDynamicsGraph(juce::Graphics&, juce::Rectangle<int>);
    void drawBandGraph(juce::Graphics&, juce::Rectangle<int>);
    void drawLevels(juce::Graphics&, juce::Rectangle<int>);
    void drawTeacher(juce::Graphics&, juce::Rectangle<int>);
    void drawGoodizer(juce::Graphics&, juce::Rectangle<int>);
    void drawSection(juce::Graphics&, juce::Rectangle<int>, const juce::String& title);
    void drawMeter(juce::Graphics&, juce::Rectangle<int>, const juce::String& label, float db, float normalised, juce::Colour colour);

    bool isFrozen() const;
    bool isRussian() const;
    float getGoodizerAmount() const;
    juce::String tr(const char* ruUtf8, const char* en) const;
    void updateProblemSnapshot();
    void drawSpectrumBars(juce::Graphics&, juce::Rectangle<int>, const mixteacher::AnalysisSnapshot&, bool compact);

    MixTeacherAudioProcessor& audioProcessor;
    mixteacher::AnalysisSnapshot snapshot;
    mixteacher::AnalysisSnapshot problemSnapshot;
    bool hasProblemSnapshot = false;
    mixteacher::IssueKind problemSnapshotKind = mixteacher::IssueKind::none;

    juce::ComboBox trackTypeBox;
    juce::ComboBox explanationModeBox;
    juce::ComboBox languageBox;
    juce::Slider sensitivitySlider;
    juce::Slider goodizerSlider;
    juce::ToggleButton freezeButton { "Freeze" };
    juce::TextButton resetButton { "Reset" };
    juce::TextButton copyJsonButton { "Copy JSON" };
    juce::TextButton askAiButton { "Ask AI" };

    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<ComboAttachment> trackTypeAttachment;
    std::unique_ptr<ComboAttachment> explanationModeAttachment;
    std::unique_ptr<ComboAttachment> languageAttachment;
    std::unique_ptr<SliderAttachment> sensitivityAttachment;
    std::unique_ptr<SliderAttachment> goodizerAttachment;
    std::unique_ptr<ButtonAttachment> freezeAttachment;
    float goodizerPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixTeacherAudioProcessorEditor)
};
