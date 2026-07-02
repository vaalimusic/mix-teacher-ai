#pragma once

#include <JuceHeader.h>

#include <array>
#include <vector>

namespace mixteacher
{
constexpr int waveformBins = 128;
constexpr int spectrumBins = 64;
constexpr int dynamicsBins = 96;

enum class Severity
{
    info,
    ok,
    warning,
    problem,
    critical
};

enum class IssueKind
{
    none,
    tooQuiet,
    clipping,
    peak,
    lowHeadroom,
    mud,
    boxiness,
    harshness,
    hatHarshness,
    snareBoxiness,
    drumBoom,
    sibilance,
    dense,
    dynamics,
    transient,
    weakPresence,
    weakLowEnd
};

enum class ValidityState
{
    tooQuiet,
    quietButUsable,
    good
};

struct TeacherIssue
{
    IssueKind kind = IssueKind::none;
    Severity severity = Severity::ok;
    float confidence = 0.0f;
    int priority = 100;
    juce::String title;
    std::vector<juce::String> evidence;
    juce::String why;
    juce::String action;
    juce::String listenCheck;
};

struct BandLevels
{
    float subRumble = 0.0f;
    float lowFoundation = 0.0f;
    float lowBody = 0.0f;
    float mud = 0.0f;
    float boxiness = 0.0f;
    float tone = 0.0f;
    float presence = 0.0f;
    float sibilance = 0.0f;
    float air = 0.0f;
};

struct BandEnergyDb
{
    float subRumble = -120.0f;
    float lowFoundation = -120.0f;
    float lowBody = -120.0f;
    float mud = -120.0f;
    float boxiness = -120.0f;
    float tone = -120.0f;
    float presence = -120.0f;
    float sibilance = -120.0f;
    float air = -120.0f;
};

struct ValidityInfo
{
    ValidityState state = ValidityState::tooQuiet;
    bool isValidForAnalysis = false;
    float confidence = 0.0f;
    std::vector<juce::String> warnings;
};

struct TrackDetection
{
    juce::String manualType = "auto";
    juce::String detectedType = "unknown";
    juce::String effectiveType = "auto";
    float confidence = 0.0f;
    std::vector<juce::String> alternatives;
};

struct FirstStep
{
    juce::String title;
    juce::String action;
};

struct DrumProfile
{
    float kickWeight = 0.0f;
    float kickClick = 0.0f;
    float snareBoxiness = 0.0f;
    float hatHarshness = 0.0f;
    float transientDensity = 0.0f;
    float busCompressionRisk = 0.0f;
};

struct AnalysisSnapshot
{
    juce::String plugin = "Mix Teacher AI";
    juce::String version = "0.2";
    juce::String analysisMode = "realtime";
    juce::String trackType = "auto";
    juce::String explanationMode = "beginner";
    juce::String language = "ru";
    float sensitivity = 0.65f;
    float goodizerAmount = 0.0f;
    double analysisDurationSec = 0.0;

    float peakDbfs = -120.0f;
    float truePeakDbfs = -120.0f;
    float rmsDbfs = -120.0f;
    float lufsShortTerm = -120.0f;
    float crestFactorDb = 0.0f;
    float headroomDb = 120.0f;
    float rmsRangeDb = 0.0f;
    float activeRmsP10 = -120.0f;
    float activeRmsP50 = -120.0f;
    float activeRmsP90 = -120.0f;
    float transientScore = 0.0f;
    int onsetCount = 0;
    int clippingCount = 0;
    bool clippingDetected = false;

    std::array<float, waveformBins> waveform {};
    std::array<float, spectrumBins> spectrum {};
    std::array<float, dynamicsBins> dynamics {};
    BandLevels bands;
    BandEnergyDb bandDb;
    std::vector<juce::String> dominantBands;
    std::vector<juce::String> weakBands;
    ValidityInfo validity;
    TrackDetection track;
    DrumProfile drumProfile;
    std::vector<TeacherIssue> issues;
    FirstStep firstStep;

    juce::String toJson() const;
};

juce::String severityToString(Severity severity);
juce::String issueKindToString(IssueKind kind);
juce::String validityToString(ValidityState state);
juce::String confidenceLabel(float confidence, const juce::String& language);
juce::Colour severityColour(Severity severity);
juce::String classifyBand(float value);
std::vector<TeacherIssue> buildTeacherIssues(const AnalysisSnapshot& snapshot);
FirstStep buildFirstStep(const AnalysisSnapshot& snapshot);
} // namespace mixteacher
