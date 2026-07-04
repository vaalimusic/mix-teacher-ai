#include "PluginEditor.h"

#include <cmath>

namespace
{
juce::Colour background() { return juce::Colour(0xff111318); }
juce::Colour panel() { return juce::Colour(0xff1b2028); }
juce::Colour panelLine() { return juce::Colour(0xff303846); }
juce::Colour textMain() { return juce::Colour(0xffedf2f7); }
juce::Colour textMuted() { return juce::Colour(0xff9aa8b8); }
juce::Colour cyan() { return juce::Colour(0xff35d0ff); }
juce::Colour green() { return juce::Colour(0xff3ddc97); }
juce::Colour goodizerColour(float amount)
{
    return juce::Colour(0xffffc857).interpolatedWith(juce::Colour(0xffff3b30), juce::jlimit(0.0f, 1.0f, amount));
}

bool isDrumSource(const juce::String& type)
{
    return type == "drums" || type == "drums_bus" || type == "kick" || type == "snare";
}

juce::String formatDb(float value)
{
    if (value <= -119.0f)
        return "-inf";

    const auto rounded = static_cast<int>(std::round(value));
    const auto absValue = std::abs(rounded);
    auto digits = juce::String(absValue);

    while (digits.length() < 2)
        digits = "0" + digits;

    return juce::String(rounded < 0 ? "-" : "+") + digits + " dB";
}

float dbToNorm(float db, float floorDb = -60.0f, float ceilingDb = 0.0f)
{
    return juce::jlimit(0.0f, 1.0f, (db - floorDb) / (ceilingDb - floorDb));
}

void setupButton(juce::Button& button)
{
    button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252c36));
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff33475b));
    button.setColour(juce::TextButton::textColourOffId, textMain());
    button.setColour(juce::TextButton::textColourOnId, textMain());
}

void setupCombo(juce::ComboBox& box)
{
    box.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff252c36));
    box.setColour(juce::ComboBox::outlineColourId, panelLine());
    box.setColour(juce::ComboBox::textColourId, textMain());
    box.setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff252c36));
    box.setColour(juce::PopupMenu::textColourId, textMain());
}

void setupKnob(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffffc857));
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff303846));
    slider.setColour(juce::Slider::thumbColourId, textMain());
}
} // namespace

MixTeacherAudioProcessorEditor::MixTeacherAudioProcessorEditor(MixTeacherAudioProcessor& processor)
    : AudioProcessorEditor(&processor), audioProcessor(processor)
{
    setSize(980, 820);

    setupCombo(pluginRoleBox);
    setupCombo(trackTypeBox);
    setupCombo(explanationModeBox);
    setupCombo(languageBox);
    setupCombo(spectrumFftBox);
    setupKnob(goodizerSlider);

    pluginRoleBox.addItemList({ "Track", "Mix Hub" }, 1);
    trackTypeBox.addItemList({ "Auto", "Vocal", "Drums", "Drums Bus", "Kick", "Snare", "Bass", "Guitar", "Piano", "Synth", "FX", "Master" }, 1);
    explanationModeBox.addItemList({ "Beginner", "Intermediate", "Advanced" }, 1);
    languageBox.addItemList({ juce::String::fromUTF8("Русский"), "English" }, 1);
    spectrumFftBox.addItemList({ "1024", "2048", "4096", "8192" }, 1);

    addAndMakeVisible(pluginRoleBox);
    addAndMakeVisible(trackTypeBox);
    addAndMakeVisible(explanationModeBox);
    addAndMakeVisible(languageBox);
    addAndMakeVisible(spectrumFftBox);
    addAndMakeVisible(goodizerSlider);

    setupButton(freezeButton);
    setupButton(resetButton);
    setupButton(copyJsonButton);
    setupButton(askAiButton);

    addAndMakeVisible(freezeButton);
    addAndMakeVisible(resetButton);
    addAndMakeVisible(copyJsonButton);
    addAndMakeVisible(askAiButton);

    resetButton.addListener(this);
    copyJsonButton.addListener(this);
    askAiButton.setEnabled(false);
    askAiButton.setTooltip("AI/Ollama integration is planned for v0.2.");

    pluginRoleAttachment = std::make_unique<ComboAttachment>(audioProcessor.getParameters(), "pluginRole", pluginRoleBox);
    trackTypeAttachment = std::make_unique<ComboAttachment>(audioProcessor.getParameters(), "trackType", trackTypeBox);
    explanationModeAttachment = std::make_unique<ComboAttachment>(audioProcessor.getParameters(), "explanationMode", explanationModeBox);
    languageAttachment = std::make_unique<ComboAttachment>(audioProcessor.getParameters(), "language", languageBox);
    spectrumFftAttachment = std::make_unique<ComboAttachment>(audioProcessor.getParameters(), "spectrumFftSize", spectrumFftBox);
    goodizerAttachment = std::make_unique<SliderAttachment>(audioProcessor.getParameters(), "goodizer", goodizerSlider);
    freezeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.getParameters(), "freeze", freezeButton);

    snapshot = audioProcessor.getLatestAnalysisSnapshot();
    displayedSnapshot = snapshot;
    hubSnapshot = audioProcessor.getMixHubSnapshot();
    displayedHubSnapshot = hubSnapshot;
    smoothedSpectrum = snapshot.spectrum;
    updateProblemSnapshot();
    startTimerHz(24);
}

MixTeacherAudioProcessorEditor::~MixTeacherAudioProcessorEditor()
{
    resetButton.removeListener(this);
    copyJsonButton.removeListener(this);
}

void MixTeacherAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(background());

    auto bounds = getLocalBounds().reduced(16);
    drawHeader(g, bounds.removeFromTop(74));
    bounds.removeFromTop(10);

    if (isMixHubMode())
    {
        drawMixHub(g, bounds);
        drawTitlePopup(g);
        return;
    }

    auto top = bounds.removeFromTop(150);
    drawWaveform(g, top.removeFromLeft((bounds.getWidth() - 10) / 2));
    top.removeFromLeft(10);
    drawSpectrum(g, top);
    drawGoodizer(g, goodizerSlider.getBounds().expanded(34));

    bounds.removeFromTop(10);
    auto middle = bounds.removeFromTop(178);
    drawLevels(g, middle.removeFromLeft(296));
    middle.removeFromLeft(10);
    drawTeacher(g, middle);

    bounds.removeFromTop(10);
    auto analysisRow = bounds.removeFromTop(112);
    drawDynamicsGraph(g, analysisRow.removeFromLeft((bounds.getWidth() - 10) / 2));
    analysisRow.removeFromLeft(10);
    drawBandGraph(g, analysisRow);

    bounds.removeFromTop(10);
    drawProblemSnapshot(g, bounds.removeFromTop(94));

    bounds.removeFromTop(10);
    auto footer = bounds.removeFromTop(56);
    drawSection(g, footer, tr("ДОВЕРИЕ / ПЕРВЫЙ ШАГ", "TRUST / FIRST STEP"));
    auto footerText = footer.reduced(14, 28);
    g.setColour(mixteacher::severityColour(snapshot.validity.isValidForAnalysis ? mixteacher::Severity::ok : mixteacher::Severity::warning));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawFittedText(tr("Доверие: ", "Trust: ") + mixteacher::confidenceLabel(snapshot.validity.confidence, snapshot.language)
                         + "   " + displayedSnapshot.firstStep.title,
                     footerText.removeFromTop(18),
                     juce::Justification::centredLeft,
                     1);
    g.setColour(textMuted());
    g.setFont(12.0f);
    g.drawFittedText(displayedSnapshot.firstStep.action, footerText, juce::Justification::centredLeft, 1);

    drawTitlePopup(g);
}

void MixTeacherAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(16);
    auto header = bounds.removeFromTop(74);
    auto titleArea = header.reduced(18, 12);
    titleClickArea = titleArea.removeFromLeft(250);
    titlePopupArea = juce::Rectangle<int>(titleClickArea.getX(),
                                          titleClickArea.getBottom() + 10,
                                          330,
                                          168)
                         .constrainedWithin(getLocalBounds().reduced(16));

    auto controls = header.withTrimmedLeft(290).withTrimmedRight(18);
    const auto controlY = controls.getY() + 36;
    const auto controlH = 30;
    auto x = controls.getX();

    pluginRoleBox.setBounds(x, controlY, 100, controlH);
    x += 108;
    trackTypeBox.setBounds(x, controlY, 150, controlH);
    x += 158;
    explanationModeBox.setBounds(x, controlY, 116, controlH);
    x += 124;
    languageBox.setBounds(x, controlY, 112, controlH);
    x += 122;
    spectrumFftBox.setBounds(x, controlY, 76, controlH);
    x += 86;
    freezeButton.setBounds(x, controlY, 68, controlH);

    const auto topY = header.getBottom() + 20;
    goodizerSlider.setBounds(getWidth() / 2 - 44, topY + 34, 88, 88);
    goodizerSlider.setVisible(!isMixHubMode());

    bounds.removeFromTop(10 + 150 + 10 + 178 + 10 + 112 + 10 + 94 + 10 + 56 + 10);
    auto buttons = bounds.removeFromTop(40).removeFromRight(368).withHeight(34);
    resetButton.setBounds(buttons.removeFromLeft(82));
    buttons.removeFromLeft(8);
    copyJsonButton.setBounds(buttons.removeFromLeft(110));
    buttons.removeFromLeft(8);
    askAiButton.setBounds(buttons.removeFromLeft(82));
}

void MixTeacherAudioProcessorEditor::mouseMove(const juce::MouseEvent& event)
{
    const auto position = event.getPosition();
    const auto overPopupButton = titlePopupVisible && thanksButtonArea.contains(position);
    setMouseCursor(titleClickArea.contains(position) || overPopupButton ? juce::MouseCursor::PointingHandCursor
                                                                        : juce::MouseCursor::NormalCursor);
}

void MixTeacherAudioProcessorEditor::mouseExit(const juce::MouseEvent&)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void MixTeacherAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    if (titleClickArea.contains(position))
    {
        titlePopupVisible = !titlePopupVisible;
        handshakeActive = false;
        handshakeProgress = 0.0f;

        if (titlePopupVisible)
            titlePopupAmount = 0.0f;

        repaint();
        return;
    }

    if (titlePopupVisible && thanksButtonArea.contains(position))
    {
        handshakeActive = true;
        handshakeProgress = 0.0f;
        repaint();
        return;
    }

    if (titlePopupVisible && !titlePopupArea.expanded(10).contains(position))
    {
        titlePopupVisible = false;
        handshakeActive = false;
        repaint();
    }
}

void MixTeacherAudioProcessorEditor::timerCallback()
{
    if (!isFrozen())
    {
        snapshot = audioProcessor.getLatestAnalysisSnapshot();
        hubSnapshot = audioProcessor.getMixHubSnapshot();
        updateDisplayedSnapshot();
        updateDisplayedHubSnapshot();
        updateSmoothedSpectrum();
        updateProblemSnapshot();
    }

    freezeButton.setButtonText(tr("Стоп", "Freeze"));
    resetButton.setButtonText(tr("Сброс", "Reset"));
    copyJsonButton.setButtonText(tr("JSON", "Copy JSON"));

    const auto goodizerAmount = getGoodizerAmount();
    goodizerPhase = std::fmod(goodizerPhase + 0.025f + goodizerAmount * 0.08f, 1.0f);
    goodizerSlider.setColour(juce::Slider::rotarySliderFillColourId, goodizerColour(goodizerAmount));
    goodizerSlider.setVisible(!isMixHubMode());

    if (titlePopupVisible)
        titlePopupAmount = juce::jmin(1.0f, titlePopupAmount + 0.14f);
    else
        titlePopupAmount = juce::jmax(0.0f, titlePopupAmount - 0.18f);

    if (handshakeActive)
    {
        handshakeProgress = juce::jmin(1.0f, handshakeProgress + 0.075f);
        if (handshakeProgress >= 1.0f)
            handshakeActive = false;
    }

    repaint();
}

void MixTeacherAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button == &resetButton)
    {
        audioProcessor.resetAnalysis();
        hasProblemSnapshot = false;
        problemSnapshotKind = mixteacher::IssueKind::none;
        snapshot = audioProcessor.getLatestAnalysisSnapshot();
        displayedSnapshot = snapshot;
        hubSnapshot = audioProcessor.getMixHubSnapshot();
        displayedHubSnapshot = hubSnapshot;
        pendingHubSignature = {};
        pendingHubStartMs = 0.0;
        smoothedSpectrum = snapshot.spectrum;
        repaint();
        return;
    }

    if (button == &copyJsonButton)
        juce::SystemClipboard::copyTextToClipboard(isMixHubMode() ? hubSnapshot.toJson() : snapshot.toJson());
}

bool MixTeacherAudioProcessorEditor::isFrozen() const
{
    if (auto* value = audioProcessor.getParameters().getRawParameterValue("freeze"))
        return value->load() > 0.5f;

    return false;
}

bool MixTeacherAudioProcessorEditor::isRussian() const
{
    if (auto* value = audioProcessor.getParameters().getRawParameterValue("language"))
        return value->load() < 0.5f;

    return true;
}

bool MixTeacherAudioProcessorEditor::isMixHubMode() const
{
    return audioProcessor.isMixHubMode();
}

float MixTeacherAudioProcessorEditor::getGoodizerAmount() const
{
    if (auto* value = audioProcessor.getParameters().getRawParameterValue("goodizer"))
        return value->load();

    return 0.0f;
}

juce::String MixTeacherAudioProcessorEditor::getSpectrumFftLabel() const
{
    if (const auto* value = audioProcessor.getParameters().getRawParameterValue("spectrumFftSize"))
        return juce::String(1024 << juce::jlimit(0, 3, static_cast<int>(std::round(value->load()))));

    return "4096";
}

juce::String MixTeacherAudioProcessorEditor::tr(const char* ruUtf8, const char* en) const
{
    return isRussian() ? juce::String::fromUTF8(ruUtf8) : juce::String(en);
}

void MixTeacherAudioProcessorEditor::updateProblemSnapshot()
{
    if (snapshot.issues.empty())
        return;

    const auto& issue = snapshot.issues.front();
    const auto shouldCapture = issue.kind != mixteacher::IssueKind::none && issue.severity != mixteacher::Severity::ok;

    if (!shouldCapture)
        return;

    if (!hasProblemSnapshot || problemSnapshotKind != issue.kind || problemSnapshot.language != snapshot.language)
    {
        problemSnapshot = snapshot;
        problemSnapshotKind = issue.kind;
        hasProblemSnapshot = true;
    }
}

void MixTeacherAudioProcessorEditor::updateDisplayedSnapshot()
{
    const auto currentKind = snapshot.issues.empty() ? mixteacher::IssueKind::none : snapshot.issues.front().kind;
    const auto displayedKind = displayedSnapshot.issues.empty() ? mixteacher::IssueKind::none : displayedSnapshot.issues.front().kind;
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();

    if (currentKind == displayedKind)
    {
        displayedSnapshot = snapshot;
        pendingDisplayKind = mixteacher::IssueKind::none;
        pendingDisplayStartMs = 0.0;
        return;
    }

    if (currentKind != pendingDisplayKind)
    {
        pendingDisplayKind = currentKind;
        pendingDisplayStartMs = nowMs;
        return;
    }

    if (nowMs - pendingDisplayStartMs >= 5000.0)
    {
        displayedSnapshot = snapshot;
        pendingDisplayKind = mixteacher::IssueKind::none;
        pendingDisplayStartMs = 0.0;
    }
}

void MixTeacherAudioProcessorEditor::updateDisplayedHubSnapshot()
{
    auto makeSignature = [](const mixteacher::MixHubSnapshot& hub)
    {
        juce::String signature = juce::String(hub.trackCount) + "|";
        const auto count = juce::jmin(6, static_cast<int>(hub.issues.size()));

        for (int i = 0; i < count; ++i)
            signature += hub.issues[static_cast<size_t>(i)].title + ";";

        return signature;
    };

    auto hasActiveTracks = [](const mixteacher::MixHubSnapshot& hub)
    {
        for (const auto& track : hub.tracks)
            if (track.validForAnalysis && track.rmsDbfs > -60.0f)
                return true;

        return false;
    };

    const auto nowMs = juce::Time::getMillisecondCounterHiRes();

    if (displayedHubSnapshot.tracks.empty())
    {
        displayedHubSnapshot = hubSnapshot;
        pendingHubSignature = {};
        pendingHubStartMs = 0.0;
        if (hasActiveTracks(hubSnapshot))
            lastHubActiveMs = nowMs;
        return;
    }

    if (hasActiveTracks(hubSnapshot))
        lastHubActiveMs = nowMs;

    const auto playbackRecentlyActive = nowMs - lastHubActiveMs < 5000.0;
    if (!playbackRecentlyActive && hubSnapshot.trackCount > 0)
    {
        displayedHubSnapshot.trackCount = hubSnapshot.trackCount;
        displayedHubSnapshot.tracks = hubSnapshot.tracks;
        displayedHubSnapshot.tonalLowDb = hubSnapshot.tonalLowDb;
        displayedHubSnapshot.tonalLowMidDb = hubSnapshot.tonalLowMidDb;
        displayedHubSnapshot.tonalMidDb = hubSnapshot.tonalMidDb;
        displayedHubSnapshot.tonalHighDb = hubSnapshot.tonalHighDb;
        displayedHubSnapshot.tonalTiltDb = hubSnapshot.tonalTiltDb;
        displayedHubSnapshot.lowEndTiming = hubSnapshot.lowEndTiming;
        return;
    }

    const auto currentSignature = makeSignature(hubSnapshot);
    const auto displayedSignature = makeSignature(displayedHubSnapshot);

    if (currentSignature == displayedSignature)
    {
        displayedHubSnapshot = hubSnapshot;
        pendingHubSignature = {};
        pendingHubStartMs = 0.0;
        return;
    }

    if (currentSignature != pendingHubSignature)
    {
        pendingHubSignature = currentSignature;
        pendingHubStartMs = nowMs;
        displayedHubSnapshot.trackCount = hubSnapshot.trackCount;
        displayedHubSnapshot.tracks = hubSnapshot.tracks;
        displayedHubSnapshot.tonalLowDb = hubSnapshot.tonalLowDb;
        displayedHubSnapshot.tonalLowMidDb = hubSnapshot.tonalLowMidDb;
        displayedHubSnapshot.tonalMidDb = hubSnapshot.tonalMidDb;
        displayedHubSnapshot.tonalHighDb = hubSnapshot.tonalHighDb;
        displayedHubSnapshot.tonalTiltDb = hubSnapshot.tonalTiltDb;
        displayedHubSnapshot.lowEndTiming = hubSnapshot.lowEndTiming;
        return;
    }

    if (nowMs - pendingHubStartMs >= 3000.0)
    {
        displayedHubSnapshot = hubSnapshot;
        pendingHubSignature = {};
        pendingHubStartMs = 0.0;
    }
}

void MixTeacherAudioProcessorEditor::updateSmoothedSpectrum()
{
    const auto smoothing = snapshot.spectrumFftSize >= 4096 ? 0.88f : 0.82f;

    for (int i = 0; i < mixteacher::spectrumBins; ++i)
    {
        const auto current = snapshot.spectrum[static_cast<size_t>(i)];
        auto& smoothed = smoothedSpectrum[static_cast<size_t>(i)];
        smoothed = smoothed * smoothing + current * (1.0f - smoothing);
    }
}

void MixTeacherAudioProcessorEditor::drawHeader(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(panel());
    g.fillRoundedRectangle(area.toFloat(), 8.0f);
    g.setColour(panelLine());
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 8.0f, 1.0f);

    auto titleArea = area.reduced(18, 12).removeFromLeft(250);
    if (titlePopupVisible)
    {
        g.setColour(cyan().withAlpha(0.12f + 0.10f * titlePopupAmount));
        g.fillRoundedRectangle(titleArea.expanded(6, 2).toFloat(), 7.0f);
        g.setColour(cyan().withAlpha(0.45f));
        g.drawRoundedRectangle(titleArea.expanded(6, 2).toFloat().reduced(0.5f), 7.0f, 1.0f);
    }

    g.setColour(textMain());
    g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    g.drawFittedText("MIX TEACHER AI", titleArea.removeFromTop(32), juce::Justification::centredLeft, 1);

    g.setColour(textMuted());
    g.setFont(13.0f);
    g.drawFittedText(tr("умный VST3 анализатор", "smart VST3 analyzer"), titleArea, juce::Justification::centredLeft, 1);

    g.setColour(textMuted());
    g.setFont(11.0f);
    auto labels = area.withTrimmedLeft(290).withTrimmedRight(18).withTrimmedTop(10);
    auto x = labels.getX();
    g.drawFittedText("ROLE", juce::Rectangle<int>(x, labels.getY(), 100, 14), juce::Justification::centredLeft, 1);
    x += 108;
    g.drawFittedText(tr("ИСТОЧНИК", "SOURCE"), juce::Rectangle<int>(x, labels.getY(), 150, 14), juce::Justification::centredLeft, 1);
    x += 158;
    g.drawFittedText(tr("РЕЖИМ", "MODE"), juce::Rectangle<int>(x, labels.getY(), 116, 14), juce::Justification::centredLeft, 1);
    x += 124;
    g.drawFittedText(tr("ЯЗЫК", "LANG"), juce::Rectangle<int>(x, labels.getY(), 112, 14), juce::Justification::centredLeft, 1);
    x += 122;
    g.drawFittedText("FFT", juce::Rectangle<int>(x, labels.getY(), 76, 14), juce::Justification::centredLeft, 1);

    const auto severity = displayedSnapshot.issues.empty() ? mixteacher::Severity::ok : displayedSnapshot.issues.front().severity;
    auto statusArea = area.reduced(16, 12).removeFromRight(34);
    g.setColour(mixteacher::severityColour(severity));
    g.fillEllipse(statusArea.removeFromTop(34).withSizeKeepingCentre(24, 24).toFloat());
}

void MixTeacherAudioProcessorEditor::drawTitlePopup(juce::Graphics& g)
{
    if (titlePopupAmount <= 0.001f)
        return;

    const auto ease = 1.0f - std::pow(1.0f - titlePopupAmount, 3.0f);
    auto area = titlePopupArea.translated(0, static_cast<int>((1.0f - ease) * -12.0f));
    const auto alpha = juce::jlimit(0.0f, 1.0f, ease);

    auto bubble = area.toFloat();
    g.setColour(juce::Colour(0xee0f141c).withAlpha(0.92f * alpha));
    g.fillRoundedRectangle(bubble, 10.0f);
    g.setColour(cyan().withAlpha(0.34f * alpha));
    g.drawRoundedRectangle(bubble.reduced(0.5f), 10.0f, 1.2f);

    auto pointer = juce::Path();
    pointer.startNewSubPath(static_cast<float>(titleClickArea.getX() + 38), static_cast<float>(area.getY()));
    pointer.lineTo(static_cast<float>(titleClickArea.getX() + 52), static_cast<float>(area.getY()));
    pointer.lineTo(static_cast<float>(titleClickArea.getX() + 45), static_cast<float>(area.getY() - 9));
    pointer.closeSubPath();
    g.setColour(juce::Colour(0xee0f141c).withAlpha(0.92f * alpha));
    g.fillPath(pointer);
    g.setColour(cyan().withAlpha(0.28f * alpha));
    g.strokePath(pointer, juce::PathStrokeType(1.0f));

    auto content = area.reduced(16, 12);
    auto handshakeArea = content.removeFromLeft(86).reduced(0, 10);
    drawHandshake(g, handshakeArea, handshakeProgress);

    content.removeFromLeft(10);
    g.setColour(textMain().withAlpha(alpha));
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawFittedText(tr("Спасибо, что тестируешь!", "Thanks for testing!"),
                     content.removeFromTop(22),
                     juce::Justification::centredLeft,
                     1);

    g.setColour(textMuted().withAlpha(alpha));
    g.setFont(12.0f);
    g.drawFittedText(tr("Автор: Артур Валиев. Клик по заголовку открывает это маленькое секретное окно.",
                        "Author: Artur Valiev. Click the title to open this tiny secret panel."),
                     content.removeFromTop(40),
                     juce::Justification::centredLeft,
                     2);

    g.drawFittedText(tr("Mix Teacher слушает микс, ищет пересечения и подсказывает безопасный первый шаг.",
                        "Mix Teacher listens, finds overlaps, and suggests a safe first step."),
                     content.removeFromTop(38),
                     juce::Justification::centredLeft,
                     2);

    thanksButtonArea = content.removeFromBottom(30).removeFromRight(104);
    const auto buttonColour = handshakeProgress > 0.0f ? green() : cyan();
    g.setColour(buttonColour.withAlpha((0.18f + 0.18f * std::sin(goodizerPhase * juce::MathConstants<float>::twoPi)) * alpha));
    g.fillRoundedRectangle(thanksButtonArea.toFloat(), 7.0f);
    g.setColour(buttonColour.withAlpha(0.85f * alpha));
    g.drawRoundedRectangle(thanksButtonArea.toFloat().reduced(0.5f), 7.0f, 1.2f);
    g.setColour(textMain().withAlpha(alpha));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawFittedText(handshakeProgress > 0.0f ? tr("Принято", "Received") : tr("Спасибо", "Thanks"),
                     thanksButtonArea,
                     juce::Justification::centred,
                     1);
}

void MixTeacherAudioProcessorEditor::drawHandshake(juce::Graphics& g, juce::Rectangle<int> area, float progress)
{
    const auto activeProgress = progress > 0.0f ? progress : 0.18f + 0.08f * std::sin(goodizerPhase * juce::MathConstants<float>::twoPi);
    const auto eased = 1.0f - std::pow(1.0f - juce::jlimit(0.0f, 1.0f, activeProgress), 2.0f);
    const auto centre = area.getCentre().toFloat();
    const auto leftX = centre.x - 34.0f + (1.0f - eased) * -24.0f;
    const auto rightX = centre.x + 2.0f + (1.0f - eased) * 24.0f;
    const auto y = centre.y - 8.0f + std::sin(activeProgress * juce::MathConstants<float>::twoPi * 2.0f) * 3.0f;

    g.setColour(cyan().withAlpha(0.14f + 0.22f * eased));
    g.fillEllipse(area.withSizeKeepingCentre(74, 74).toFloat());

    g.setColour(juce::Colour(0xffffc857));
    g.fillRoundedRectangle(juce::Rectangle<float>(leftX, y, 42.0f, 18.0f), 7.0f);
    g.setColour(juce::Colour(0xffff8a65));
    g.fillRoundedRectangle(juce::Rectangle<float>(rightX, y + 2.0f, 42.0f, 18.0f), 7.0f);

    g.setColour(juce::Colour(0xfff6f1df));
    g.fillRoundedRectangle(juce::Rectangle<float>(centre.x - 11.0f, y + 3.0f, 22.0f, 18.0f), 6.0f);

    g.setColour(juce::Colour(0xff151a22).withAlpha(0.55f));
    g.drawLine(leftX + 28.0f, y + 5.0f, centre.x - 6.0f, y + 15.0f, 1.5f);
    g.drawLine(rightX + 14.0f, y + 7.0f, centre.x + 7.0f, y + 17.0f, 1.5f);

    if (progress > 0.0f)
    {
        for (int i = 0; i < 4; ++i)
        {
            const auto angle = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 4.0f + progress * 2.1f;
            const auto radius = 22.0f + progress * 18.0f;
            const auto spark = centre + juce::Point<float>(std::cos(angle) * radius, std::sin(angle) * radius);
            g.setColour(green().withAlpha((1.0f - progress) * 0.9f));
            g.fillEllipse(spark.x - 2.0f, spark.y - 2.0f, 4.0f, 4.0f);
        }
    }
}

void MixTeacherAudioProcessorEditor::drawGoodizer(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto amount = getGoodizerAmount();
    const auto active = amount > 0.001f;
    const auto colour = active ? goodizerColour(amount) : juce::Colour(0xff4a5564);
    const auto centre = area.getCentre().toFloat();

    g.setColour(juce::Colour(0xee171b22));
    g.fillRoundedRectangle(area.toFloat(), 12.0f);
    g.setColour(active ? colour.withAlpha(0.85f) : panelLine());
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 12.0f, active ? 2.0f : 1.0f);

    if (active)
    {
        for (int ring = 0; ring < 3; ++ring)
        {
            const auto pulse = std::fmod(goodizerPhase + ring * 0.31f, 1.0f);
            const auto radius = 42.0f + pulse * (28.0f + amount * 16.0f);
            const auto alpha = (1.0f - pulse) * (0.25f + amount * 0.45f);
            g.setColour(colour.withAlpha(alpha));
            g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.2f + amount * 2.5f);
        }
    }

    const auto labelArea = area.reduced(10).removeFromTop(30);
    g.setColour(active ? colour : textMuted());
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawFittedText(active ? "GOODIZER" : "GOODIZER OFF", labelArea, juce::Justification::centred, 1);

    auto valueArea = area.reduced(10).removeFromBottom(28);
    g.setColour(active ? textMain() : textMuted());
    g.setFont(13.0f);
    g.drawFittedText(juce::String(amount * 100.0f, 0) + "%   "
                         + (active ? tr("ВКЛ", "ON") : tr("ЧИСТО", "CLEAN")),
                     valueArea,
                     juce::Justification::centred,
                     1);
}

void MixTeacherAudioProcessorEditor::drawMixHub(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& hub = displayedHubSnapshot;
    auto summary = area.removeFromTop(150);
    drawSection(g, summary, "MIX HUB");
    auto summaryContent = summary.reduced(16, 32);

    const auto mainIssue = hub.issues.empty() ? mixteacher::MixHubIssue {} : hub.issues.front();
    const auto colour = mixteacher::severityColour(mainIssue.severity);

    g.setColour(colour);
    g.fillEllipse(summaryContent.removeFromLeft(34).withSizeKeepingCentre(24, 24).toFloat());
    summaryContent.removeFromLeft(12);

    g.setColour(textMain());
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawFittedText(mainIssue.title.isNotEmpty() ? mainIssue.title : tr("Mix Hub слушает", "Mix Hub listening"),
                     summaryContent.removeFromTop(26),
                     juce::Justification::centredLeft,
                     1);

    g.setColour(textMuted());
    g.setFont(12.0f);
    const auto holdText = pendingHubSignature.isNotEmpty() ? tr("   HOLD: новая картина стабилизируется", "   HOLD: stabilizing new picture")
                                                           : tr("   LIVE/HOLD включён", "   LIVE/HOLD enabled");
    g.drawFittedText(tr("Активных дорожек: ", "Active tracks: ") + juce::String(hub.trackCount)
                         + holdText + "   " + mainIssue.detail,
                     summaryContent.removeFromTop(24),
                     juce::Justification::centredLeft,
                     1);

    g.drawFittedText("Low " + formatDb(hub.tonalLowDb)
                         + "   Low-mid " + formatDb(hub.tonalLowMidDb)
                         + "   Mid " + formatDb(hub.tonalMidDb)
                         + "   High " + formatDb(hub.tonalHighDb),
                     summaryContent.removeFromTop(20),
                     juce::Justification::centredLeft,
                     1);

    g.setColour(textMain());
    g.drawFittedText(mainIssue.action, summaryContent, juce::Justification::centredLeft, 2);

    area.removeFromTop(10);
    auto mapPanel = area.removeFromTop(190);
    drawMixHubConflictMap(g, mapPanel, hub);

    area.removeFromTop(10);
    auto middle = area.removeFromTop(210);
    auto tracksPanel = middle.removeFromLeft((middle.getWidth() - 10) / 2);
    middle.removeFromLeft(10);
    auto issuesPanel = middle;
    drawSection(g, tracksPanel, tr("ДОРОЖКИ В MIX HUB", "TRACKS IN MIX HUB"));
    drawSection(g, issuesPanel, tr("КОНФЛИКТЫ", "RELATION ISSUES"));
    auto tracks = tracksPanel.reduced(14, 34);

    auto drawMiniBar = [&g](juce::Rectangle<int> barArea, float db, juce::Colour colour)
    {
        const auto value = dbToNorm(db, -72.0f, -18.0f);
        g.setColour(juce::Colour(0xff252c36));
        g.fillRoundedRectangle(barArea.toFloat(), 3.0f);
        g.setColour(colour);
        g.fillRoundedRectangle(barArea.withWidth(static_cast<int>(barArea.getWidth() * value)).toFloat(), 3.0f);
    };

    if (hub.tracks.empty())
    {
        g.setColour(textMuted());
        g.setFont(13.0f);
        g.drawFittedText(tr("Поставь Mix Teacher на дорожки в режиме Track. Этот экземпляр оставь на master в Role = Mix Hub.",
                            "Insert Mix Teacher on tracks in Track mode. Keep this instance on the master with Role = Mix Hub."),
                         tracks,
                         juce::Justification::centred,
                         3);
    }
    else
    {
        g.setColour(textMuted());
        g.setFont(11.0f);
        auto header = tracks.removeFromTop(18);
        g.drawText("TYPE", header.removeFromLeft(120), juce::Justification::centredLeft);
        g.drawText("RMS", header.removeFromLeft(76), juce::Justification::centredRight);
        g.drawText("PEAK", header.removeFromLeft(76), juce::Justification::centredRight);
        g.drawText("LOW", header.removeFromLeft(150), juce::Justification::centred);
        g.drawText("MID", header.removeFromLeft(150), juce::Justification::centred);
        g.drawText("HIGH", header.removeFromLeft(150), juce::Justification::centred);

        const auto maxRows = juce::jmin(5, static_cast<int>(hub.tracks.size()));
        for (int i = 0; i < maxRows; ++i)
        {
            const auto& track = hub.tracks[static_cast<size_t>(i)];
            auto row = tracks.removeFromTop(30);
            g.setColour(i % 2 == 0 ? juce::Colour(0xff202631) : juce::Colour(0xff1b2028));
            g.fillRoundedRectangle(row.toFloat(), 5.0f);

            auto cell = row.reduced(8, 4);
            const auto low = juce::jmax(track.bandDb.lowFoundation, track.bandDb.lowBody);
            const auto mid = juce::jmax(track.bandDb.tone, track.bandDb.presence);
            const auto high = juce::jmax(track.bandDb.sibilance, track.bandDb.air);

            g.setColour(textMain());
            g.setFont(12.0f);
            g.drawFittedText(track.effectiveType, cell.removeFromLeft(120), juce::Justification::centredLeft, 1);
            g.setColour(textMuted());
            g.drawText(formatDb(track.rmsDbfs), cell.removeFromLeft(76), juce::Justification::centredRight);
            g.drawText(formatDb(track.peakDbfs), cell.removeFromLeft(76), juce::Justification::centredRight);
            drawMiniBar(cell.removeFromLeft(150).reduced(10, 6), low, juce::Colour(0xff35d0ff));
            drawMiniBar(cell.removeFromLeft(150).reduced(10, 6), mid, juce::Colour(0xff3ddc97));
            drawMiniBar(cell.removeFromLeft(150).reduced(10, 6), high, juce::Colour(0xffffc857));
        }
    }

    auto issues = issuesPanel.reduced(14, 34);
    g.setFont(12.0f);
    const auto issueCount = juce::jmin(4, static_cast<int>(hub.issues.size()));
    for (int i = 0; i < issueCount; ++i)
    {
        const auto& issue = hub.issues[static_cast<size_t>(i)];
        auto line = issues.removeFromTop(42);
        g.setColour(mixteacher::severityColour(issue.severity));
        g.fillEllipse(line.removeFromLeft(18).withSizeKeepingCentre(10, 10).toFloat());
        line.removeFromLeft(8);
        g.setColour(textMain());
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawFittedText(issue.title, line.removeFromTop(17), juce::Justification::centredLeft, 1);
        g.setColour(textMuted());
        g.setFont(11.0f);
        g.drawFittedText(issue.detail, line, juce::Justification::centredLeft, 1);
    }

    area.removeFromTop(10);
    auto firstPanel = area;
    drawSection(g, firstPanel, tr("ПЕРВЫЙ ШАГ", "FIRST STEP"));
    auto first = firstPanel.reduced(14, 34);
    g.setColour(textMain());
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawFittedText(hub.firstStepTitle, first.removeFromTop(24), juce::Justification::centredLeft, 1);
    g.setColour(textMuted());
    g.setFont(12.0f);
    g.drawFittedText(hub.firstStepAction, first.removeFromTop(58), juce::Justification::topLeft, 3);
    g.drawFittedText(tr("Ограничение MVP: обмен работает между экземплярами в одном процессе DAW.",
                        "MVP limitation: sharing works between instances in the same DAW process."),
                     first,
                     juce::Justification::bottomLeft,
                     2);
}

void MixTeacherAudioProcessorEditor::drawMixHubConflictMap(juce::Graphics& g,
                                                           juce::Rectangle<int> area,
                                                           const mixteacher::MixHubSnapshot& hub)
{
    drawSection(g, area, tr("КАРТА ЧАСТОТНЫХ КОНФЛИКТОВ", "FREQUENCY CONFLICT MAP"));

    auto content = area.reduced(14, 34);
    auto graph = content;
    graph.removeFromBottom(34);

    g.setColour(juce::Colour(0xff151a22));
    g.fillRoundedRectangle(graph.toFloat(), 7.0f);
    g.setColour(juce::Colour(0xff2a3340));
    g.drawRoundedRectangle(graph.toFloat().reduced(0.5f), 7.0f, 1.0f);

    if (hub.tracks.empty())
    {
        g.setColour(textMuted());
        g.setFont(13.0f);
        g.drawFittedText(tr("Здесь появятся связи между дорожками, когда Mix Hub увидит частотные пересечения.",
                            "Links between tracks will appear here when Mix Hub detects frequency overlaps."),
                         graph,
                         juce::Justification::centred,
                         2);
        return;
    }

    struct Node
    {
        int id = 0;
        juce::String label;
        juce::Point<float> point;
        juce::Colour colour;
    };

    std::vector<Node> nodes;
    const auto nodeCount = juce::jmin(6, static_cast<int>(hub.tracks.size()));
    const auto centre = graph.getCentre().toFloat();
    const auto radiusX = graph.getWidth() * 0.38f;
    const auto radiusY = graph.getHeight() * 0.32f;

    for (int i = 0; i < nodeCount; ++i)
    {
        const auto& track = hub.tracks[static_cast<size_t>(i)];
        const auto angle = juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(nodeCount)
                           - juce::MathConstants<float>::halfPi;
        const auto point = juce::Point<float>(centre.x + std::cos(angle) * radiusX,
                                              centre.y + std::sin(angle) * radiusY);

        const auto colour = isDrumSource(track.effectiveType) ? juce::Colour(0xffffb347)
                           : track.effectiveType == "vocal" ? juce::Colour(0xffff6fb3)
                           : track.effectiveType == "bass" ? juce::Colour(0xff35d0ff)
                                                             : juce::Colour(0xff3ddc97);
        nodes.push_back({ track.instanceId, track.effectiveType, point, colour });
    }

    auto findNode = [&nodes](int id) -> const Node*
    {
        for (const auto& node : nodes)
            if (node.id == id)
                return &node;

        return nullptr;
    };

    if (hub.frequencyConflicts.empty())
    {
        g.setColour(juce::Colour(0xff2a3340));
        g.drawEllipse(centre.x - radiusX * 0.55f, centre.y - radiusY * 0.55f, radiusX * 1.1f, radiusY * 1.1f, 1.0f);
        g.setColour(textMuted());
        g.setFont(12.0f);
        g.drawFittedText(tr("Потенциальных пересечений пока не видно", "No potential overlaps visible yet"),
                         graph.withSizeKeepingCentre(260, 24),
                         juce::Justification::centred,
                         1);
    }
    else
    {
        const auto maxLines = juce::jmin(7, static_cast<int>(hub.frequencyConflicts.size()));
        for (int i = 0; i < maxLines; ++i)
        {
            const auto& conflict = hub.frequencyConflicts[static_cast<size_t>(i)];
            const auto* a = findNode(conflict.trackAId);
            const auto* b = findNode(conflict.trackBId);

            if (a == nullptr || b == nullptr)
                continue;

            const auto colour = mixteacher::severityColour(conflict.severity);
            const auto thickness = 1.5f + conflict.strength * 5.0f;
            const auto mid = (a->point + b->point) * 0.5f;

            g.setColour(colour.withAlpha(0.23f + conflict.strength * 0.4f));
            g.drawLine({ a->point, b->point }, thickness + 4.0f);
            g.setColour(colour);
            g.drawLine({ a->point, b->point }, thickness);

            g.setColour(juce::Colour(0xee151a22));
            auto tag = juce::Rectangle<float>(mid.x - 62.0f, mid.y - 10.0f, 124.0f, 20.0f);
            g.fillRoundedRectangle(tag, 5.0f);
            g.setColour(colour);
            g.drawRoundedRectangle(tag.reduced(0.5f), 5.0f, 1.0f);
            g.setColour(textMain());
            g.setFont(10.0f);
            g.drawFittedText(conflict.band, tag.toNearestInt().reduced(4, 1), juce::Justification::centred, 1);
        }
    }

    for (const auto& node : nodes)
    {
        auto nodeArea = juce::Rectangle<float>(node.point.x - 40.0f, node.point.y - 16.0f, 80.0f, 32.0f);
        g.setColour(juce::Colour(0xff202631));
        g.fillRoundedRectangle(nodeArea, 8.0f);
        g.setColour(node.colour);
        g.drawRoundedRectangle(nodeArea.reduced(0.5f), 8.0f, 1.5f);
        g.setColour(textMain());
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawFittedText(node.label, nodeArea.toNearestInt().reduced(5, 2), juce::Justification::centred, 1);
    }

    if (hub.lowEndTiming.available)
    {
        auto timing = graph.removeFromBottom(26).reduced(12, 3);
        g.setColour(juce::Colour(0xdd111820));
        g.fillRoundedRectangle(timing.toFloat(), 5.0f);
        g.setColour(mixteacher::severityColour(hub.lowEndTiming.severity).withAlpha(0.55f));
        g.drawRoundedRectangle(timing.toFloat().reduced(0.5f), 5.0f, 1.0f);

        auto label = timing.removeFromLeft(126).reduced(6, 0);
        g.setColour(textMain());
        g.setFont(10.0f);
        g.drawFittedText("LOW TIME " + juce::String(hub.lowEndTiming.risk * 100.0f, 0) + "%",
                         label,
                         juce::Justification::centredLeft,
                         1);

        const auto cellCount = mixteacher::dynamicsBins;
        const auto cellW = juce::jmax(1.0f, timing.getWidth() / static_cast<float>(cellCount));
        const auto colour = mixteacher::severityColour(hub.lowEndTiming.severity);
        for (int i = 0; i < cellCount; ++i)
        {
            const auto value = hub.lowEndTiming.curve[static_cast<size_t>(i)];
            if (value <= 0.01f)
                continue;

            const auto x = timing.getX() + i * cellW;
            const auto h = timing.getHeight() * juce::jlimit(0.15f, 1.0f, value);
            g.setColour(colour.withAlpha(0.18f + value * 0.72f));
            g.fillRoundedRectangle(juce::Rectangle<float>(x, timing.getBottom() - h, juce::jmax(1.0f, cellW - 1.0f), h),
                                   2.0f);
        }
    }

    auto strip = content.removeFromBottom(26);
    const char* labels[] = { "40", "120", "250", "600", "1k", "3k", "7k", "12k" };
    const auto bandCount = 8;
    const auto gap = 5;
    const auto cellW = (strip.getWidth() - gap * (bandCount - 1)) / bandCount;
    for (int band = 0; band < bandCount; ++band)
    {
        auto cell = strip.removeFromLeft(cellW);
        float strength = 0.0f;
        mixteacher::Severity severity = mixteacher::Severity::ok;

        for (const auto& conflict : hub.frequencyConflicts)
        {
            if (conflict.bandIndex == band)
            {
                strength = juce::jmax(strength, conflict.strength);
                severity = conflict.severity;
            }
        }

        g.setColour(strength > 0.0f ? mixteacher::severityColour(severity).withAlpha(0.35f + strength * 0.5f)
                                    : juce::Colour(0xff252c36));
        g.fillRoundedRectangle(cell.toFloat(), 4.0f);
        g.setColour(strength > 0.0f ? textMain() : textMuted());
        g.setFont(10.0f);
        g.drawText(labels[band], cell, juce::Justification::centred);
        strip.removeFromLeft(gap);
    }
}

void MixTeacherAudioProcessorEditor::drawSection(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    g.setColour(panel());
    g.fillRoundedRectangle(area.toFloat(), 8.0f);
    g.setColour(panelLine());
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 8.0f, 1.0f);

    g.setColour(textMuted());
    g.setFont(13.0f);
    g.drawText(title, area.reduced(14, 10).removeFromTop(18), juce::Justification::centredLeft);
}

void MixTeacherAudioProcessorEditor::drawWaveform(juce::Graphics& g, juce::Rectangle<int> area)
{
    drawSection(g, area, "WAVEFORM");

    auto graph = area.reduced(14, 36).withTrimmedTop(4);
    const auto centreY = static_cast<float>(graph.getCentreY());
    const auto scaleY = graph.getHeight() * 0.45f;

    g.setColour(juce::Colour(0xff262e39));
    g.drawHorizontalLine(graph.getCentreY(), static_cast<float>(graph.getX()), static_cast<float>(graph.getRight()));

    juce::Path path;
    for (int i = 0; i < mixteacher::waveformBins; ++i)
    {
        const auto x = graph.getX() + graph.getWidth() * i / static_cast<float>(mixteacher::waveformBins - 1);
        const auto y = centreY - juce::jlimit(-1.0f, 1.0f, snapshot.waveform[static_cast<size_t>(i)]) * scaleY;

        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }

    g.setColour(cyan());
    g.strokePath(path, juce::PathStrokeType(2.0f));
}

void MixTeacherAudioProcessorEditor::drawSpectrum(juce::Graphics& g, juce::Rectangle<int> area)
{
    drawSection(g, area, tr("СПЕКТР", "SPECTRUM"));

    auto graph = area.reduced(14, 36).withTrimmedTop(4);
    auto visual = snapshot;
    visual.spectrum = smoothedSpectrum;
    drawSpectrumBars(g, graph, visual, false);
    g.setColour(textMuted());
    g.setFont(11.0f);
    g.drawText("80   250   800   2k   5k   10k   FFT " + getSpectrumFftLabel(), graph.removeFromBottom(16), juce::Justification::centred);
}

void MixTeacherAudioProcessorEditor::drawSpectrumBars(juce::Graphics& g,
                                                      juce::Rectangle<int> graph,
                                                      const mixteacher::AnalysisSnapshot& source,
                                                      bool compact)
{
    const auto barWidth = graph.getWidth() / static_cast<float>(mixteacher::spectrumBins);
    const auto highlight = source.issues.empty() ? mixteacher::IssueKind::none : source.issues.front().kind;

    auto isHighlighted = [highlight](int bin)
    {
        const auto normalised = bin / static_cast<float>(mixteacher::spectrumBins - 1);

        if (highlight == mixteacher::IssueKind::mud)
            return normalised >= 0.15f && normalised <= 0.32f;
        if (highlight == mixteacher::IssueKind::snareBoxiness)
            return normalised >= 0.30f && normalised <= 0.48f;
        if (highlight == mixteacher::IssueKind::drumBoom)
            return normalised >= 0.08f && normalised <= 0.34f;
        if (highlight == mixteacher::IssueKind::sibilance || highlight == mixteacher::IssueKind::harshness
            || highlight == mixteacher::IssueKind::hatHarshness)
            return normalised >= 0.63f && normalised <= 0.82f;
        if (highlight == mixteacher::IssueKind::peak || highlight == mixteacher::IssueKind::clipping
            || highlight == mixteacher::IssueKind::dense || highlight == mixteacher::IssueKind::dynamics
            || highlight == mixteacher::IssueKind::transient || highlight == mixteacher::IssueKind::tooQuiet)
            return true;

        return false;
    };

    for (int i = 0; i < mixteacher::spectrumBins; ++i)
    {
        const auto value = juce::jlimit(0.0f, 1.0f, source.spectrum[static_cast<size_t>(i)]);
        auto bar = juce::Rectangle<float>(graph.getX() + i * barWidth,
                                          graph.getBottom() - value * graph.getHeight(),
                                          juce::jmax(1.0f, barWidth - (compact ? 2.0f : 1.5f)),
                                          value * graph.getHeight());

        const auto hot = isHighlighted(i);
        g.setColour(hot ? mixteacher::severityColour(source.issues.empty() ? mixteacher::Severity::info : source.issues.front().severity)
                        : cyan().interpolatedWith(green(), value * 0.5f));
        g.fillRoundedRectangle(bar, compact ? 1.5f : 2.0f);
    }
}

void MixTeacherAudioProcessorEditor::drawDynamicsGraph(juce::Graphics& g, juce::Rectangle<int> area)
{
    drawSection(g, area, tr("ДИНАМИКА RMS", "RMS DYNAMICS"));

    auto graph = area.reduced(14, 34);
    g.setColour(juce::Colour(0xff252c36));
    g.fillRoundedRectangle(graph.toFloat(), 5.0f);

    juce::Path path;
    for (int i = 0; i < mixteacher::dynamicsBins; ++i)
    {
        const auto value = juce::jlimit(0.0f, 1.0f, snapshot.dynamics[static_cast<size_t>(i)]);
        const auto x = graph.getX() + graph.getWidth() * i / static_cast<float>(mixteacher::dynamicsBins - 1);
        const auto y = graph.getBottom() - value * graph.getHeight();

        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }

    const auto colour = snapshot.rmsRangeDb > 15.0f ? mixteacher::severityColour(mixteacher::Severity::problem)
                                                    : snapshot.rmsRangeDb > 9.0f ? mixteacher::severityColour(mixteacher::Severity::warning)
                                                                               : green();
    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(2.0f));

    g.setColour(textMuted());
    g.setFont(12.0f);
    g.drawText(tr("P90-P10 ", "P90-P10 ") + juce::String(static_cast<int>(std::round(snapshot.rmsRangeDb))) + " dB   "
                   + tr("Транзиенты ", "Transients ") + juce::String(snapshot.transientScore * 100.0f, 0) + "%"
                   + "   Onsets " + juce::String(snapshot.onsetCount),
               graph.reduced(8, 4),
               juce::Justification::topLeft);
}

void MixTeacherAudioProcessorEditor::drawBandGraph(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto drumMode = isDrumSource(snapshot.track.effectiveType);
    drawSection(g, area, drumMode ? tr("ЗОНЫ УДАРНЫХ", "DRUM ZONES") : tr("ЧАСТОТНЫЕ ЗОНЫ", "FREQUENCY ZONES"));

    struct BandView
    {
        const char* label;
        float value;
        float db;
        mixteacher::IssueKind kind;
    };

    const BandView generalBands[] = {
        { "20", snapshot.bands.subRumble, snapshot.bandDb.subRumble, mixteacher::IssueKind::none },
        { "60", snapshot.bands.lowFoundation, snapshot.bandDb.lowFoundation, mixteacher::IssueKind::weakLowEnd },
        { "120", snapshot.bands.lowBody, snapshot.bandDb.lowBody, mixteacher::IssueKind::weakLowEnd },
        { "250", snapshot.bands.mud, snapshot.bandDb.mud, mixteacher::IssueKind::mud },
        { "600", snapshot.bands.boxiness, snapshot.bandDb.boxiness, mixteacher::IssueKind::boxiness },
        { "2k", snapshot.bands.presence, snapshot.bandDb.presence, mixteacher::IssueKind::weakPresence },
        { "7k", snapshot.bands.sibilance, snapshot.bandDb.sibilance, mixteacher::IssueKind::sibilance },
        { "12k", snapshot.bands.air, snapshot.bandDb.air, mixteacher::IssueKind::harshness },
    };

    const BandView drumBands[] = {
        { "Kick", snapshot.drumProfile.kickWeight, juce::jmax(snapshot.bandDb.lowFoundation, snapshot.bandDb.lowBody), mixteacher::IssueKind::drumBoom },
        { "Boom", snapshot.bands.mud, snapshot.bandDb.mud, mixteacher::IssueKind::drumBoom },
        { "Snare", snapshot.drumProfile.snareBoxiness, snapshot.bandDb.boxiness, mixteacher::IssueKind::snareBoxiness },
        { "Atk", snapshot.drumProfile.kickClick, snapshot.bandDb.presence, mixteacher::IssueKind::weakPresence },
        { "Hats", snapshot.drumProfile.hatHarshness, snapshot.bandDb.sibilance, mixteacher::IssueKind::hatHarshness },
        { "Air", snapshot.bands.air, snapshot.bandDb.air, mixteacher::IssueKind::harshness },
        { "Punch", snapshot.drumProfile.transientDensity, snapshot.transientScore * 100.0f, mixteacher::IssueKind::transient },
        { "Flat", snapshot.drumProfile.busCompressionRisk, snapshot.crestFactorDb, mixteacher::IssueKind::dense },
    };

    const auto* bands = drumMode ? drumBands : generalBands;
    auto graph = area.reduced(14, 34);
    const auto count = drumMode ? static_cast<int>(std::size(drumBands)) : static_cast<int>(std::size(generalBands));
    const auto gap = 7;
    const auto barWidth = (graph.getWidth() - gap * (count - 1)) / count;
    const auto issueKind = snapshot.issues.empty() ? mixteacher::IssueKind::none : snapshot.issues.front().kind;

    for (int i = 0; i < count; ++i)
    {
        auto cell = graph.removeFromLeft(barWidth);
        const auto value = juce::jlimit(0.0f, 1.0f, bands[i].value);
        auto bar = cell.withTrimmedTop(static_cast<int>((1.0f - value) * (cell.getHeight() - 18))).withTrimmedBottom(18);

        const auto hot = bands[i].kind != mixteacher::IssueKind::none && bands[i].kind == issueKind;
        g.setColour(hot ? mixteacher::severityColour(snapshot.issues.front().severity) : cyan().interpolatedWith(green(), value * 0.45f));
        g.fillRoundedRectangle(bar.toFloat(), 4.0f);

        g.setColour(hot ? textMain() : textMuted());
        g.setFont(10.0f);
        g.drawText(juce::String(bands[i].label) + "\n" + juce::String(bands[i].db, 0), cell.removeFromBottom(18), juce::Justification::centred);

        graph.removeFromLeft(gap);
    }
}

void MixTeacherAudioProcessorEditor::drawProblemSnapshot(juce::Graphics& g, juce::Rectangle<int> area)
{
    drawSection(g, area, tr("СНИМОК ПРОБЛЕМЫ", "PROBLEM SNAPSHOT"));

    auto content = area.reduced(14, 32);
    auto graph = content.removeFromLeft(410);

    if (hasProblemSnapshot)
    {
        drawSpectrumBars(g, graph, problemSnapshot, true);

        const auto issue = problemSnapshot.issues.empty() ? mixteacher::TeacherIssue {} : problemSnapshot.issues.front();
        auto text = content.withTrimmedLeft(14);
        g.setColour(mixteacher::severityColour(issue.severity));
        g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        g.drawFittedText(issue.title, text.removeFromTop(22), juce::Justification::centredLeft, 1);
        g.setColour(textMuted());
        g.setFont(12.0f);
        g.drawFittedText(tr("Снимок спектра в момент главной проблемы. Яркие зоны показывают, куда смотреть.",
                            "Spectrum snapshot from the main issue moment. Bright zones show where to look."),
                         text,
                         juce::Justification::topLeft,
                         2);
    }
    else
    {
        g.setColour(textMuted());
        g.setFont(13.0f);
        g.drawFittedText(tr("Пока серьёзной проблемы нет. Когда появится важная подсказка, здесь останется снимок спектра.",
                            "No clear issue yet. When an important hint appears, this panel keeps a spectrum snapshot."),
                         content,
                         juce::Justification::centredLeft,
                         2);
    }
}

void MixTeacherAudioProcessorEditor::drawMeter(juce::Graphics& g,
                                               juce::Rectangle<int> area,
                                               const juce::String& label,
                                               float db,
                                               float normalised,
                                               juce::Colour colour)
{
    auto text = area.removeFromLeft(96);
    g.setColour(textMain());
    g.setFont(13.0f);
    g.drawText(label, text, juce::Justification::centredLeft);

    auto valueArea = area.removeFromRight(72);
    g.setColour(textMuted());
    g.drawText(formatDb(db), valueArea, juce::Justification::centredRight);

    auto meter = area.reduced(0, 6);
    g.setColour(juce::Colour(0xff252c36));
    g.fillRoundedRectangle(meter.toFloat(), 4.0f);
    g.setColour(colour);
    g.fillRoundedRectangle(meter.withWidth(static_cast<int>(meter.getWidth() * normalised)).toFloat(), 4.0f);
}

void MixTeacherAudioProcessorEditor::drawLevels(juce::Graphics& g, juce::Rectangle<int> area)
{
    drawSection(g, area, tr("УРОВНИ", "LEVELS"));

    auto content = area.reduced(14, 34);
    drawMeter(g, content.removeFromTop(22), "Peak", snapshot.truePeakDbfs, dbToNorm(snapshot.truePeakDbfs), mixteacher::severityColour(snapshot.truePeakDbfs > -3.0f ? mixteacher::Severity::problem : mixteacher::Severity::ok));
    content.removeFromTop(2);
    drawMeter(g, content.removeFromTop(22), "RMS", snapshot.rmsDbfs, dbToNorm(snapshot.rmsDbfs), cyan());
    content.removeFromTop(2);
    drawMeter(g, content.removeFromTop(22), "LUFS", snapshot.lufsShortTerm, dbToNorm(snapshot.lufsShortTerm), green());

    content.removeFromTop(4);
    g.setColour(textMuted());
    g.setFont(11.0f);
    auto statusText = "Crest " + juce::String(static_cast<int>(std::round(snapshot.crestFactorDb))) + " dB   "
                      + tr("Запас ", "Headroom ") + juce::String(static_cast<int>(std::round(snapshot.headroomDb))) + " dB   "
                      + tr("Тип ", "Type ") + snapshot.track.effectiveType + " "
                      + juce::String(snapshot.track.confidence * 100.0f, 0) + "%"
                      + "   FFT " + juce::String(snapshot.spectrumFftSize);

    statusText += "\nCorr " + juce::String(snapshot.stereoCorrelation, 2)
                  + "   Width " + juce::String(snapshot.stereoWidth * 100.0f, 0) + "%"
                  + "   Mono " + juce::String(snapshot.monoFoldDownLossDb, 1) + " dB";

    if (isDrumSource(snapshot.track.effectiveType))
    {
        statusText += "\nKick " + juce::String(snapshot.drumProfile.kickWeight * 100.0f, 0) + "%"
                      + "   Hats " + juce::String(snapshot.drumProfile.hatHarshness * 100.0f, 0) + "%"
                      + "   Punch " + juce::String(snapshot.drumProfile.transientDensity * 100.0f, 0) + "%";
    }

    g.drawFittedText(statusText, content, juce::Justification::centredLeft, 3);
}

void MixTeacherAudioProcessorEditor::drawTeacher(juce::Graphics& g, juce::Rectangle<int> area)
{
    drawSection(g, area, tr("УЧИТЕЛЬ ГОВОРИТ", "TEACHER SAYS"));

    const auto issue = displayedSnapshot.issues.empty() ? mixteacher::TeacherIssue {} : displayedSnapshot.issues.front();

    auto content = area.reduced(16, 34);
    const auto colour = mixteacher::severityColour(issue.severity);

    g.setColour(colour);
    g.fillEllipse(content.removeFromLeft(34).withSizeKeepingCentre(24, 24).toFloat());
    content.removeFromLeft(10);

    g.setColour(textMain());
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    g.drawFittedText(issue.title.isNotEmpty() ? issue.title : tr("Слушаю", "Listening"),
                     content.removeFromTop(22),
                     juce::Justification::centredLeft,
                     1);

    g.setColour(colour);
    g.setFont(12.0f);
    g.drawFittedText(mixteacher::confidenceLabel(issue.confidence, displayedSnapshot.language)
                         + " " + juce::String(issue.confidence * 100.0f, 0) + "%",
                     content.removeFromTop(18),
                     juce::Justification::centredLeft,
                     1);

    content.removeFromTop(2);
    g.setColour(textMuted());
    g.setFont(12.0f);
    const auto evidence = issue.evidence.empty() ? tr("Жду активный аудиосигнал.", "Waiting for active audio.") : issue.evidence.front();
    g.drawFittedText(evidence, content.removeFromTop(22), juce::Justification::topLeft, 1);

    g.setColour(textMuted());
    g.drawFittedText(issue.why, content.removeFromTop(38), juce::Justification::topLeft, 2);

    content.removeFromTop(4);
    g.setColour(textMain());
    g.drawFittedText(issue.action, content.removeFromTop(34), juce::Justification::topLeft, 2);

    g.setColour(textMuted());
    g.drawFittedText(issue.listenCheck, content, juce::Justification::topLeft, 2);
}
