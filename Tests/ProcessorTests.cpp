#include "PluginProcessor.h"
#include "PatternLibrary.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class TestPlayHead : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm(bpm);
        p.setPpqPosition(ppq);
        p.setTimeSignature(TimeSignature{4, 4});
        p.setIsPlaying(playing);
        p.setIsLooping(looping);
        p.setLoopPoints(LoopPoints{loopStart, loopEnd});
        return p;
    }
    double ppq = 0;
    bool playing = true;
    double bpm = 120.0;
    bool looping = false;
    double loopStart = 0.0, loopEnd = 4.0;
};

void restoreXml(BreakVarAudioProcessor& processor, const juce::XmlElement& xml)
{
    juce::MemoryBlock binary;
    juce::AudioProcessor::copyXmlToBinary(xml, binary);
    processor.setStateInformation(binary.getData(), static_cast<int>(binary.getSize()));
}

void checkStateVersions()
{
    BreakVarAudioProcessor source(false), restored(false);
    juce::String error;
    auto pattern = source.getPattern();
    pattern.outputBars = 8;
    pattern.smoothTenths = 10;
    require(source.updatePattern(pattern, error), "versioned state fixture");
    juce::MemoryBlock binary;
    source.getStateInformation(binary);
    auto xml = juce::AudioProcessor::getXmlFromBinary(binary.getData(), static_cast<int>(binary.getSize()));
    require(xml && xml->getStringAttribute("version") == "4" && xml->hasAttribute("orderLengths")
            && xml->hasAttribute("smoothTenths") && xml->hasAttribute("tightenPercent")
            && xml->hasAttribute("nudgeTenths") && xml->hasAttribute("nudgeManual"),
            "new state writes version 4 with sound controls and occurrence lengths");
    restoreXml(restored, *xml);
    require(restored.getPattern() == pattern, "version 3 session restores colour arrangement");

    xml->setAttribute("version", 1);
    xml->removeAttribute("name");
    xml->removeAttribute("userPreset");
    require(restored.loadFactoryPreset(0, error), "reset before legacy state");
    restoreXml(restored, *xml);
    require(restored.getPattern() == pattern && restored.getPresetName() == "Custom"
            && !restored.isCurrentUserPreset(), "version 1 session remains readable without optional identity fields");

    const auto legacyDirectory = juce::File::getCurrentWorkingDirectory().getChildFile("processor-test-artifacts")
        .getChildFile("legacy-library-" + juce::Uuid().toString());
    require(legacyDirectory.createDirectory().wasOk(), "legacy preset directory fixture");
    const auto legacyFile = legacyDirectory.getChildFile("legacy.shflr");
    xml->setAttribute("name", "Legacy arrangement");
    require(xml->writeTo(legacyFile), "legacy version 1 preset file fixture");
    BreakVarAudioProcessor legacyReader(false, legacyDirectory);
    require(legacyReader.loadPreset(legacyFile, error) && legacyReader.getPattern() == pattern,
            "legacy external preset stays readable");
    require(legacyReader.getUserPresetNames().contains("Legacy arrangement")
            && legacyReader.loadUserPreset("Legacy arrangement", error) && legacyReader.getPattern() == pattern
            && legacyReader.isCurrentUserPreset(), "existing version 1 managed library preset stays readable");

    for (const auto& version : juce::StringArray{"0", "5", "2junk", "1.5", ""})
    {
        xml->setAttribute("version", version);
        xml->setAttribute("name", "Must not load");
        xml->setAttribute("outputBars", 1);
        restoreXml(restored, *xml);
        require(restored.getPattern() == pattern && restored.getPresetName() == "Custom",
                "unknown or malformed state version rejected atomically");
    }
}

void checkRestPresetState()
{
    const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile("processor-test-artifacts")
        .getChildFile("rest-library-" + juce::Uuid().toString());
    BreakVarAudioProcessor source(false, directory), restored(false, directory);
    juce::String error;
    auto mixed = breakvar::Pattern::makeDefault();
    mixed.inputBars = 1;
    mixed.outputBars = 2;
    mixed.cells.fill(-1);
    std::fill_n(mixed.cells.begin(), 4, 0);
    std::fill_n(mixed.cells.begin() + 4, 4, 1);
    mixed.order.fill(-1);
    // Stable serialized tokens: colours 0..7; rests 8..12 mean 1, 2, 4, 8, 16 cells.
    const std::array<int, 7> mixedOrder{0, 8, 9, 10, 11, 12, 1};
    std::copy(mixedOrder.begin(), mixedOrder.end(), mixed.order.begin());
    mixed.orderCount = static_cast<int>(mixedOrder.size());
    require(source.updatePattern(mixed, error), "mixed colour and every rest duration accepted");
    require(source.saveUserPreset("Rest roll", false, error), "save mixed rest preset");
    require(restored.loadUserPreset("Rest roll", error) && restored.getPattern() == mixed
            && restored.isCurrentUserPreset(), "managed preset retains rest lengths and user identity");

    juce::MemoryBlock binary;
    source.getStateInformation(binary);
    BreakVarAudioProcessor otherProject(false, directory.getSiblingFile("unrelated-" + juce::Uuid().toString()));
    otherProject.setStateInformation(binary.getData(), static_cast<int>(binary.getSize()));
    require(otherProject.getPattern() == mixed && otherProject.getPresetName() == "Rest roll"
            && otherProject.isCurrentUserPreset(), "project session embeds mixed rests independently of preset library");

    auto onlyRests = mixed;
    onlyRests.smoothTenths = 10;
    onlyRests.cells.fill(-1);
    onlyRests.order.fill(-1);
    onlyRests.orderCount = 5;
    for (int i = 0; i < 5; ++i) onlyRests.order[static_cast<std::size_t>(i)] = 8 + i;
    require(source.updatePattern(onlyRests, error), "all-rest arrangement accepted without assigned input colours");
    require(source.undoPattern() && source.getPattern() == mixed, "undo restores mixed rest arrangement");
    require(source.redoPattern() && source.getPattern() == onlyRests, "redo restores all-rest arrangement");
    require(source.saveUserPreset("All rests", false, error), "save all-rest managed preset");
    require(restored.loadUserPreset("All rests", error) && restored.getPattern() == onlyRests,
            "managed all-rest preset roundtrip");
    source.getStateInformation(binary);
    otherProject.setStateInformation(binary.getData(), static_cast<int>(binary.getSize()));
    require(otherProject.getPattern() == onlyRests && otherProject.isCurrentUserPreset(),
            "project all-rest session roundtrip");
    const auto standaloneFile = directory.getChildFile("external-rests.shflr");
    require(source.savePreset(standaloneFile, error) && restored.loadPreset(standaloneFile, error)
            && restored.getPattern() == onlyRests, "external compatibility preset supports rest tokens");

    auto xml = juce::AudioProcessor::getXmlFromBinary(binary.getData(), static_cast<int>(binary.getSize()));
    require(xml != nullptr, "rest state XML fixture");
    auto legacyRests = *xml;
    legacyRests.setAttribute("version", 2);
    legacyRests.removeAttribute("orderLengths");
    BreakVarAudioProcessor legacyRestReader(false, directory);
    restoreXml(legacyRestReader, legacyRests);
    require(legacyRestReader.getPattern() == onlyRests && legacyRestReader.getPresetName() == "All rests",
            "version 2 rest states remain readable with zero occurrence overrides");
    xml->setAttribute("name", "Corrupt rest");
    xml->setAttribute("version", 1);
    restoreXml(otherProject, *xml);
    require(otherProject.getPattern() == onlyRests && otherProject.getPresetName() == "All rests",
            "legacy version 1 cannot silently reinterpret previously invalid order values as rests");
    const auto corruptPreset = directory.getChildFile("corrupt-rest.shflr");
    require(xml->writeTo(corruptPreset) && !restored.loadPreset(corruptPreset, error),
            "legacy preset file rejects rest tokens");
    require(!restored.getUserPresetNames().contains("Corrupt rest"), "invalid legacy rest is hidden from managed library");
    xml->setAttribute("version", 2);
    xml->setAttribute("order", "8,9,10,11,13");
    restoreXml(otherProject, *xml);
    require(otherProject.getPattern() == onlyRests && otherProject.getPresetName() == "All rests",
            "version 2 rejects unknown rest token atomically");
    xml->setAttribute("order", "8,9,10,11,-1");
    restoreXml(otherProject, *xml);
    require(otherProject.getPattern() == onlyRests && otherProject.getPresetName() == "All rests",
            "version 2 rejects empty active order token");
    xml->setAttribute("order", "8,9,10,11,12");
    const auto cells = xml->getStringAttribute("cells");
    xml->setAttribute("cells", "8" + cells.substring(cells.indexOfChar(',')));
    restoreXml(otherProject, *xml);
    require(otherProject.getPattern() == onlyRests && otherProject.getPresetName() == "All rests",
            "rests are never accepted as input slice colours");
}

void checkSoundStateAndPublication()
{
    const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile("processor-test-artifacts")
        .getChildFile("sound-" + juce::Uuid().toString());
    BreakVarAudioProcessor processor(false, directory), restored(false, directory);
    juce::String error;
    require(processor.loadFactoryPreset(1, error), "sound straight fixture");
    const auto original = processor.getPattern();
    auto edited = original;
    edited.smoothTenths = 37; edited.tightenPercent = 65;
    edited.nudgeTenths = {-125, 300, 0, -300, 17, 0, 0, 0};
    edited.nudgeManual[0] = edited.nudgeManual[1] = edited.nudgeManual[2] = true;
    require(processor.updatePattern(edited, error) && processor.undoPattern()
            && processor.getPattern() == original && processor.redoPattern()
            && processor.getPattern() == edited, "undo/redo retains every sound control including manual zero");
    require(processor.saveUserPreset("Sound settings", false, error)
            && restored.loadUserPreset("Sound settings", error) && restored.getPattern() == edited,
            "managed preset roundtrips all sound controls");
    juce::MemoryBlock binary; processor.getStateInformation(binary);
    auto xml = juce::AudioProcessor::getXmlFromBinary(binary.getData(), static_cast<int>(binary.getSize()));
    require(xml != nullptr, "sound state XML fixture");
    restoreXml(restored, *xml);
    require(restored.getPattern() == edited, "project state roundtrips signed fractional nudges and manual zero");
    for (int version : {1, 2, 3})
    {
        auto legacy = *xml; legacy.setAttribute("version", version);
        auto expected = original; expected.smoothTenths = 10;
        restoreXml(restored, legacy);
        require(restored.getPattern() == expected, "legacy states ignore unversioned sound fields and use one-ms/off defaults");
    }
    restoreXml(restored, *xml);
    for (const auto* attribute : {"smoothTenths", "tightenPercent", "nudgeTenths", "nudgeManual"})
    {
        auto missing = *xml; missing.removeAttribute(attribute);
        restoreXml(restored, missing);
        require(restored.getPattern() == edited, "version 4 requires every sound field atomically");
    }
    const std::pair<const char*, const char*> bad[] {
        {"smoothTenths", "101"}, {"smoothTenths", "-1"}, {"smoothTenths", "2junk"},
        {"tightenPercent", "101"}, {"tightenPercent", "nan"}, {"tightenPercent", "50.5"},
        {"nudgeTenths", "-301,0,0,0,0,0,0,0"}, {"nudgeTenths", "0,0"},
        {"nudgeTenths", "0,0,0,0,0,0,0,1x"}, {"nudgeTenths", "-999999,0,0,0,0,0,0,0"},
        {"nudgeManual", "0,0,0,0,0,0,0,2"}, {"nudgeManual", "0,0,0,0,0,0,0,-1"}
    };
    for (const auto& [attribute, value] : bad)
    {
        auto malformed = *xml; malformed.setAttribute(attribute, value);
        const bool undo = restored.canUndo(), redo = restored.canRedo();
        restoreXml(restored, malformed);
        require(restored.getPattern() == edited && restored.getPresetName() == "Sound settings"
                && restored.canUndo() == undo && restored.canRedo() == redo,
                "malformed sound state preserves complete model, identity and history");
    }

    require(processor.updatePattern(original, error) && processor.saveUserPreset("Audible", false, error),
            "audible settings preset");
    for (int variant = 0; variant < 4; ++variant)
    {
        auto p = original;
        if (variant == 0) p.smoothTenths = 50;
        if (variant == 1) p.tightenPercent = 50;
        if (variant == 2) { p.nudgeManual[0] = true; p.nudgeTenths[0] = 157; }
        if (variant == 3) p.nudgeManual[0] = true;
        require(processor.updatePattern(p, error) && processor.saveUserPreset("Variant " + juce::String(variant), false, error),
                "sound-only variant saved");
    }
    require(processor.loadUserPreset("Audible", error), "restore audible pattern");
    TestPlayHead clock; processor.setPlayHead(&clock);
    processor.setRateAndBufferSizeDetails(8000, 128); processor.prepareToPlay(8000, 128);
    juce::AudioBuffer<float> audio(2, 128); juce::MidiBuffer midi; int frame = 0;
    const auto run = [&](int blocks)
    {
        for (int i = 0; i < blocks; ++i)
        {
            clock.ppq = frame / 4000.0;
            for (int s = 0; s < 128; ++s)
            { audio.setSample(0, s, 0.2f); audio.setSample(1, s, -0.3f); }
            processor.processBlock(audio, midi); frame += 128;
        }
    };
    run(400);
    require(processor.hasOutputReady(), "sound publication baseline has export");
    for (int variant = 0; variant < 4; ++variant)
    {
        require(processor.loadUserPreset("Variant " + juce::String(variant), error)
                && processor.getPreview().presetPending && !processor.hasOutputReady(),
                "each sound-only preset difference queues and invalidates old WAV");
        require(processor.loadUserPreset("Audible", error) && !processor.getPreview().presetPending,
                "active-pattern equality cancels pending sound-only changes");
    }
    require(processor.loadUserPreset("Variant 2", error), "queue manual nudge");
    run(500);
    const auto preview = processor.getPreview();
    require(!preview.presetPending && preview.sourceTiming.status[0] == breakvar::TimingStatus::manual
            && std::abs(preview.sourceTiming.offsetsMs[0] - 15.7f) < 0.01f
            && preview.latencySamples >= 126 && processor.hasOutputReady(),
            "audio mailbox applies manual nudge, reports compensated latency and publishes actual marker");
    require(processor.loadUserPreset("Audible", error) && processor.getPreview().presetPending
            && processor.loadUserPreset("Variant 2", error) && !processor.getPreview().presetPending,
            "active mailbox remembers applied sound fields after preset transition");
    auto immediate = processor.getPattern(); immediate.smoothTenths = 91;
    require(processor.updatePattern(immediate, error) && !processor.hasOutputReady(),
            "manual sound edit immediately invalidates exported loop");
    require(processor.getPreview().sourceTiming.status[0] == breakvar::TimingStatus::off
            && processor.getPreview().sourceTiming.offsetsMs[0] == 0,
            "unapplied edit hides stale audio timing markers even before the next callback");
    processor.setPlayHead(nullptr);
}

breakvar::Pattern threeBeatOccurrences()
{
    auto pattern = breakvar::Pattern::makeDefault();
    pattern.inputBars = 1;
    pattern.outputBars = 2;
    pattern.cells.fill(-1);
    std::fill_n(pattern.cells.begin(), 12, 0);
    pattern.order.fill(-1);
    std::fill_n(pattern.order.begin(), 3, 0);
    pattern.orderCount = 3;
    return pattern;
}

void checkOccurrenceLengthState()
{
    const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile("processor-test-artifacts")
        .getChildFile("length-library-" + juce::Uuid().toString());
    BreakVarAudioProcessor source(false, directory), restored(false, directory);
    juce::String error;
    auto full = threeBeatOccurrences();
    full.smoothTenths = 10;
    auto trimmed = full;
    require(trimmed.setOrderLength(1, 8), "trim middle occurrence to two beats");
    require(source.updatePattern(full, error) && source.updatePattern(trimmed, error), "length-only edit accepted");
    require(source.undoPattern() && source.getPattern() == full, "undo restores Full occurrence length");
    require(source.redoPattern() && source.getPattern() == trimmed, "redo restores individual occurrence length");
    require(source.saveUserPreset("Three two three", false, error), "save user occurrence lengths");
    require(restored.loadUserPreset("Three two three", error) && restored.getPattern() == trimmed
            && restored.getPresetName() == "Three two three" && restored.isCurrentUserPreset(),
            "user preset restores occurrence lengths and identity");
    juce::MemoryBlock binary;
    source.getStateInformation(binary);
    auto xml = juce::AudioProcessor::getXmlFromBinary(binary.getData(), static_cast<int>(binary.getSize()));
    require(xml && xml->getStringAttribute("version") == "4"
            && xml->getStringAttribute("orderLengths") == "0,8,0", "version 4 serializes one length per active occurrence");
    BreakVarAudioProcessor project(false, directory.getSiblingFile("unrelated-length-library"));
    restoreXml(project, *xml);
    require(project.getPattern() == trimmed && project.getPresetName() == "Three two three"
            && project.isCurrentUserPreset(), "session embeds lengths independently of managed library");
    const auto external = directory.getChildFile("External lengths.shflr");
    require(source.savePreset(external, error) && restored.loadPreset(external, error)
            && restored.getPattern() == trimmed, "external preset retains per-occurrence lengths");

    for (int version : {1, 2})
    {
        auto legacy = *xml;
        legacy.setAttribute("version", version);
        legacy.removeAttribute("orderLengths");
        restoreXml(project, legacy);
        require(project.getPattern() == full, "legacy state defaults every occurrence to Full");
        // Legacy readers must not accidentally reinterpret a field unknown to that format.
        legacy.setAttribute("orderLengths", "0,8,0");
        restoreXml(project, legacy);
        require(project.getPattern() == full, "legacy version ignores unversioned length overrides");
    }

    restoreXml(project, *xml);
    auto shorter = trimmed;
    require(shorter.setOrderLength(1, 4) && project.updatePattern(shorter, error) && project.undoPattern(),
            "state rejection history fixture");
    const auto before = project.getPattern();
    const auto beforeName = project.getPresetName();
    const bool beforeUser = project.isCurrentUserPreset();
    const bool beforeUndo = project.canUndo(), beforeRedo = project.canRedo();
    const auto unchanged = [&]
    {
        require(project.getPattern() == before && project.getPresetName() == beforeName
                && project.isCurrentUserPreset() == beforeUser && project.canUndo() == beforeUndo
                && project.canRedo() == beforeRedo, "malformed version 3 preserves pattern, identity and history atomically");
    };
    for (const auto& value : juce::StringArray{"", "0,8", "0,8,0,0", "0,-1,0", "0,65,0", "0,8x,0",
                                             "0,8.0,0", "0,+8,0", "0, 8,0", "0,999999999999,0", "0,8,0,"})
    {
        auto malformed = *xml;
        malformed.setAttribute("name", "Must not load");
        malformed.setAttribute("orderLengths", value);
        restoreXml(project, malformed);
        unchanged();
        const auto malformedFile = directory.getChildFile("malformed-lengths.shflr");
        require(malformed.writeTo(malformedFile) && !project.loadPreset(malformedFile, error),
                "malformed version 3 preset file rejected");
        unchanged();
    }
    auto malformed = *xml;
    malformed.removeAttribute("orderLengths");
    restoreXml(project, malformed);
    unchanged();
    malformed = *xml;
    malformed.setAttribute("order", "0,10,0");
    restoreXml(project, malformed);
    unchanged();
    malformed.setAttribute("orderLengths", "0,0,0");
    restoreXml(project, malformed);
    require(project.getPattern().order[1] == 10 && project.getPattern().orderLengths[1] == 0,
            "version 3 rest retains its token length with a zero override");

    auto empty = *xml;
    empty.setAttribute("orderCount", 0);
    empty.setAttribute("order", "");
    empty.setAttribute("orderLengths", "");
    restoreXml(project, empty);
    require(project.getPattern().orderCount == 0, "empty version 3 order has an explicit empty lengths field");
    empty.removeAttribute("orderLengths");
    empty.setAttribute("name", "Missing empty lengths");
    const auto emptyName = project.getPresetName();
    restoreXml(project, empty);
    require(project.getPattern().orderCount == 0 && project.getPresetName() == emptyName,
            "version 3 requires lengths even for an empty order");
}

void checkLengthOnlyPublicationAndExport()
{
    const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile("processor-test-artifacts")
        .getChildFile("length-output-" + juce::Uuid().toString());
    BreakVarAudioProcessor processor(false, directory);
    juce::String error;
    const auto full = threeBeatOccurrences();
    auto trimmed = full;
    require(trimmed.setOrderLength(1, 8), "trimmed publication fixture");
    require(processor.updatePattern(trimmed, error) && processor.saveUserPreset("Three two three", false, error)
            && processor.updatePattern(full, error) && processor.saveUserPreset("Full occurrences", false, error),
            "presets differ only by the middle occurrence length");
    TestPlayHead clock;
    processor.setPlayHead(&clock);
    processor.setRateAndBufferSizeDetails(8000, 257);
    processor.prepareToPlay(8000, 257);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> audio(2, 257);
    // Independently enumerated source-beat identities: 3+3+cut2 versus 3+2+3.
    const std::array<int, 8> fullBeats{1, 2, 3, 1, 2, 3, 1, 2};
    const std::array<int, 8> trimmedBeats{1, 2, 3, 1, 2, 1, 2, 3};
    const auto run = [&](int first, int end, const std::array<int, 8>& expected)
    {
        while (first < end)
        {
            const int count = std::min(257, end - first);
            clock.ppq = first / 4000.0;
            for (int s = 0; s < count; ++s)
            {
                const float sample = ((first + s) % 16000 / 4000 + 1) * 0.1f;
                audio.setSample(0, s, sample);
                audio.setSample(1, s, -sample);
            }
            juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(), 2, count);
            processor.processBlock(block, midi);
            for (int s = 0; s < count; ++s)
            {
                const int phase = (first + s) % 32000;
                if (phase % 4000 < 100 || phase % 4000 > 3900) continue;
                const float sample = expected[static_cast<std::size_t>(phase / 4000)] * 0.1f;
                require(std::abs(block.getSample(0, s) - sample) < 0.00001f
                        && std::abs(block.getSample(1, s) + sample) < 0.00001f,
                        "processor publishes literal per-occurrence source beat mapping");
            }
            first += count;
        }
    };
    // A repeated source beginning needs pre-roll from before that source.
    // Warm one output cycle before asserting complete export availability.
    run(0, 32000, fullBeats);
    run(32000, 68000, fullBeats);
    require(processor.hasOutputReady(), "Full occurrence loop captured before length-only selection");
    require(processor.loadUserPreset("Three two three", error) && processor.getPreview().presetPending
            && processor.getPresetName() == "Three two three", "length-only preset selection updates name and reports queued");
    require(!processor.hasOutputReady(), "length-only pending preset immediately rejects stale export");
    juce::AudioBuffer<float> empty(2, 0);
    processor.processBlock(empty, midi);
    require(processor.getPreview().presetPending && !processor.hasOutputReady(), "zero-frame callback cannot activate length edit");
    require(processor.loadUserPreset("Full occurrences", error) && !processor.getPreview().presetPending,
            "reselecting audible Full lengths cancels pending status");
    require(processor.loadUserPreset("Three two three", error), "requeue middle length edit");
    run(68000, 72000, fullBeats);
    require(processor.getPreview().presetPending && std::abs(processor.getPreview().presetBeatsRemaining - 6.0) < 0.00001,
            "length-only queue counts down against currently playing loop");
    run(72000, 96000, fullBeats);
    run(96000, 128001, trimmedBeats);
    require(!processor.getPreview().presetPending, "length-only preset becomes active at output boundary");
    run(128001, 160001, trimmedBeats);
    const auto wav = directory.getChildFile("three-two-three.wav");
    require(processor.hasOutputReady() && processor.writeOutputWav(wav, error), "settled 3+2+3 output exports");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(wav.createInputStream().release(), true));
    juce::AudioBuffer<float> decoded(2, 32000);
    require(reader && reader->lengthInSamples == 32000 && reader->sampleRate == 8000
            && reader->read(&decoded, 0, 32000, 0, true, true), "decode exact eight-beat length-only output");
    for (int beat = 0; beat < 8; ++beat)
    {
        const float sample = trimmedBeats[static_cast<std::size_t>(beat)] * 0.1f;
        require(std::abs(decoded.getSample(0, beat * 4000 + 2000) - sample) < 0.00001f
                && std::abs(decoded.getSample(1, beat * 4000 + 2000) + sample) < 0.00001f,
                "export repeats source beginning for literal 3+2+3 occurrence lengths");
    }
    require(processor.loadUserPreset("Full occurrences", error) && processor.getPreview().presetPending,
            "active mailbox retains trimmed lengths when queuing Full");
    require(processor.loadUserPreset("Three two three", error) && !processor.getPreview().presetPending,
            "reselecting audible trimmed lengths cancels pending status");
    require(processor.updatePattern(full, error) && !processor.getPreview().presetPending && !processor.hasOutputReady(),
            "manual length-only edit applies immediately and invalidates export");
    run(160001, 192001, fullBeats);
    require(processor.undoPattern() && processor.getPattern() == trimmed, "length-only audio edit has one undo step");
    run(192001, 224001, trimmedBeats);
    require(processor.redoPattern() && processor.getPattern() == full, "length-only audio redo restores Full");
    run(224001, 256001, fullBeats);
    processor.releaseResources();
}

void checkQueuedRestExport()
{
    const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile("processor-test-artifacts")
        .getChildFile("rest-output-" + juce::Uuid().toString());
    BreakVarAudioProcessor processor(false, directory);
    juce::String error;
    auto mixed = breakvar::Pattern::makeDefault();
    mixed.inputBars = 1;
    mixed.outputBars = 1;
    mixed.cells.fill(-1);
    std::fill_n(mixed.cells.begin(), 4, 0);
    mixed.order.fill(-1);
    mixed.order[0] = 0;
    mixed.order[1] = 10; // One source beat, one silent beat; repeat to fill four beats.
    mixed.orderCount = 2;
    auto silence = mixed;
    silence.cells.fill(-1);
    silence.order.fill(-1);
    silence.order[0] = 12;
    silence.orderCount = 1;
    require(processor.updatePattern(silence, error) && processor.saveUserPreset("Silent bar", false, error),
            "queued rest preset fixture");
    require(processor.updatePattern(mixed, error), "alternating sound and silence export fixture");
    TestPlayHead clock;
    processor.setPlayHead(&clock);
    processor.setRateAndBufferSizeDetails(8000, 257);
    processor.prepareToPlay(8000, 257);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> audio(2, 257);
    const auto run = [&](int first, int end, bool expectAllSilent)
    {
        while (first < end)
        {
            const int count = std::min(257, end - first);
            clock.ppq = first / 4000.0;
            for (int s = 0; s < count; ++s)
            {
                audio.setSample(0, s, 0.2f);
                audio.setSample(1, s, -0.3f);
            }
            juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(), 2, count);
            processor.processBlock(block, midi);
            for (int s = 0; s < count; ++s)
            {
                const int phase = (first + s) % 16000;
                if (expectAllSilent)
                    require(block.getSample(0, s) == 0.0f && block.getSample(1, s) == 0.0f,
                            "activated all-rest preset emits exact silence despite live input");
                else if (phase % 4000 > 100 && phase % 4000 < 3900)
                {
                    const bool silentBeat = (phase / 4000) % 2 == 1;
                    require(std::abs(block.getSample(0, s) - (silentBeat ? 0.0f : 0.2f)) < 0.00001f
                            && std::abs(block.getSample(1, s) - (silentBeat ? 0.0f : -0.3f)) < 0.00001f,
                            "mixed rest arrangement preserves source beats and silent beats until queued boundary");
                }
            }
            first += count;
        }
    };
    run(0, 18000, false);
    const auto mixedWav = directory.getChildFile("mixed.wav");
    require(processor.hasOutputReady() && processor.writeOutputWav(mixedWav, error), "mixed rest loop exports");
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> mixedReader(format.createReaderFor(mixedWav.createInputStream().release(), true));
    juce::AudioBuffer<float> decoded(2, 16000);
    require(mixedReader && mixedReader->lengthInSamples == 16000
            && mixedReader->read(&decoded, 0, 16000, 0, true, true), "decode mixed rest export");
    for (int sample : {1000, 9000})
        require(std::abs(decoded.getSample(0, sample) - 0.2f) < 0.00001f
                && std::abs(decoded.getSample(1, sample) + 0.3f) < 0.00001f, "export preserves played source beats");
    for (int sample : {5000, 13000})
        require(decoded.getSample(0, sample) == 0.0f && decoded.getSample(1, sample) == 0.0f,
                "export preserves deliberate silent beats");

    require(processor.loadUserPreset("Silent bar", error) && processor.getPreview().presetPending,
            "managed rest preset queues while playing");
    require(!processor.hasOutputReady(), "queued rest selection cannot export previous arrangement");
    run(18000, 32000, false);
    run(32000, 48001, true);
    require(!processor.getPreview().presetPending, "rest preset activates at loop boundary");
    // The existing edit fade excludes the transition cycle from export. The next
    // full cycle is authoritative even when the entire arrangement is silent.
    run(48001, 64001, true);
    require(processor.hasOutputReady(), "rest preset captures complete settled loop");
    const auto silentWav = directory.getChildFile("silent.wav");
    require(processor.writeOutputWav(silentWav, error), "all-rest loop is exportable");
    std::unique_ptr<juce::AudioFormatReader> silentReader(format.createReaderFor(silentWav.createInputStream().release(), true));
    require(silentReader && silentReader->lengthInSamples == 16000
            && silentReader->read(&decoded, 0, 16000, 0, true, true), "decode all-rest export");
    require(decoded.getMagnitude(0, 16000) == 0.0f, "all-rest WAV contains exact silence for its full duration");
    processor.releaseResources();
}

void checkQuantisedPresetSwitch()
{
    BreakVarAudioProcessor switched(false), reference(false);
    juce::String error;
    require(switched.loadFactoryPreset(1, error) && reference.loadFactoryPreset(1, error),
            "straight switch fixture");
    TestPlayHead clock;
    for (auto* processor : {&switched, &reference})
    {
        processor->setPlayHead(&clock);
        processor->setRateAndBufferSizeDetails(8000, 257);
        processor->prepareToPlay(8000, 257);
    }
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> actual(2, 257), expected(2, 257);
    const auto run = [&](int first, int end, bool compare)
    {
        while (first < end)
        {
            const int count = std::min(257, end - first);
            clock.ppq = first / 4000.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < count; ++s)
                {
                    const float sample = static_cast<float>((first + s) % 16000) / 20000.0f;
                    actual.setSample(ch, s, sample); expected.setSample(ch, s, sample);
                }
            juce::AudioBuffer<float> a(actual.getArrayOfWritePointers(), 2, count);
            juce::AudioBuffer<float> b(expected.getArrayOfWritePointers(), 2, count);
            switched.processBlock(a, midi); reference.processBlock(b, midi);
            if (compare)
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < count; ++s)
                        require(std::abs(a.getSample(ch, s) - b.getSample(ch, s)) < 0.00001f,
                                "queued preset keeps current audio until output loop boundary");
            first += count;
        }
    };
    run(0, 34001, true);
    require(switched.loadFactoryPreset(6, error), "select skip and repeat while playing");
    require(switched.getPresetName() == "One bar - Skip and repeat", "selected name updates before activation");
    require(switched.getPreview().presetPending && switched.getPreview().presetBeatsRemaining < 0,
            "preset reports queued immediately before audio receives it");
    require(!switched.hasOutputReady(), "queued preset rejects stale WAV immediately");
    juce::AudioBuffer<float> empty(2, 0);
    switched.processBlock(empty, midi);
    require(switched.getPreview().presetPending && !switched.hasOutputReady(),
            "zero-frame callback cannot acknowledge queued audio or expose old WAV");
    require(switched.loadFactoryPreset(1, error) && !switched.getPreview().presetPending,
            "selecting currently audible arrangement immediately cancels queue");
    require(switched.loadFactoryPreset(2, error) && switched.loadFactoryPreset(6, error), "last queued selection wins");
    run(34001, 35001, true);
    require(switched.getPreview().presetPending && std::abs(switched.getPreview().presetBeatsRemaining - 3.24975) < 0.00001,
            "queued status counts remaining quarter-note beats to old loop end");
    run(35001, 48000, true);
    require(reference.loadFactoryPreset(6, error), "reference switches at boundary");
    run(48000, 56000, true);
    require(!switched.getPreview().presetPending && switched.getPreview().playing, "activated preset clears queued status");
    require(switched.loadFactoryPreset(2, error) && switched.getPreview().presetPending, "queued before manual edit");
    auto edit = switched.getPattern();
    edit.outputBars = 4;
    require(switched.updatePattern(edit, error) && !switched.getPreview().presetPending,
            "manual editing cancels preset queue immediately");
    run(56000, 56515, false);
    require(!switched.getPreview().presetPending, "manual edit applies without waiting for output loop");
    require(switched.loadFactoryPreset(6, error) && switched.getPreview().presetPending, "queued before host stop");
    clock.playing = false;
    run(56515, 56772, false);
    require(!switched.getPreview().presetPending && !switched.getPreview().playing, "stop applies pending selection and clears wait");
    require(switched.loadFactoryPreset(1, error) && !switched.getPreview().presetPending, "stopped selection is ready immediately");
    clock.playing = true;
    run(56772, 57029, false);
    require(!switched.getPreview().presetPending, "stopped selection does not queue on resume");
    require(switched.loadFactoryPreset(6, error), "queue before release");
    switched.releaseResources();
    require(!switched.getPreview().presetPending && !switched.getPreview().playing, "release without stopped callback clears wait");
    run(57029, 57286, false);
    require(!switched.getPreview().presetPending, "release applies selected preset on resumed callback");
    require(switched.loadFactoryPreset(1, error) && switched.getPreview().presetPending, "queue before suspended callbacks");
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    require(!switched.getPreview().presetPending && !switched.getPreview().playing, "suspended callbacks do not leave a permanent waiting label");
    run(57286, 57543, false);
    require(!switched.getPreview().presetPending, "resumed callback applies selection immediately after suspension");
}
}

int main()
{
    try
    {
        // Initialises JUCE infrastructure only; no editor, window or DAW is opened.
        juce::ScopedJuceInitialiser_GUI initialise;
        checkStateVersions();
        checkSoundStateAndPublication();
        checkRestPresetState();
        checkOccurrenceLengthState();
        checkLengthOnlyPublicationAndExport();
        checkQueuedRestExport();
        checkQuantisedPresetSwitch();
        BreakVarAudioProcessor original;
        juce::String error;
        auto pattern = original.getPattern();
        pattern.outputBars = 8;
        require(pattern.insert(1, 2), "insert fixture");
        require(original.updatePattern(pattern, error), "valid update");
        require(original.canUndo(), "update creates undo");
        require(original.undoPattern(), "undo works");
        require(original.getPattern() == breakvar::Pattern::makeDefault(), "undo restores initial pattern");
        require(original.redoPattern(), "redo works");
        require(original.getPattern() == pattern, "redo restores edited pattern");
        auto invalid = pattern;
        invalid.order[0] = 99;
        require(!original.updatePattern(invalid, error), "invalid update rejected");
        require(original.getPattern() == pattern, "invalid update atomic");

        juce::MemoryBlock saved;
        original.getStateInformation(saved);
        BreakVarAudioProcessor restored;
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        require(restored.getPattern() == pattern, "project state roundtrip");
        require(!restored.canUndo(), "restore starts fresh undo history");
        const char garbage[] = "not a plugin state";
        restored.setStateInformation(garbage, sizeof(garbage));
        require(restored.getPattern() == pattern, "malformed state ignored");
        juce::XmlElement corrupt("BreakVarState");
        corrupt.setAttribute("version", 1);
        corrupt.setAttribute("inputBars", 999);
        juce::MemoryBlock corruptBinary;
        juce::AudioProcessor::copyXmlToBinary(corrupt, corruptBinary);
        restored.setStateInformation(corruptBinary.getData(), static_cast<int>(corruptBinary.getSize()));
        require(restored.getPattern() == pattern, "invalid XML state ignored");

        TestPlayHead playhead;
        restored.setPlayHead(&playhead);
        restored.setRateAndBufferSizeDetails(48000, 257);
        restored.prepareToPlay(48000, 257);
        require(restored.getLatencySamples() == breakvar::requiredLatencySamples(pattern, 48000, 120), "initial pattern latency declared");
        juce::AudioBuffer<float> audio(2, 257);
        juce::MidiBuffer midi;
        // Publish edits concurrently with audio snapshot reads; every result must stay finite.
        std::jthread writer([&]
        {
            for (int i = 0; i < 500; ++i)
            {
                auto edit = pattern;
                edit.outputBars = (i & 1) ? 2 : 4;
                juce::String why;
                restored.updatePattern(edit, why);
            }
        });
        for (int block = 0; block < 900; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < audio.getNumSamples(); ++s)
                    audio.setSample(ch, s, ch == 0 ? 0.2f : -0.3f);
            restored.processBlock(audio, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < audio.getNumSamples(); ++s)
                    require(std::isfinite(audio.getSample(ch, s)), "finite concurrent processing");
            playhead.ppq += 257.0 / 24000.0;
        }
        writer.join();
        require(restored.getPreview().latencySamples == breakvar::requiredLatencySamples(restored.getPattern(), 48000, 120), "runtime latency matches arrangement");
        restored.releaseResources();

        BreakVarAudioProcessor library(false);
        require(library.getFactoryPresetNames().size() >= 13, "factory collection available");
        require(library.loadFactoryPreset(1, error), "factory load");
        const auto factory = library.getPattern();
        const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile("processor-test-artifacts");
        require(directory.createDirectory().wasOk(), "test directory");
        const auto preset = directory.getChildFile("Saved pattern.shflr");
        require(library.savePreset(preset, error), "save named preset");
        require(library.getPresetName() == "Saved pattern", "preset saved name");
        require(library.randomiseSlices(345, error), "slice randomise");
        require(library.undoPattern() && library.getPattern() == factory, "slice randomise single undo");
        require(library.generateOrder(42, error), "new order");
        require(library.undoPattern() && library.getPattern() == factory, "new order single undo");
        require(library.loadFactoryPreset(2, error), "different factory");
        require(library.loadPreset(preset, error) && library.getPattern() == factory, "preset file roundtrip");
        const auto badPreset = directory.getChildFile("invalid.shflr");
        require(badPreset.replaceWithText("<BreakVarState version=\"1\" inputBars=\"9\"/>"), "malformed fixture");
        require(!library.loadPreset(badPreset, error) && library.getPattern() == factory, "invalid file atomic");
        require(!library.loadFactoryPreset(999, error), "invalid factory index");
        juce::MemoryBlock named;
        library.getStateInformation(named);
        restored.setStateInformation(named.getData(), static_cast<int>(named.getSize()));
        require(restored.getPresetName() == "Saved pattern", "preset name session recall");
        // Freeze the v0.1 field contract (version 1, no preset-name attribute).
        auto legacy = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
        legacy->setAttribute("version", 1);
        legacy->removeAttribute("name");
        legacy->removeAttribute("userPreset");
        juce::MemoryBlock legacyBinary;
        juce::AudioProcessor::copyXmlToBinary(*legacy, legacyBinary);
        restored.setStateInformation(legacyBinary.getData(), static_cast<int>(legacyBinary.getSize()));
        auto legacyExpected = pattern; legacyExpected.smoothTenths = 10;
        require(restored.getPattern() == legacyExpected && restored.getPresetName() == "Custom", "v0.1 unnamed state remains readable");

        // Managed presets use an isolated workspace library, never the user's AppData.
        const auto userDirectory = directory.getChildFile("user-library-" + juce::Uuid().toString());
        BreakVarAudioProcessor userA(false, userDirectory), userB(false, userDirectory);
        require(userA.getUserPresetNames().isEmpty() && !userDirectory.exists(), "reading user library creates no files");
        require(userA.loadFactoryPreset(1, error), "user preset source");
        const auto userPattern = userA.getPattern();
        require(userA.saveUserPreset("  My break  ", false, error) && error.isEmpty(), "save trimmed user preset");
        require(userA.getPresetName() == "My break", "user save updates active name");
        require(userB.getUserPresetNames() == juce::StringArray{"My break"}, "other instance sees saved user name");
        require(userB.loadUserPreset("my BREAK", error) && userB.getPattern() == userPattern
                && userB.getPresetName() == "My break", "user load ignores case and restores canonical name");
        require(userB.loadFactoryPreset(2, error), "replacement source");
        const auto replacement = userB.getPattern();
        const auto unchangedName = userB.getPresetName();
        require(!userB.saveUserPreset("MY BREAK", false, error) && error.isNotEmpty(), "case collision requires replacement");
        require(userB.getPresetName() == unchangedName && userB.getPattern() == replacement, "collision does not rename or modify active pattern");
        require(userB.saveUserPreset("MY BREAK", true, error), "explicit case-insensitive replacement");
        require(userA.loadUserPreset("My break", error) && userA.getPattern() == replacement
                && userA.getPresetName() == "MY BREAK", "replacement visible across existing instances");
        require(userA.getUserPresetNames().size() == 1, "replacement creates no duplicate");
        require(userA.saveUserPreset("Second", false, error), "independent second user preset");
        require(userB.getUserPresetNames().size() == 2, "second instance refresh retains both presets");
        for (const auto& bad : juce::StringArray{"", "  ", "../escape", "a\\b", "a:b", "a?b", "bad\nname", ".", "..", "trailing.", juce::String::repeatedString("x", 65)})
        {
            const auto before = userA.getPresetName();
            require(!userA.saveUserPreset(bad, true, error) && error.isNotEmpty(), "invalid user name rejected");
            require(userA.getPresetName() == before && userA.getPattern() == replacement, "invalid name preserves model and active name");
        }
        require(!userA.loadUserPreset("Missing", error) && error.isNotEmpty()
                && userA.getPresetName() == "Second" && userA.getPattern() == replacement, "missing user load preserves state");
        BreakVarAudioProcessor restarted(false, userDirectory);
        require(restarted.getUserPresetNames().size() == 2 && restarted.loadUserPreset("second", error)
                && restarted.getPattern() == replacement, "user preset survives fresh processor");
        juce::MemoryBlock userState;
        restarted.getStateInformation(userState);
        BreakVarAudioProcessor otherProject(false, directory.getChildFile("unrelated-library"));
        otherProject.setStateInformation(userState.getData(), static_cast<int>(userState.getSize()));
        require(otherProject.getPattern() == replacement && otherProject.getPresetName() == "Second", "session embeds active user arrangement independently of library");
        const auto blockedDirectory = directory.getChildFile("blocked-" + juce::Uuid().toString());
        require(blockedDirectory.replaceWithText("not a directory"), "blocked library fixture");
        BreakVarAudioProcessor blocked(false, blockedDirectory);
        require(!blocked.saveUserPreset("Cannot save", false, error) && error.isNotEmpty()
                && blocked.getPresetName() == "Original", "save error does not rename active preset");
        const auto corruptUserFile = userDirectory.getChildFile("corrupt.shflr");
        const juce::String corruptContents = "<BreakVarState version=\"1\" name=\"Damaged\" inputBars=\"999\"/>";
        require(corruptUserFile.replaceWithText(corruptContents), "corrupt user fixture");
        require(!restarted.loadUserPreset("Damaged", error) && restarted.getPresetName() == "Second", "corrupt user load preserves active name");
        require(restarted.saveUserPreset("Damaged", false, error)
                && corruptUserFile.loadFileAsString() == corruptContents, "saving never overwrites corrupt library data");

        // Separate processors write concurrently without a stale whole-bank snapshot losing either save.
        bool savedA = false, savedB = false;
        std::jthread firstSave([&] { juce::String why; savedA = userA.saveUserPreset("Concurrent A", false, why); });
        std::jthread secondSave([&] { juce::String why; savedB = userB.saveUserPreset("Concurrent B", false, why); });
        firstSave.join(); secondSave.join();
        require(savedA && savedB && restarted.getUserPresetNames().size() == 5, "concurrent instances preserve every saved preset");

        require(userA.loadFactoryPreset(0, error) && !userA.isCurrentUserPreset(), "factory source identified");
        require(userA.saveUserPreset("Original", false, error) && userA.isCurrentUserPreset(), "user source identified despite matching factory name");
        juce::MemoryBlock userIdentity;
        userA.getStateInformation(userIdentity);
        userB.setStateInformation(userIdentity.getData(), static_cast<int>(userIdentity.getSize()));
        require(userB.getPresetName() == "Original" && userB.isCurrentUserPreset(), "session preserves user source with factory name collision");
        require(userB.updatePattern(userB.getPattern(), error) && userB.isCurrentUserPreset(), "no-op edit preserves user source");
        require(userB.loadFactoryPreset(0, error) && !userB.isCurrentUserPreset(), "same-pattern factory selection resets source");
        require(userB.loadUserPreset("original", error) && userB.isCurrentUserPreset(), "same-pattern user selection restores source");
        require(userB.loadFactoryPreset(1, error) && !userB.isCurrentUserPreset(), "changed factory selection resets source");
        require(userB.loadUserPreset("Original", error) && userB.isCurrentUserPreset(), "user source before editing");
        auto identityEdit = userB.getPattern();
        identityEdit.outputBars = 8;
        require(userB.updatePattern(identityEdit, error) && !userB.isCurrentUserPreset(), "manual edit resets user source");
        require(userB.undoPattern() && !userB.isCurrentUserPreset(), "undo remains custom source");
        require(userB.redoPattern() && !userB.isCurrentUserPreset(), "redo remains custom source");
        require(userB.loadUserPreset("Original", error) && userB.isCurrentUserPreset(), "user source before legacy load");
        userB.setStateInformation(legacyBinary.getData(), static_cast<int>(legacyBinary.getSize()));
        require(!userB.isCurrentUserPreset(), "legacy session has non-user source");
        require(userB.loadUserPreset("Original", error) && userB.isCurrentUserPreset(), "user source before file load");
        require(userB.loadPreset(preset, error) && !userB.isCurrentUserPreset(), "legacy file load has non-user source");

        require(library.loadFactoryPreset(0, error), "atomic preset fixture");
        std::jthread presetWriter([&]
        {
            for (int i = 0; i < 500; ++i) { juce::String why; library.loadFactoryPreset(i % 13, why); }
        });
        BreakVarAudioProcessor recall(false);
        for (int i = 0; i < 500; ++i)
        {
            juce::MemoryBlock snapshot;
            library.getStateInformation(snapshot);
            recall.setStateInformation(snapshot.getData(), static_cast<int>(snapshot.getSize()));
            const int index = library.getFactoryPresetNames().indexOf(recall.getPresetName());
            require(index >= 0 && recall.getPattern() == breakvar::factoryPatterns()[static_cast<std::size_t>(index)].pattern,
                    "preset name and arrangement are one transaction");
        }
        presetWriter.join();

        require(library.loadFactoryPreset(0, error), "original capture fixture");
        auto capturePattern = library.getPattern();
        capturePattern.outputBars = 1;
        require(library.updatePattern(capturePattern, error), "capture short loop");
        library.setRateAndBufferSizeDetails(8000, 257);
        library.prepareToPlay(8000, 257);
        playhead.ppq = 0;
        library.setPlayHead(&playhead);
        const auto wav = directory.getChildFile("output.wav");
        require(!library.hasOutputReady() && !library.writeOutputWav(wav, error), "incomplete export rejected");
        for (int block = 0; block < 200; ++block)
        {
            for (int s = 0; s < 257; ++s) { audio.setSample(0, s, 0.2f); audio.setSample(1, s, -0.3f); }
            library.processBlock(audio, midi);
            playhead.ppq += 257.0 / 4000.0;
        }
        require(library.hasOutputReady(), "complete output captured");
        playhead.playing = false;
        library.processBlock(audio, midi);
        require(library.hasOutputReady(), "completed output retained on stop");
        library.releaseResources();
        require(library.hasOutputReady(), "host release retains completed output");
        require(library.writeOutputWav(wav, error), "write completed output WAV");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(wav.createInputStream().release(), true));
        require(reader && reader->sampleRate == 8000 && reader->numChannels == 2
                && reader->bitsPerSample == 24 && reader->lengthInSamples == 16000, "export rate channels depth and loop length");
        juce::AudioBuffer<float> exported(2, 16000);
        require(reader->read(&exported, 0, 16000, 0, true, true), "decode exported audio");
        require(std::abs(exported.getSample(0, 2000) - 0.2f) < 0.001f
                && std::abs(exported.getSample(1, 2000) + 0.3f) < 0.001f, "export contains wet audio");
        capturePattern.outputBars = 2;
        require(library.updatePattern(capturePattern, error), "edit after capture");
        require(!library.hasOutputReady() && !library.writeOutputWav(wav, error), "pending audio edit rejects stale export immediately");
        BreakVarAudioProcessor endpoint(false);
        require(endpoint.loadFactoryPreset(1, error), "release endpoint straight fixture");
        endpoint.setRateAndBufferSizeDetails(8000, 257);
        endpoint.prepareToPlay(8000, 257);
        playhead.ppq = 0; playhead.playing = true;
        endpoint.setPlayHead(&playhead);
        for (int offset = 0; offset < 16000;)
        {
            const int count = std::min(257, 16000 - offset);
            for (int s = 0; s < count; ++s) { audio.setSample(0, s, 0.2f); audio.setSample(1, s, -0.3f); }
            juce::AudioBuffer<float> block(audio.getArrayOfWritePointers(), 2, count);
            endpoint.processBlock(block, midi);
            offset += count; playhead.ppq = offset / 4000.0;
        }
        require(!endpoint.hasOutputReady(), "nominal endpoint awaits actual wrap or host release");
        endpoint.releaseResources();
        require(endpoint.hasOutputReady() && endpoint.writeOutputWav(directory.getChildFile("released-endpoint.wav"), error),
                "host release finalizes exact endpoint without stopped audio callback");
        endpoint.prepareToPlay(8000, 257); playhead.ppq = 0;
        endpoint.processBlock(audio, midi);
        endpoint.releaseResources();
        require(!endpoint.hasOutputReady(), "host release cannot export partial first loop");
        // Software-only images: no peer, native window, device or input control.
        std::unique_ptr<juce::AudioProcessorEditor> editor(library.createEditor());
        for (const auto size : {juce::Point<int>{1340, 820}, juce::Point<int>{1140, 740}})
        {
            editor->setSize(size.x, size.y);
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
            juce::MemoryOutputStream imageBytes;
            juce::PNGImageFormat png;
            require(png.writeImageToStream(image, imageBytes), "offscreen editor render");
            require(directory.getChildFile("editor-" + juce::String(size.x) + ".png")
                .replaceWithData(imageBytes.getData(), imageBytes.getDataSize()), "save editor render");
        }
        std::cout << "Processor state, undo, presets, WAV capture, latency and concurrent publication checks passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
