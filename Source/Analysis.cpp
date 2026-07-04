#include "Analysis.h"

#include <algorithm>
#include <cmath>

namespace mixteacher
{
namespace
{
juce::String utf8(const char* text)
{
    return juce::String::fromUTF8(text);
}

juce::String localised(bool russian, const char* ru, const char* en)
{
    return russian ? utf8(ru) : juce::String(en);
}

juce::var stringArrayToVar(const std::vector<juce::String>& values)
{
    juce::Array<juce::var> array;
    for (const auto& value : values)
        array.add(value);

    return array;
}

juce::var issueToVar(const TeacherIssue& issue, const juce::String& language)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("kind", issueKindToString(issue.kind));
    object->setProperty("severity", severityToString(issue.severity));
    object->setProperty("confidence", issue.confidence);
    object->setProperty("confidence_label", confidenceLabel(issue.confidence, language));
    object->setProperty("title", issue.title);
    object->setProperty("evidence", stringArrayToVar(issue.evidence));
    object->setProperty("why", issue.why);
    object->setProperty("action", issue.action);
    object->setProperty("listen_check", issue.listenCheck);
    return object;
}

void addBandClass(juce::DynamicObject& object, const char* name, float value)
{
    object.setProperty(name, classifyBand(value));
}

void addBandDb(juce::DynamicObject& object, const char* name, float value)
{
    object.setProperty(name, value);
}

TeacherIssue makeIssue(IssueKind kind,
                       Severity severity,
                       float confidence,
                       int priority,
                       const juce::String& title,
                       std::vector<juce::String> evidence,
                       const juce::String& why,
                       const juce::String& action,
                       const juce::String& listenCheck)
{
    return { kind, severity, juce::jlimit(0.0f, 1.0f, confidence), priority, title, std::move(evidence), why, action, listenCheck };
}

juce::String dbEvidence(const char* label, float value)
{
    const auto rounded = static_cast<int>(std::round(value));
    const auto absValue = std::abs(rounded);
    auto digits = juce::String(absValue);

    while (digits.length() < 2)
        digits = "0" + digits;

    return juce::String(label) + ": " + juce::String(rounded < 0 ? "-" : "+") + digits + " dB";
}
} // namespace

juce::String severityToString(Severity severity)
{
    switch (severity)
    {
        case Severity::info: return "info";
        case Severity::ok: return "ok";
        case Severity::warning: return "warning";
        case Severity::problem: return "problem";
        case Severity::critical: return "critical";
    }

    return "info";
}

juce::String issueKindToString(IssueKind kind)
{
    switch (kind)
    {
        case IssueKind::none: return "none";
        case IssueKind::tooQuiet: return "too_quiet";
        case IssueKind::clipping: return "clipping";
        case IssueKind::peak: return "too_hot";
        case IssueKind::lowHeadroom: return "low_headroom";
        case IssueKind::mud: return "mud";
        case IssueKind::boxiness: return "boxiness";
        case IssueKind::harshness: return "harshness";
        case IssueKind::hatHarshness: return "hat_harshness";
        case IssueKind::snareBoxiness: return "snare_boxiness";
        case IssueKind::drumBoom: return "drum_boom";
        case IssueKind::sibilance: return "sibilance";
        case IssueKind::dense: return "too_dense";
        case IssueKind::dynamics: return "too_dynamic";
        case IssueKind::transient: return "too_spiky";
        case IssueKind::weakPresence: return "weak_presence";
        case IssueKind::weakLowEnd: return "weak_low_end";
    }

    return "none";
}

juce::String validityToString(ValidityState state)
{
    switch (state)
    {
        case ValidityState::tooQuiet: return "too_quiet_for_analysis";
        case ValidityState::quietButUsable: return "quiet_but_usable";
        case ValidityState::good: return "good";
    }

    return "too_quiet_for_analysis";
}

juce::String confidenceLabel(float confidence, const juce::String& language)
{
    const auto russian = language == "ru";

    if (confidence < 0.3f)
        return localised(russian, "Низкая уверенность", "Low confidence");
    if (confidence < 0.6f)
        return localised(russian, "Возможно", "Possible");
    if (confidence < 0.8f)
        return localised(russian, "Похоже", "Likely");
    if (confidence < 0.92f)
        return localised(russian, "Очень похоже", "Very likely");

    return localised(russian, "Почти точно", "Almost certain");
}

juce::Colour severityColour(Severity severity)
{
    switch (severity)
    {
        case Severity::ok: return juce::Colour(0xff3ddc97);
        case Severity::info: return juce::Colour(0xff4da3ff);
        case Severity::warning: return juce::Colour(0xffffd166);
        case Severity::problem: return juce::Colour(0xffff9f43);
        case Severity::critical: return juce::Colour(0xffff4d6d);
    }

    return juce::Colours::white;
}

juce::String classifyBand(float value)
{
    if (value > 0.70f)
        return "high";
    if (value > 0.38f)
        return "medium";
    return "low";
}

std::vector<TeacherIssue> buildTeacherIssues(const AnalysisSnapshot& snapshot)
{
    std::vector<TeacherIssue> issues;
    const auto russian = snapshot.language == "ru";
    const auto validForFrequency = snapshot.validity.state == ValidityState::good;
    const auto quietUsable = snapshot.validity.state == ValidityState::quietButUsable;
    const auto effectiveType = snapshot.track.effectiveType;

    if (snapshot.validity.state == ValidityState::tooQuiet)
    {
        issues.push_back(makeIssue(IssueKind::tooQuiet,
                                   Severity::warning,
                                   snapshot.validity.confidence,
                                   2,
                                   localised(russian, "Сигнал слишком тихий для анализа", "Signal is too quiet for analysis"),
                                   { dbEvidence("Peak", snapshot.peakDbfs), dbEvidence("RMS", snapshot.rmsDbfs) },
                                   localised(russian,
                                             "Частотные и динамические выводы сейчас ненадёжны: алгоритм видит почти тишину или хвост.",
                                             "Frequency and dynamics conclusions are unreliable right now: the analyzer mostly sees silence or tail."),
                                   localised(russian,
                                             "Проиграй более громкий участок или проверь, что плагин стоит на активной дорожке.",
                                             "Play a louder section or check that the plugin is inserted on an active track."),
                                   localised(russian,
                                             "Включи место, где инструмент реально играет, и посмотри, появились ли стабильные уровни.",
                                             "Play a section where the source is actually active and check whether the meters become stable.")));
        return issues;
    }

    if (snapshot.clippingDetected)
    {
        issues.push_back(makeIssue(IssueKind::clipping,
                                   Severity::critical,
                                   0.96f,
                                   1,
                                   localised(russian, "Есть клиппинг", "Clipping detected"),
                                   { juce::String("Clips: ") + juce::String(snapshot.clippingCount), dbEvidence("Peak", snapshot.peakDbfs) },
                                   localised(russian,
                                             "Сначала нужно убрать перегруз. Пока есть клиппинг, анализ частот становится менее полезным.",
                                             "Fix overload first. While clipping is present, frequency analysis is less useful."),
                                   localised(russian,
                                             "Убавь clip/input gain на 3-6 dB до insert-chain.",
                                             "Lower clip/input gain by 3-6 dB before the insert chain."),
                                   localised(russian,
                                             "После снижения уровня проверь, исчезли ли красные пики и щелчки.",
                                             "After lowering the level, check whether red peaks and clicks are gone.")));
    }
    else if (snapshot.peakDbfs > -1.0f)
    {
        issues.push_back(makeIssue(IssueKind::peak,
                                   Severity::critical,
                                   0.9f,
                                   3,
                                   localised(russian, "Пики почти у цифрового потолка", "Peaks are almost at digital ceiling"),
                                   { dbEvidence("Peak", snapshot.peakDbfs), dbEvidence("Headroom", snapshot.headroomDb) },
                                   localised(russian,
                                             "Запаса почти нет. EQ, компрессия или сатурация легко доведут сигнал до клиппинга.",
                                             "There is almost no headroom. EQ, compression, or saturation can easily push the signal into clipping."),
                                   localised(russian,
                                             "Убавь clip/input gain примерно на 3-5 dB.",
                                             "Lower clip/input gain by about 3-5 dB."),
                                   localised(russian,
                                             "Сравни до и после на той же громкости мониторинга: звук должен стать чище, а не просто тише.",
                                             "Compare before and after at the same monitoring volume: it should sound cleaner, not just quieter.")));
    }
    else if (snapshot.peakDbfs > -3.0f)
    {
        issues.push_back(makeIssue(IssueKind::lowHeadroom,
                                   Severity::problem,
                                   0.78f,
                                   3,
                                   localised(russian, "Мало headroom", "Low headroom"),
                                   { dbEvidence("Peak", snapshot.peakDbfs), dbEvidence("Headroom", snapshot.headroomDb) },
                                   localised(russian,
                                             "Уровень рабочий, но запас до 0 dBFS маленький.",
                                             "The level is usable, but the safety margin before 0 dBFS is small."),
                                   localised(russian,
                                             "Если дальше стоят плагины, убавь вход на 2-4 dB.",
                                             "If more plugins follow, trim the input down by 2-4 dB."),
                                   localised(russian,
                                             "Проверь самый громкий фрагмент, а не только текущий момент.",
                                             "Check the loudest phrase, not only the current moment.")));
    }

    if (quietUsable)
    {
        issues.push_back(makeIssue(IssueKind::tooQuiet,
                                   Severity::info,
                                   snapshot.validity.confidence,
                                   2,
                                   localised(russian, "Дорожка очень тихая", "Track is very quiet"),
                                   { dbEvidence("Peak", snapshot.peakDbfs), dbEvidence("RMS", snapshot.rmsDbfs) },
                                   localised(russian,
                                             "Клиппинга нет, но частотные советы могут быть неточными из-за низкого уровня.",
                                             "There is no clipping, but frequency suggestions may be inaccurate because the level is low."),
                                   localised(russian,
                                             "Сначала проверь уровень и проиграй более насыщенный участок.",
                                             "Check the level first and play a more active section."),
                                   localised(russian,
                                             "Если при громком фрагменте предупреждение пропадает, всё нормально.",
                                             "If this warning disappears on a louder passage, the track is fine.")));
    }

    const auto dynamicRangeThreshold = effectiveType == "vocal" || effectiveType == "guitar" || effectiveType == "piano"
                                           || effectiveType == "drums" || effectiveType == "drums_bus"
                                       ? 15.0f
                                       : 12.0f;

    if (snapshot.rmsRangeDb > dynamicRangeThreshold && snapshot.analysisDurationSec > 8.0)
    {
        issues.push_back(makeIssue(IssueKind::dynamics,
                                   Severity::problem,
                                   juce::jlimit(0.62f, 0.92f, snapshot.rmsRangeDb / 16.0f),
                                   5,
                                   localised(russian, "Громкость сильно скачет", "Loudness jumps around"),
                                   { dbEvidence("P10 RMS", snapshot.activeRmsP10),
                                     dbEvidence("P90 RMS", snapshot.activeRmsP90),
                                     dbEvidence("Range", snapshot.rmsRangeDb) },
                                   localised(russian,
                                             "Алгоритм считает только активные окна, поэтому паузы не должны ломать вывод. Разброс всё равно большой.",
                                             "The analyzer counts active windows only, so pauses should not skew this. The active range is still large."),
                                   localised(russian,
                                             "Сначала выровняй clip gain вручную, потом используй мягкую компрессию 2-6 dB GR.",
                                             "Level clip gain manually first, then use gentle compression with 2-6 dB of gain reduction."),
                                   localised(russian,
                                             "Послушай, не вылезают ли отдельные фразы из микса и не пропадают ли тихие.",
                                             "Listen for phrases jumping out of the mix or quiet ones disappearing.")));
    }
    else if (snapshot.crestFactorDb > 0.1f && snapshot.crestFactorDb < 7.0f && snapshot.rmsDbfs > -24.0f)
    {
        issues.push_back(makeIssue(IssueKind::dense,
                                   Severity::warning,
                                   0.66f,
                                   5,
                                   localised(russian, "Сигнал выглядит слишком плотным", "Signal looks too dense"),
                                   { dbEvidence("Crest factor", snapshot.crestFactorDb), dbEvidence("RMS", snapshot.rmsDbfs) },
                                   localised(russian,
                                             "Пики и средний уровень близко друг к другу. Источник может казаться пережатым.",
                                             "Peak and average level are close together. The source may feel over-compressed."),
                                   localised(russian,
                                             "Если звук стал меньше в миксе, ослабь компрессию или сатурацию.",
                                             "If the sound got smaller in the mix, back off compression or saturation."),
                                   localised(russian,
                                             "Сравни bypass компрессора: должен вернуться удар или дыхание, а не только громкость.",
                                             "Bypass the compressor: impact or movement should return, not only loudness.")));
    }

    if (snapshot.transientScore > 0.62f && snapshot.peakDbfs > -14.0f)
    {
        issues.push_back(makeIssue(IssueKind::transient,
                                   Severity::warning,
                                   juce::jlimit(0.58f, 0.88f, snapshot.transientScore),
                                   5,
                                   localised(russian, "Много резких всплесков", "Many sharp spikes"),
                                   { juce::String("Transient score: ") + juce::String(snapshot.transientScore * 100.0f, 0) + "%",
                                     juce::String("Onsets: ") + juce::String(snapshot.onsetCount) },
                                   localised(russian,
                                             "Частые резкие пики могут заставлять компрессор работать нервно и делать звук колким.",
                                             "Frequent sharp peaks can make compressors react nervously and make the sound poky."),
                                   localised(russian,
                                             "Попробуй ручное выравнивание самых резких пиков или быстрый компрессор очень аккуратно.",
                                             "Try manual editing of the sharpest peaks or a fast compressor very gently."),
                                   localised(russian,
                                             "Проверь на тихом мониторинге, не колет ли атака ухо.",
                                             "At lower monitoring volume, check whether the attack still pokes your ear.")));
    }

    if (validForFrequency)
    {
        const auto isVocal = effectiveType == "vocal";
        const auto isKick = effectiveType == "kick";
        const auto isBass = effectiveType == "bass";
        const auto isDrums = effectiveType == "drums_bus" || effectiveType == "drums";
        const auto isMaster = effectiveType == "master";

        if (isDrums && snapshot.drumProfile.hatHarshness > 0.66f && snapshot.bandDb.sibilance > snapshot.bandDb.tone + 3.0f)
        {
            issues.push_back(makeIssue(IssueKind::hatHarshness,
                                       Severity::problem,
                                       juce::jlimit(0.62f, 0.9f, snapshot.drumProfile.hatHarshness),
                                       6,
                                       localised(russian, "Хэты или тарелки могут резать ухо", "Hats or cymbals may be harsh"),
                                       { dbEvidence("5-9 kHz", snapshot.bandDb.sibilance),
                                         dbEvidence("800-2k", snapshot.bandDb.tone),
                                         juce::String("Hat harshness: ") + juce::String(snapshot.drumProfile.hatHarshness * 100.0f, 0) + "%" },
                                       localised(russian,
                                                 "Для барабанов активная зона 5-9 kHz часто означает резкие хэты, тарелки или атаку снейра.",
                                                 "For drums, active 5-9 kHz often means sharp hats, cymbals, or snare attack."),
                                       localised(russian,
                                                 "Если верх режет ухо, попробуй dynamic EQ или мягкий shelf-cut 1-3 dB около 6-9 kHz.",
                                                 "If the top hurts, try dynamic EQ or a gentle 1-3 dB shelf cut around 6-9 kHz."),
                                       localised(russian,
                                                 "Сделай мониторинг тише и проверь, не раздражают ли хэты после 20-30 секунд прослушивания.",
                                                 "Turn monitoring down and check whether hats still irritate after 20-30 seconds.")));
        }

        if (isDrums && snapshot.drumProfile.snareBoxiness > 0.62f && snapshot.bandDb.boxiness > snapshot.bandDb.presence + 2.0f)
        {
            issues.push_back(makeIssue(IssueKind::snareBoxiness,
                                       Severity::warning,
                                       juce::jlimit(0.56f, 0.84f, snapshot.drumProfile.snareBoxiness),
                                       6,
                                       localised(russian, "Снейр/барабаны могут звучать коробочно", "Snare/drums may sound boxy"),
                                       { dbEvidence("350-800 Hz", snapshot.bandDb.boxiness),
                                         dbEvidence("2-5 kHz", snapshot.bandDb.presence) },
                                       localised(russian,
                                                 "Зона 350-800 Hz на барабанах часто даёт картонность и уменьшает читаемость атаки.",
                                                 "The 350-800 Hz range often adds cardboard-like tone to drums and masks attack."),
                                       localised(russian,
                                                 "Попробуй аккуратно убрать 400-700 Hz на 1-3 dB, если снейр кажется коробочным.",
                                                 "Try a careful 1-3 dB cut around 400-700 Hz if the snare feels boxy."),
                                       localised(russian,
                                                 "Проверь, стал ли снейр читаться лучше, не потеряв тело.",
                                                 "Check whether the snare reads better without losing body.")));
        }

        if (isDrums && snapshot.drumProfile.kickWeight > 0.68f && snapshot.drumProfile.kickClick < 0.42f)
        {
            issues.push_back(makeIssue(IssueKind::drumBoom,
                                       Severity::warning,
                                       0.7f,
                                       6,
                                       localised(russian, "Барабаны мощные снизу, но атаки мало", "Drums have weight but little attack"),
                                       { dbEvidence("40-150 Hz", juce::jmax(snapshot.bandDb.lowFoundation, snapshot.bandDb.lowBody)),
                                         dbEvidence("2-5 kHz", snapshot.bandDb.presence) },
                                       localised(russian,
                                                 "Кик и томы могут занимать низ, но на маленьких колонках ритм будет хуже читаться.",
                                                 "Kick and toms may fill the low end, but the groove may read poorly on small speakers."),
                                       localised(russian,
                                                 "Не поднимай общую громкость. Проверь атаку 2-5 kHz или убери немного 150-350 Hz.",
                                                 "Do not raise overall level. Check 2-5 kHz attack or cut a little 150-350 Hz."),
                                       localised(russian,
                                                 "Послушай тихо: должен быть понятен ритм кика без лишнего гула.",
                                                 "Listen quietly: the kick rhythm should be clear without extra boom.")));
        }

        if (isDrums && snapshot.drumProfile.busCompressionRisk > 0.6f)
        {
            issues.push_back(makeIssue(IssueKind::dense,
                                       Severity::warning,
                                       snapshot.drumProfile.busCompressionRisk,
                                       5,
                                       localised(russian, "Drum bus может быть пережат", "Drum bus may be over-compressed"),
                                       { dbEvidence("Crest factor", snapshot.crestFactorDb),
                                         dbEvidence("RMS", snapshot.rmsDbfs) },
                                       localised(russian,
                                                 "Если crest factor низкий, барабаны теряют удар и становятся плоскими.",
                                                 "If crest factor is low, drums lose punch and become flat."),
                                       localised(russian,
                                                 "Ослабь bus compression или сделай меньше gain reduction. Начни с 1-2 dB меньше.",
                                                 "Back off bus compression or reduce gain reduction. Start with 1-2 dB less."),
                                       localised(russian,
                                                 "Проверь, вернулся ли удар кика и снейра без резкого роста громкости.",
                                                 "Check whether kick and snare punch returns without a big loudness jump.")));
        }

        if (isVocal && snapshot.bandDb.mud > snapshot.bandDb.presence + 2.5f && snapshot.bandDb.mud > snapshot.bandDb.tone + 2.0f)
        {
            issues.push_back(makeIssue(IssueKind::mud,
                                       Severity::problem,
                                       0.74f,
                                       6,
                                       localised(russian, "Похоже, есть муть в вокале", "Vocal may be muddy"),
                                       { dbEvidence("180-350 Hz", snapshot.bandDb.mud),
                                         dbEvidence("2-5 kHz", snapshot.bandDb.presence),
                                         localised(russian, "Зона 180-350 Hz выше соседних зон", "180-350 Hz is above neighbouring bands") },
                                       localised(russian,
                                                 "Нижняя середина часто делает голос тяжёлым и менее читаемым.",
                                                 "Low-mid buildup often makes a voice heavy and less readable."),
                                       localised(russian,
                                                 "Попробуй EQ cut 2-4 dB около 220-300 Hz, Q 1-2.",
                                                 "Try an EQ cut of 2-4 dB around 220-300 Hz, Q 1-2."),
                                       localised(russian,
                                                 "Проверь, стал ли текст понятнее, не потеряв тело голоса.",
                                                 "Check whether words become clearer without losing the voice body.")));
        }

        if (isVocal && snapshot.bandDb.sibilance > snapshot.bandDb.tone + 3.0f && snapshot.transientScore > 0.35f)
        {
            issues.push_back(makeIssue(IssueKind::sibilance,
                                       Severity::problem,
                                       0.76f,
                                       6,
                                       localised(russian, "Возможны сибилянты", "Possible sibilance"),
                                       { dbEvidence("5-9 kHz", snapshot.bandDb.sibilance),
                                         juce::String("Transient score: ") + juce::String(snapshot.transientScore * 100.0f, 0) + "%" },
                                       localised(russian,
                                                 "Верхняя зона активна и совпадает с резкими моментами. На вокале это часто слышится как 'с' и 'ш'.",
                                                 "The upper band is active and lines up with sharp moments. On vocals this often sounds like 's' and 'sh'."),
                                       localised(russian,
                                                 "Попробуй de-esser 6-8 kHz, reduction 2-4 dB. Не дави весь вокал.",
                                                 "Try a de-esser at 6-8 kHz, 2-4 dB reduction. Do not squash the whole vocal."),
                                       localised(russian,
                                                 "Послушай только слова с 'с' и 'ш': они должны стать мягче, но не шепелявыми.",
                                                 "Listen only to words with sibilants: they should get softer, not lisped.")));
        }

        if ((isDrums || isMaster) && snapshot.bandDb.sibilance > snapshot.bandDb.tone + 4.0f)
        {
            issues.push_back(makeIssue(IssueKind::harshness,
                                       Severity::warning,
                                       0.7f,
                                       6,
                                       localised(russian, "Верх может быть резким", "Top end may be harsh"),
                                       { dbEvidence("5-9 kHz", snapshot.bandDb.sibilance), dbEvidence("800-2k", snapshot.bandDb.tone) },
                                       localised(russian,
                                                 "Для барабанов или мастера активная зона 5-9 kHz часто означает резкие хэты, тарелки или атаку.",
                                                 "For drums or master, active 5-9 kHz often means sharp hats, cymbals, or attack."),
                                       localised(russian,
                                                 "Если верх режет ухо, попробуй dynamic EQ или мягкий shelf-cut 1-3 dB около 6-9 kHz.",
                                                 "If the top hurts, try dynamic EQ or a gentle 1-3 dB shelf cut around 6-9 kHz."),
                                       localised(russian,
                                                 "Сделай мониторинг тише и проверь, не раздражают ли хэты.",
                                                 "Turn monitoring down and check whether hats still feel irritating.")));
        }

        if (isKick && snapshot.bandDb.lowFoundation > snapshot.bandDb.presence + 7.0f)
        {
            issues.push_back(makeIssue(IssueKind::weakPresence,
                                       Severity::warning,
                                       0.68f,
                                       6,
                                       localised(russian, "Кик есть по низу, но может плохо читаться", "Kick has low end but may not read"),
                                       { dbEvidence("40-80 Hz", snapshot.bandDb.lowFoundation), dbEvidence("2-5 kHz", snapshot.bandDb.presence) },
                                       localised(russian,
                                                 "Много тела и мало атаки: на маленьких колонках кик может пропадать.",
                                                 "Lots of body and little attack: on small speakers the kick may disappear."),
                                       localised(russian,
                                                 "Не поднимай громкость. Попробуй добавить 2-4 kHz на 1-3 dB или убрать 250-400 Hz.",
                                                 "Do not just turn it up. Try adding 2-4 kHz by 1-3 dB or cutting 250-400 Hz."),
                                       localised(russian,
                                                 "Проверь на маленькой акустике или тихом мониторинге, читается ли ритм.",
                                                 "Check on small speakers or low monitoring volume whether the rhythm reads.")));
        }

        if (isBass && snapshot.bandDb.lowFoundation < snapshot.bandDb.tone - 5.0f)
        {
            issues.push_back(makeIssue(IssueKind::weakLowEnd,
                                       Severity::warning,
                                       0.62f,
                                       6,
                                       localised(russian, "Басу может не хватать основы", "Bass may lack foundation"),
                                       { dbEvidence("40-120 Hz", snapshot.bandDb.lowFoundation), dbEvidence("800-2k", snapshot.bandDb.tone) },
                                       localised(russian,
                                                 "Если середина заметно сильнее основы, бас может слышаться как атака без веса.",
                                                 "If mids are much stronger than the foundation, bass can sound like attack without weight."),
                                       localised(russian,
                                                 "Проверь источник и баланс 60-120 Hz. Не добавляй много: начни с 1-3 dB.",
                                                 "Check the source and 60-120 Hz balance. Do not add much: start with 1-3 dB."),
                                       localised(russian,
                                                 "Слушай вместе с киком: низ должен поддерживать грув, а не гудеть.",
                                                 "Listen with the kick: low end should support the groove, not boom.")));
        }
    }

    std::stable_sort(issues.begin(), issues.end(), [](const auto& a, const auto& b)
    {
        if (a.priority != b.priority)
            return a.priority < b.priority;
        return a.confidence > b.confidence;
    });

    if (issues.size() > 3)
        issues.resize(3);

    if (issues.empty())
    {
        issues.push_back(makeIssue(IssueKind::none,
                                   Severity::ok,
                                   snapshot.validity.confidence,
                                   99,
                                   localised(russian, "Выглядит спокойно", "Looks healthy"),
                                   { localised(russian, "Нет крупных предупреждений по уровню, динамике или частотам.", "No major level, dynamics, or frequency warning is active.") },
                                   localised(russian,
                                             "Это не гарантия идеального микса, но явной технической проблемы сейчас не видно.",
                                             "This is not a guarantee of a perfect mix, but no obvious technical issue is visible right now."),
                                   localised(russian,
                                             "Продолжай слушать в контексте микса.",
                                             "Keep listening in the context of the full mix."),
                                   localised(russian,
                                             "Сравни с остальными дорожками: не выбивается ли источник по ощущению.",
                                             "Compare with other tracks: check whether the source feels out of place.")));
    }

    return issues;
}

FirstStep buildFirstStep(const AnalysisSnapshot& snapshot)
{
    const auto russian = snapshot.language == "ru";

    if (!snapshot.issues.empty())
        return { snapshot.issues.front().title, snapshot.issues.front().action };

    return { localised(russian, "Продолжай слушать в миксе", "Keep listening in the mix"),
             localised(russian,
                       "Сейчас нет главной проблемы. Проверь баланс с соседними дорожками.",
                       "No main issue is active right now. Check balance against neighbouring tracks.") };
}

juce::String AnalysisSnapshot::toJson() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty("plugin", plugin);
    root->setProperty("version", version);
    root->setProperty("analysis_mode", analysisMode);
    root->setProperty("language", language);
    root->setProperty("sensitivity", sensitivity);
    root->setProperty("spectrum_fft_size", spectrumFftSize);
    root->setProperty("goodizer_amount", goodizerAmount);
    root->setProperty("analysis_duration_sec", analysisDurationSec);

    auto* trackObject = new juce::DynamicObject();
    trackObject->setProperty("manual_type", track.manualType);
    trackObject->setProperty("detected_type", track.detectedType);
    trackObject->setProperty("effective_type", track.effectiveType);
    trackObject->setProperty("type_confidence", track.confidence);
    trackObject->setProperty("alternatives", stringArrayToVar(track.alternatives));
    root->setProperty("track", trackObject);

    auto* validityObject = new juce::DynamicObject();
    validityObject->setProperty("state", validityToString(validity.state));
    validityObject->setProperty("is_valid_for_analysis", validity.isValidForAnalysis);
    validityObject->setProperty("confidence", validity.confidence);
    validityObject->setProperty("warnings", stringArrayToVar(validity.warnings));
    root->setProperty("validity", validityObject);

    auto* levels = new juce::DynamicObject();
    levels->setProperty("peak_dbfs", peakDbfs);
    levels->setProperty("true_peak_dbfs", truePeakDbfs);
    levels->setProperty("rms_dbfs", rmsDbfs);
    levels->setProperty("lufs_short_term", lufsShortTerm);
    levels->setProperty("crest_factor_db", crestFactorDb);
    levels->setProperty("headroom_db", headroomDb);
    levels->setProperty("clipping_count", clippingCount);
    levels->setProperty("clipping_detected", clippingDetected);
    root->setProperty("levels", levels);

    juce::Array<juce::var> dynamicsCurve;
    for (const auto value : dynamics)
        dynamicsCurve.add(value);

    auto* dynamicsObject = new juce::DynamicObject();
    dynamicsObject->setProperty("active_rms_p10", activeRmsP10);
    dynamicsObject->setProperty("active_rms_p50", activeRmsP50);
    dynamicsObject->setProperty("active_rms_p90", activeRmsP90);
    dynamicsObject->setProperty("active_rms_range_db", rmsRangeDb);
    dynamicsObject->setProperty("transient_score", transientScore);
    dynamicsObject->setProperty("onset_count", onsetCount);
    dynamicsObject->setProperty("peak_to_rms_db", crestFactorDb);
    dynamicsObject->setProperty("curve", dynamicsCurve);
    root->setProperty("dynamics", dynamicsObject);

    auto* bandDbObject = new juce::DynamicObject();
    addBandDb(*bandDbObject, "20_40_hz", bandDb.subRumble);
    addBandDb(*bandDbObject, "40_80_hz", bandDb.lowFoundation);
    addBandDb(*bandDbObject, "80_150_hz", bandDb.lowBody);
    addBandDb(*bandDbObject, "150_350_hz", bandDb.mud);
    addBandDb(*bandDbObject, "350_800_hz", bandDb.boxiness);
    addBandDb(*bandDbObject, "800_2khz", bandDb.tone);
    addBandDb(*bandDbObject, "2_5khz", bandDb.presence);
    addBandDb(*bandDbObject, "5_9khz", bandDb.sibilance);
    addBandDb(*bandDbObject, "9_16khz", bandDb.air);

    auto* spectrumObject = new juce::DynamicObject();
    spectrumObject->setProperty("band_energy_db", bandDbObject);
    spectrumObject->setProperty("dominant_bands", stringArrayToVar(dominantBands));
    spectrumObject->setProperty("weak_bands", stringArrayToVar(weakBands));
    root->setProperty("spectrum", spectrumObject);

    auto* drumObject = new juce::DynamicObject();
    drumObject->setProperty("kick_weight", drumProfile.kickWeight);
    drumObject->setProperty("kick_click", drumProfile.kickClick);
    drumObject->setProperty("snare_boxiness", drumProfile.snareBoxiness);
    drumObject->setProperty("hat_harshness", drumProfile.hatHarshness);
    drumObject->setProperty("transient_density", drumProfile.transientDensity);
    drumObject->setProperty("bus_compression_risk", drumProfile.busCompressionRisk);
    root->setProperty("drum_profile", drumObject);

    auto* spectralClasses = new juce::DynamicObject();
    addBandClass(*spectralClasses, "20_40_hz", bands.subRumble);
    addBandClass(*spectralClasses, "40_80_hz", bands.lowFoundation);
    addBandClass(*spectralClasses, "80_150_hz", bands.lowBody);
    addBandClass(*spectralClasses, "180_350_hz", bands.mud);
    addBandClass(*spectralClasses, "500_800_hz", bands.boxiness);
    addBandClass(*spectralClasses, "2_5khz", bands.presence);
    addBandClass(*spectralClasses, "5_9khz", bands.sibilance);
    addBandClass(*spectralClasses, "10_16khz", bands.air);
    root->setProperty("spectral_notes", spectralClasses);

    juce::Array<juce::var> issuesArray;
    juce::Array<juce::var> possibleIssues;
    juce::Array<juce::var> suggestedActions;

    for (const auto& issue : issues)
    {
        issuesArray.add(issueToVar(issue, language));
        possibleIssues.add(issueKindToString(issue.kind));
        suggestedActions.add(issue.action);
    }

    root->setProperty("issues", issuesArray);
    root->setProperty("teacher_issues", issuesArray);
    root->setProperty("possible_issues", possibleIssues);
    root->setProperty("suggested_actions", suggestedActions);

    auto* first = new juce::DynamicObject();
    first->setProperty("title", firstStep.title);
    first->setProperty("action", firstStep.action);
    root->setProperty("first_step", first);

    return juce::JSON::toString(juce::var(root), true);
}
} // namespace mixteacher
