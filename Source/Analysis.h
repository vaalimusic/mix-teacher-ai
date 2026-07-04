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
    monoCompatibility,
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
    int spectrumFftSize = 4096;
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
    float activeWindowRatio = 0.0f;
    float noiseFloorDb = -120.0f;
    float transientScore = 0.0f;
    float transientDensity = 0.0f;
    float sibilanceSpikeScore = 0.0f;
    float sibilancePeakDb = -120.0f;
    float stereoCorrelation = 1.0f;
    float stereoWidth = 0.0f;
    float monoFoldDownLossDb = 0.0f;
    int onsetCount = 0;
    int clippingCount = 0;
    bool clippingDetected = false;

    std::array<float, waveformBins> waveform {};
    std::array<float, spectrumBins> spectrum {};
    std::array<float, dynamicsBins> dynamics {};
    std::array<float, dynamicsBins> lowEndCurve {};
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

struct HubTrackSummary
{
    int instanceId = 0;
    juce::String manualType = "auto";
    juce::String detectedType = "unknown";
    juce::String effectiveType = "unknown";
    float typeConfidence = 0.0f;
    float peakDbfs = -120.0f;
    float rmsDbfs = -120.0f;
    float lufsShortTerm = -120.0f;
    float crestFactorDb = 0.0f;
    float headroomDb = 120.0f;
    float stereoCorrelation = 1.0f;
    float stereoWidth = 0.0f;
    float monoFoldDownLossDb = 0.0f;
    bool clippingDetected = false;
    bool validForAnalysis = false;
    BandEnergyDb bandDb;
    std::array<float, dynamicsBins> lowEndCurve {};
    DrumProfile drumProfile;
    float transientScore = 0.0f;
    double ageSec = 0.0;
};

struct MixHubIssue
{
    Severity severity = Severity::ok;
    juce::String title;
    juce::String detail;
    juce::String action;
};

struct HubFrequencyConflict
{
    int trackAId = 0;
    int trackBId = 0;
    juce::String trackAType;
    juce::String trackBType;
    juce::String band;
    int bandIndex = -1;
    float strength = 0.0f;
    Severity severity = Severity::info;
};

struct HubLowEndTiming
{
    bool available = false;
    int trackAId = 0;
    int trackBId = 0;
    juce::String trackAType;
    juce::String trackBType;
    float simultaneousRatio = 0.0f;
    float weightedOverlap = 0.0f;
    float risk = 0.0f;
    int activeBins = 0;
    int overlapBins = 0;
    Severity severity = Severity::info;
    std::array<float, dynamicsBins> curve {};
};

struct MixHubSnapshot
{
    juce::String plugin = "Mix Teacher AI";
    juce::String version = "0.4";
    juce::String language = "ru";
    int trackCount = 0;
    float tonalLowDb = -120.0f;
    float tonalLowMidDb = -120.0f;
    float tonalMidDb = -120.0f;
    float tonalHighDb = -120.0f;
    float tonalTiltDb = 0.0f;
    std::vector<HubTrackSummary> tracks;
    std::vector<MixHubIssue> issues;
    std::vector<HubFrequencyConflict> frequencyConflicts;
    HubLowEndTiming lowEndTiming;
    juce::String firstStepTitle;
    juce::String firstStepAction;

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
