// Random-click ("monkey") test: audio runs on its own thread while the real editor window is clicked, dragged,
// double-clicked, resized, closed and reopened at random, parameters are automated and projects are saved and loaded.
// It is most useful in a build with sanitizers (-fsanitize=address,undefined or -fsanitize=thread).
//
//   Run:  build/LfoMonkey_artefacts/Release/LfoMonkey [seed] [actions]      (exit code 0 = no broken invariant)
//
// It never presses Copy, Paste or Save as, so the system clipboard and your presets are left alone.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/PresetMenu.h"
#include "fake_host.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>

namespace
{
    std::mt19937 rng;
    float unit() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng); }
    int pick(int n) { return n <= 0 ? 0 : std::uniform_int_distribution<int>(0, n - 1)(rng); }

    void collect(juce::Component& c, std::vector<juce::Component::SafePointer<juce::Component>>& out)
    {
        for (auto* child : c.getChildren())
            if (child->isVisible() && child->getWidth() > 0 && child->getHeight() > 0)
            {
                out.push_back(child);
                collect(*child, out);
            }
    }

    juce::ModifierKeys randomMods()
    {
        int m = juce::ModifierKeys::leftButtonModifier; // never the right button: native menus need a real screen
        if (unit() < 0.2f) m |= juce::ModifierKeys::altModifier;
        if (unit() < 0.15f) m |= juce::ModifierKeys::shiftModifier;
        if (unit() < 0.1f) m |= juce::ModifierKeys::commandModifier;
        return juce::ModifierKeys(m);
    }

    // One gesture on one component, as JUCE would deliver it
    void gesture(juce::Component::SafePointer<juce::Component> target)
    {
        if (target == nullptr) return;
        const auto mods = randomMods();
        const juce::Point<float> down(unit() * (float) target->getWidth(), unit() * (float) target->getHeight());
        const auto time = juce::Time::getCurrentTime();
        auto event = [&](juce::Point<float> at, bool dragged, int clicks) {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.f, 0.f, 0.f, 0.f, 0.f,
                                    target.getComponent(), target.getComponent(), time, down, time, clicks, dragged);
        };
        target->mouseMove(event(down, false, 1));
        if (target == nullptr) return;
        const float kind = unit();
        target->mouseDown(event(down, false, 1));
        auto at = down;
        if (kind < 0.45f) // drag, sometimes far outside the component
            for (int i = 1 + pick(8); --i >= 0 && target != nullptr;)
            {
                at = unit() < 0.1f ? juce::Point<float>(unit() * 4000.f - 2000.f, unit() * 4000.f - 2000.f)
                                   : at.translated(unit() * 120.f - 60.f, unit() * 120.f - 60.f);
                target->mouseDrag(event(at, true, 1));
            }
        if (target != nullptr) target->mouseUp(event(at, kind < 0.45f, 1));
        if (kind > 0.7f && target != nullptr)
        {
            target->mouseDown(event(down, false, 2));
            if (target != nullptr) target->mouseDoubleClick(event(down, false, 2));
            if (target != nullptr) target->mouseUp(event(down, false, 2));
        }
        if (target != nullptr) target->mouseExit(event(at, false, 1));
    }

    bool curveIsSane(const Envelope& e, size_t most)
    {
        if (e.points.size() < 2 || e.points.size() > most) return false;
        if (e.points.front().x != 0.f || e.points.back().x != 1.f) return false;
        if (e.points.front().y != e.points.back().y) return false;
        float last = 0.f;
        for (const auto& p : e.points)
        {
            for (float v : { p.x, p.y, p.inDx, p.inDy, p.outDx, p.outDy, p.bend, p.bendX }) if (!std::isfinite(v)) return false;
            if (p.x < last || p.x > 1.f || p.y < 0.f || p.y > 1.f) return false;
            last = p.x;
        }
        return true;
    }
}

int main(int argc, char** argv)
{
   #if ! JUCE_WINDOWS
    setenv("FREELFO_NO_PREFS", "1", 1); // never touch the user's saved preferences or presets
   #else
    _putenv_s("FREELFO_NO_PREFS", "1");
   #endif
    juce::ScopedJuceInitialiser_GUI gui;
    const unsigned seed = argc > 1 ? (unsigned) std::strtoul(argv[1], nullptr, 10) : 1u;
    const int actions = argc > 2 ? std::atoi(argv[2]) : 4000;
    rng.seed(seed);

    FreeLfoProcessor proc;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses = { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo() }; // with sidechain
    layout.outputBuses = { juce::AudioChannelSet::stereo() };
    proc.setBusesLayout(layout);
    FakeHost host;
    proc.setPlayHead(&host);
    proc.setRateAndBufferSizeDetails(48000.0, 512);
    proc.prepareToPlay(48000.0, 512);

    std::atomic<bool> stop { false };
    std::atomic<long> blocks { 0 }, badSamples { 0 };
    std::atomic<int> action { 0 };
    std::atomic<int> lastKind { 0 }; // the roll (0..999) that chose the latest UI action: together they explain a bad sample
    std::thread audio([&] {
        std::mt19937 r(seed + 1);
        std::uniform_real_distribution<float> u(-0.9f, 0.9f);
        juce::AudioBuffer<float> big(4, 2048);
        juce::MidiBuffer midi;
        while (!stop.load())
        {
            const int n = 1 + (int) (r() % 1024u);
            juce::AudioBuffer<float> buf(big.getArrayOfWritePointers(), 4, n);
            for (int c = 0; c < 4; ++c) for (int i = 0; i < n; ++i) buf.setSample(c, i, u(r));
            midi.clear();
            if (r() % 40u == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), (int) (r() % (unsigned) n));
            proc.processBlock(buf, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    if (!std::isfinite(buf.getSample(c, i)) || std::fabs(buf.getSample(c, i)) > 400.f) // +48 dB of trim is legal
                        if (badSamples++ < 6)
                        {
                            auto v = [&](const char* id) { return (double) proc.apvts.getRawParameterValue(id)->load(); };
                            for (auto* prm : proc.getParameters()) // name any setting that is itself the bad value
                                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm))
                                {
                                    const float raw = proc.apvts.getRawParameterValue(rp->paramID)->load();
                                    const auto& range = rp->getNormalisableRange();
                                    if (!std::isfinite(raw) || raw < range.start - 0.001f || raw > range.end + 0.001f)
                                        std::printf("  setting not finite or out of range: %s = %g\n", rp->paramID.toRawUTF8(), (double) raw);
                                }
                            std::printf("  bad sample %g (action %d, kind %d): in %g out %g mb %g engine %g bypass %g mode %g hz %g\n", (double) buf.getSample(c, i), action.load(), lastKind.load(),
                                        v("inGain"), v("outGain"), v("mb"), v("mbEngine"), v("bypass"), v("mode"), v("hz"));
                        }
            host.ppq += (double) n * host.bpm / 60.0 / 48000.0;
            ++blocks;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditorIfNeeded());
    std::unique_ptr<juce::Component> popup;
    juce::MemoryBlock saved;
    proc.getStateInformation(saved);
    int failures = 0, opened = 1, loads = 0;
    auto pump = [](int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); };

    for (int a = 0; a < actions && failures == 0; ++a)
    {
        const float what = unit();
        action = a;
        lastKind = (int) (what * 1000.f);
        if (editor == nullptr)
        {
            editor.reset(proc.createEditorIfNeeded());
            ++opened;
        }
        else if (what < 0.62f)
        {
            std::vector<juce::Component::SafePointer<juce::Component>> all;
            collect(*editor, all);
            if (!all.empty())
            {
                // the graph and the band strip are where the logic is: they get extra clicks
                auto c = all[(size_t) pick((int) all.size())];
                if (unit() < 0.45f)
                    for (auto& cand : all)
                        if (cand != nullptr && cand->getWidth() > 400 && cand->getHeight() > 150) { c = cand; if (unit() < 0.7f) break; }
                if (auto* text = dynamic_cast<juce::TextButton*>(c.getComponent()))
                {
                    const auto t = text->getButtonText();
                    if (t != "Copy" && t != "Paste" && t.indexOf("Save") < 0) text->triggerClick();
                }
                else if (auto* button = dynamic_cast<juce::Button*>(c.getComponent())) button->triggerClick();
                else if (auto* box = dynamic_cast<juce::ComboBox*>(c.getComponent()))
                    box->setSelectedItemIndex(pick(box->getNumItems()), juce::sendNotificationSync);
                else if (dynamic_cast<juce::TextEditor*>(c.getComponent()) != nullptr)
                {
                    static const char* typed[] = { "0.5", "-999", "1/8", "2 bars", "banana", "", "1e39", "nan" };
                    if (auto* te = dynamic_cast<juce::TextEditor*>(c.getComponent()))
                    {
                        te->setText(typed[pick(8)], false);
                        if (unit() < 0.5f) te->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
                    }
                }
                else gesture(c);
            }
        }
        else if (what < 0.74f) // host automation
        {
            const auto& params = proc.getParameters();
            for (int i = 1 + pick(6); --i >= 0;) params[pick(params.size())]->setValueNotifyingHost(unit());
            if (unit() < 0.5f) if (auto* b = proc.apvts.getParameter("bypass")) b->setValueNotifyingHost(0.f);
        }
        else if (what < 0.79f) { if (unit() < 0.5f) proc.undo(); else proc.redo(); }
        else if (what < 0.83f) // a preset, as the popup applies it
        {
            const auto list = presets::factory();
            proc.applyCurve(list[(size_t) pick((int) list.size())].env, pick(FreeLfoProcessor::numSlots));
            if (unit() < 0.4f) // a multiband preset: all the bands and the band count at once
            {
                const auto sets = presets::factoryMultiband();
                const auto& set = sets[(size_t) pick((int) sets.size())];
                proc.applyBandCurves({ set.low, set.mid, set.high }, set.three);
            }
        }
        else if (what < 0.86f) editor->setSize(880 + pick(1121), 500 + pick(801));
        else if (what < 0.875f) { popup.reset(); proc.editorBeingDeleted(editor.get()); editor.reset(); } // as a host closes it
        else if (what < 0.89f) popup = PresetMenu::createPanel(proc, pick(FreeLfoProcessor::numSlots), nullptr, editor->getLookAndFeel());
        else if (what < 0.905f) proc.getStateInformation(saved);
        else if (what < 0.915f) { proc.setStateInformation(saved.getData(), (int) saved.getSize()); ++loads; }
        else if (what < 0.925f) // damaged project data: flip some bytes
        {
            juce::MemoryBlock bad(saved);
            for (int i = 1 + pick(20); --i >= 0 && bad.getSize() > 0;) static_cast<char*>(bad.getData())[pick((int) bad.getSize())] = (char) pick(256);
            proc.setStateInformation(bad.getData(), (int) (unit() < 0.3f ? (size_t) pick((int) bad.getSize()) : bad.getSize()));
            proc.setStateInformation(saved.getData(), (int) saved.getSize());
        }
        else if (what < 0.94f) // damaged curve text
        {
            auto text = presets::toText(proc.getEnvelope(pick(FreeLfoProcessor::numSlots)));
            for (int i = 1 + pick(12); --i >= 0;) text[(size_t) pick((int) text.size())] = "0123456789,;.-einaf "[pick(20)];
            Envelope e;
            if (presets::fromText(text, e) && !curveIsSane(e, Envelope::maxPoints)) { std::printf("bad curve accepted from text: %s\n", text.c_str()); ++failures; }
        }
        else if (editor != nullptr) // paint what is on screen, and let timers and async work run
        {
            auto shot = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
            if (popup != nullptr) popup->createComponentSnapshot(popup->getLocalBounds(), true, 1.0f);
            pump(18);
        }

        juce::PopupMenu::dismissAllActiveMenus();
        if (a % 5 == 0) pump(1);
        for (int s = 0; s < FreeLfoProcessor::numSlots; ++s)
            if (!curveIsSane(proc.getEnvelope(s), Envelope::maxPoints))
            {
                std::printf("broken curve in slot %d after action %d (%zu points)\n", s, a, proc.getEnvelope(s).points.size());
                ++failures;
            }
        if (a % 500 == 499) { std::printf("  %d actions, %ld audio blocks\n", a + 1, blocks.load()); std::fflush(stdout); }
    }

    popup.reset();
    if (editor != nullptr) proc.editorBeingDeleted(editor.get());
    editor.reset();
    pump(50);
    stop = true;
    audio.join();
    if (badSamples.load() != 0) { std::printf("bad audio samples: %ld (of %ld blocks run)\n", badSamples.load(), blocks.load()); ++failures; }
    std::printf("seed %u: %d actions, %ld audio blocks, editor opened %d times, %d project loads: %s\n",
                seed, actions, blocks.load(), opened, loads, failures == 0 ? "OK" : "FAILED");
    return failures == 0 ? 0 : 1;
}
