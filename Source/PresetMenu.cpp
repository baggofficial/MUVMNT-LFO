#include "Fonts.h"
#include "PresetMenu.h"

namespace
{
    bool usePrefs() { return juce::SystemStats::getEnvironmentVariable("FREELFO_NO_PREFS", "").isEmpty(); }

    // Unit-square outline of a curve (y up: 0% at the bottom, 100% at the top).
    juce::Path curvePath(const Envelope& e)
    {
        juce::Path p;
        constexpr int n = 64;
        for (int i = 0; i <= n; ++i)
        {
            // the last sample is the loop's end point itself (not a wrap to the start), so a sharp final drop shows
            const float x = (float) i / n, y = i == n ? e.points.back().y : e.evaluate(x);
            if (i == 0) p.startNewSubPath(x, 1.f - y); else p.lineTo(x, 1.f - y);
        }
        return p;
    }

    juce::AffineTransform toRect(juce::Rectangle<float> r) { return juce::AffineTransform::scale(r.getWidth(), r.getHeight()).translated(r.getX(), r.getY()); }

    // The popup: a grid of curve icons. Hover for the name (tooltip), click to apply, right-click one of your own to delete it.
    class PresetPanel : public juce::Component, public juce::TooltipClient
    {
    public:
        PresetPanel(FreeLfoProcessor& p, int slotIndex, std::function<void()> onChanged, juce::LookAndFeel& laf, std::function<void()> onDismiss)
            : proc(p), slot(slotIndex), changed(std::move(onChanged)), dismiss(std::move(onDismiss))
        {
            setLookAndFeel(&laf);
            for (auto* b : { &saveBtn, &copyBtn, &pasteBtn }) addAndMakeVisible(*b);
            saveBtn.setTooltip("Save the current curve as your own preset");
            copyBtn.setTooltip("Copy the current curve (paste it into another band or plugin)");
            pasteBtn.setTooltip("Paste a copied curve");
            saveBtn.onClick = [this] { askName(); };
            copyBtn.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(presets::toText(proc.getEnvelope(slot))); close(); };
            pasteBtn.onClick = [this] {
                const auto text = juce::SystemClipboard::getTextFromClipboard().toStdString();
                Envelope e;
                if (presets::fromText(text, e)) apply(e);
                else if (presets::hasTooManyPoints(text)) showNote("Not pasted: over " + limitText());
            };
            reload();
        }
        ~PresetPanel() override { setLookAndFeel(nullptr); }

        juce::String getTooltip() override { return hover >= 0 && hover < (int) tiles.size() ? tiles[(size_t) hover].name : juce::String(); }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(theme::panel);
            g.setFont(fonts::semibold(11.f).withExtraKerningFactor(0.06f));
            for (auto& h : headings)
            {
                g.setColour(theme::sub);
                g.drawText(h.text.toUpperCase(), h.area, juce::Justification::centredLeft);
            }
            for (int i = 0; i < (int) tiles.size(); ++i)
            {
                const auto& t = tiles[(size_t) i];
                const bool over = i == hover;
                auto r = t.area.toFloat();
                g.setColour(over ? theme::accent.withAlpha(0.22f) : theme::bg);
                g.fillRoundedRectangle(r, 6.f);
                g.setColour(over ? theme::accent : theme::line);
                g.drawRoundedRectangle(r.reduced(0.5f), 6.f, 1.f);
                auto c = r.reduced(10.f, 9.f);
                g.setColour(theme::text.withAlpha(0.18f));                       // the 100% and 0% lines
                g.drawLine(c.getX(), c.getY(), c.getRight(), c.getY(), 0.6f);
                g.drawLine(c.getX(), c.getBottom(), c.getRight(), c.getBottom(), 0.6f);
                const juce::PathStrokeType stroke(1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
                if (t.bandSet < 0)
                {
                    const auto ink = slot > 0 ? theme::bandColour(slot - 1) : theme::accent;
                    g.setColour(over ? ink.brighter(0.3f) : ink);
                    g.strokePath(t.path, stroke, toRect(c));
                    continue;
                }
                // a multiband preset: every band's curve in its own colour, the lows on top
                const auto& set = bandSets[(size_t) t.bandSet];
                for (int b = 2; b >= 0; --b)
                {
                    if (b == 1 && !set.three) continue;
                    g.setColour(theme::bandColour(b));
                    g.strokePath(t.bandPaths[(size_t) b], stroke, toRect(c));
                }
            }
            if (dividerX > 0)
            {
                g.setColour(theme::line);
                g.drawLine((float) dividerX, 12.f, (float) dividerX, (float) saveBtn.getY() - 10.f, 1.f);
            }
        }

        void mouseMove(const juce::MouseEvent& e) override { setHover(indexAt(e.getPosition())); }
        void mouseExit(const juce::MouseEvent&) override { setHover(-1); }

        void mouseUp(const juce::MouseEvent& e) override
        {
            const int i = indexAt(e.getPosition());
            if (i < 0) return;
            if (e.mods.isPopupMenu())
            {
                if (!tiles[(size_t) i].user) return;
                juce::PopupMenu m;
                m.setLookAndFeel(&getLookAndFeel());
                m.addItem(1, "Delete \"" + tiles[(size_t) i].name + "\"");
                const auto file = tiles[(size_t) i].file;
                m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(e.getScreenPosition(), e.getScreenPosition())),
                                [safe = juce::Component::SafePointer<PresetPanel>(this), file](int r) { if (r == 1) { file.deleteFile(); if (safe != nullptr) safe->reload(); } });
                return;
            }
            const auto& t = tiles[(size_t) i];
            if (t.bandSet < 0) { apply(t.env); return; }
            const auto& set = bandSets[(size_t) t.bandSet];
            proc.applyBandCurves({ set.low, set.mid, set.high }, set.three);
            close();
        }

    private:
        struct Tile
        {
            juce::String name; Envelope env; juce::Path path; juce::Rectangle<int> area; bool user = false; juce::File file;
            int bandSet = -1; // >= 0: a multiband preset (an index into bandSets)
            std::array<juce::Path, 3> bandPaths;
        };
        struct Heading { juce::String text; juce::Rectangle<int> area; };

        void setHover(int i) { if (i != hover) { hover = i; repaint(); } }
        int indexAt(juce::Point<int> p) const
        {
            for (int i = 0; i < (int) tiles.size(); ++i) if (tiles[(size_t) i].area.contains(p)) return i;
            return -1;
        }

        void close()
        {
            if (changed) changed();
            if (dismiss) dismiss();
        }

        void apply(const Envelope& e) { proc.applyCurve(e, slot); close(); }

        static juce::String limitText() { return juce::String((int) Envelope::maxPoints) + " points"; }

        // A short message across the bottom of the popup, just above the buttons
        void showNote(const juce::String& text)
        {
            note = text;
            repaint();
            juce::Timer::callAfterDelay(4000, [safe = juce::Component::SafePointer<PresetPanel>(this), text] {
                if (safe != nullptr && safe->note == text) { safe->note.clear(); safe->repaint(); }
            });
        }

        void paintOverChildren(juce::Graphics& g) override
        {
            if (note.isEmpty()) return;
            const auto r = juce::Rectangle<float>(8.f, (float) saveBtn.getY() - 28.f, (float) getWidth() - 16.f, 24.f);
            g.setColour(theme::panel);
            g.fillRoundedRectangle(r, 6.f);
            g.setColour(theme::amber);
            g.drawRoundedRectangle(r, 6.f, 1.2f);
            g.setColour(theme::text);
            g.setFont(fonts::regular(11.5f));
            g.drawFittedText(note, r.reduced(6.f, 0.f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }

        void askName()
        {
            // a project from before the limit can hold more points than a preset may: refuse instead of saving a file that won't load
            if (proc.getEnvelope(slot).points.size() > Envelope::maxPoints) { showNote("Not saved: over " + limitText()); return; }
            auto* w = new juce::AlertWindow("Save curve as preset", "Name of the preset:", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor("name", "", "");
            w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            const Envelope current = proc.getEnvelope(slot);
            w->enterModalState(true, juce::ModalCallbackFunction::create([w, current](int r) {
                if (r != 1) return;
                const auto name = juce::File::createLegalFileName(w->getTextEditorContents("name").trim());
                if (name.isEmpty()) return;
                PresetMenu::userFolder().getChildFile(name + ".lfocurve").replaceWithText(presets::toText(current));
            }), true);
            close();
        }

        // Build the tile list (factory + user files) and lay it out.
        void reload()
        {
            tiles.clear();
            headings.clear();
            struct Group { juce::String name; std::vector<Tile> items; };
            std::vector<Group> groups;
            for (auto& pr : presets::factory())
            {
                if (groups.empty() || groups.back().name != juce::String(pr.group)) groups.push_back({ juce::String(pr.group), {} });
                Tile t; t.name = pr.name; t.env = pr.env; t.path = curvePath(pr.env);
                groups.back().items.push_back(std::move(t));
            }
            Group mine { "My presets", {} };
            auto files = PresetMenu::userFolder().findChildFiles(juce::File::findFiles, false, "*.lfocurve");
            std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) { return a.getFileName().compareIgnoreCase(b.getFileName()) < 0; });
            int overLimit = 0;
            for (auto& f : files)
            {
                Envelope e;
                const auto text = f.loadFileAsString().trim().toStdString();
                if (!presets::fromText(text, e)) { if (presets::hasTooManyPoints(text)) ++overLimit; continue; }
                Tile t; t.name = f.getFileNameWithoutExtension(); t.env = e; t.path = curvePath(e); t.user = true; t.file = f;
                mine.items.push_back(std::move(t));
            }
            if (!mine.items.empty()) groups.push_back(std::move(mine));

            constexpr int pad = 12, tw = 56, th = 40, gap = 6, cols = 4, headH = 20;
            constexpr int columnW = cols * tw + (cols - 1) * gap, columnGap = 20;

            // In Multiband, a first column holds the presets that set every band at once (two per row, so they read well)
            const bool multi = slot > 0;
            int multiBottom = pad;
            dividerX = 0;
            bandSets.clear();
            if (multi)
            {
                bandSets = presets::factoryMultiband();
                constexpr int bw2 = (columnW - gap) / 2, bh = 54;
                for (const bool three : { true, false })
                {
                    headings.push_back({ three ? "All 3 bands" : "Low + High", { pad, multiBottom, columnW, headH } });
                    multiBottom += headH;
                    int col = 0;
                    for (size_t k = 0; k < bandSets.size(); ++k)
                    {
                        const auto& set = bandSets[k];
                        if (set.three != three) continue;
                        Tile t;
                        t.name = juce::String(set.name) + (three ? " (sets 3 bands)" : " (sets 2 bands)");
                        t.bandSet = (int) k;
                        t.bandPaths = { curvePath(set.low), curvePath(set.mid), curvePath(set.high) };
                        t.area = { pad + col * (bw2 + gap), multiBottom, bw2, bh };
                        tiles.push_back(std::move(t));
                        if (++col == 2) { col = 0; multiBottom += bh + gap; }
                    }
                    if (col != 0) multiBottom += bh + gap;
                    multiBottom += 4;
                }
                dividerX = pad + columnW + columnGap / 2;
            }

            const int left = multi ? pad + columnW + columnGap : pad;
            int y = pad;
            for (auto& gr : groups)
            {
                headings.push_back({ multi && gr.name == "Pumps" ? "This band: pumps" : gr.name, { left, y, columnW, headH } });
                y += headH;
                int col = 0;
                for (auto& t : gr.items)
                {
                    t.area = { left + col * (tw + gap), y, tw, th };
                    tiles.push_back(std::move(t));
                    if (++col == cols) { col = 0; y += th + gap; }
                }
                if (col != 0) y += th + gap;
                y += 4;
            }
            y = juce::jmax(y, multiBottom);
            const int w = left + columnW + pad;
            auto row = juce::Rectangle<int>(pad, y + 2, w - 2 * pad, 26);
            const int bw = (row.getWidth() - 2 * gap) / 3;
            saveBtn.setBounds(row.removeFromLeft(bw)); row.removeFromLeft(gap);
            copyBtn.setBounds(row.removeFromLeft(bw)); row.removeFromLeft(gap);
            pasteBtn.setBounds(row);
            pasteBtn.setEnabled(clipboardHasCurve()); // also for a curve over the limit, so a click can say why it is refused
            setSize(w, y + 2 + 26 + pad);
            hover = -1;
            if (overLimit > 0)
                showNote(juce::String(overLimit) + (overLimit == 1 ? " preset" : " presets") + " hidden: over " + limitText());
            repaint();
        }

        static bool clipboardHasCurve()
        {
            Envelope probe;
            const auto text = juce::SystemClipboard::getTextFromClipboard().toStdString();
            return presets::fromText(text, probe) || presets::hasTooManyPoints(text);
        }

        FreeLfoProcessor& proc;
        int slot;
        std::function<void()> changed, dismiss;
        std::vector<Tile> tiles;
        std::vector<Heading> headings;
        std::vector<BandPreset> bandSets;
        int dividerX = 0;
        int hover = -1;
        juce::String note;
        juce::TextButton saveBtn { "Save as..." }, copyBtn { "Copy" }, pasteBtn { "Paste" };
    };
}

juce::File PresetMenu::userFolder()
{
    juce::File dir;
    if (usePrefs())
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "FreeLfoTool UI";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.folderName = "FreeLfo";
        dir = o.getDefaultFile().getParentDirectory().getChildFile("Presets");
    }
    else
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("FreeLfoTestPresets");
    dir.createDirectory();
    return dir;
}

void PresetButton::paintButton(juce::Graphics& g, bool over, bool down)
{
    auto b = getLocalBounds().toFloat().reduced(0.5f, 2.5f);
    g.setColour(down ? theme::track : over ? theme::bg : theme::panel);
    g.fillRoundedRectangle(b, 6.f);
    g.setColour(theme::line);
    g.drawRoundedRectangle(b, 6.f, 1.f);

    // a tiny sidechain curve, the word, then a chevron
    static const juce::Path curve = curvePath(Envelope());
    auto icon = b.withTrimmedLeft(8.f).withWidth(20.f).withSizeKeepingCentre(20.f, 11.f);
    g.setColour(theme::text.withAlpha(over ? 1.f : 0.8f));
    g.strokePath(curve, juce::PathStrokeType(1.5f), toRect(icon));
    g.setFont(fonts::semibold(12.f));
    g.setColour(theme::text);
    g.drawText("Presets", b.withTrimmedLeft(34.f).withTrimmedRight(18.f), juce::Justification::centredLeft, false);

    juce::Path chev;
    const auto c = juce::Point<float>(b.getRight() - 12.f, b.getCentreY());
    chev.startNewSubPath(c.x - 3.5f, c.y - 1.6f);
    chev.lineTo(c.x, c.y + 1.8f);
    chev.lineTo(c.x + 3.5f, c.y - 1.6f);
    g.setColour(theme::sub);
    g.strokePath(chev, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

std::unique_ptr<juce::Component> PresetMenu::createPanel(FreeLfoProcessor& proc, int slot, std::function<void()> changed, juce::LookAndFeel& laf)
{
    return std::make_unique<PresetPanel>(proc, slot, std::move(changed), laf, nullptr);
}

// The popup lives inside the plugin window (no extra OS window, so it always drops straight down from the button
// and can never be clipped by the host). A transparent layer over the whole editor catches clicks outside it.
namespace
{
    class Overlay : public juce::Component, private juce::ComponentListener
    {
    public:
        Overlay(juce::Component& hostC, juce::Component& anchorC, std::unique_ptr<juce::Component> content)
            : host(hostC), anchor(anchorC), panel(std::move(content))
        {
            addAndMakeVisible(*panel);
            host.addAndMakeVisible(this);
            host.addComponentListener(this);
            setWantsKeyboardFocus(true);
            fit();
            toFront(false);
            grabKeyboardFocus();
        }
        ~Overlay() override { host.removeComponentListener(this); }

        std::function<void()> onDismiss;

        void paint(juce::Graphics& g) override
        {
            const auto r = panel->getBounds();
            juce::DropShadow(juce::Colours::black.withAlpha(0.45f), 14, { 0, 4 }).drawForRectangle(g, r);
        }
        void paintOverChildren(juce::Graphics& g) override
        {
            g.setColour(theme::line.brighter(0.2f));
            g.drawRoundedRectangle(panel->getBounds().toFloat().reduced(0.5f), 8.f, 1.f);
        }
        void mouseDown(const juce::MouseEvent& e) override { if (!panel->getBounds().contains(e.getPosition())) dismiss(); }
        bool keyPressed(const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { dismiss(); return true; } return false; }
        void dismiss() { if (onDismiss) onDismiss(); }

    private:
        void fit()
        {
            setBounds(host.getLocalBounds());
            const auto ab = host.getLocalArea(&anchor, anchor.getLocalBounds());
            const int x = juce::jlimit(4, juce::jmax(4, getWidth() - panel->getWidth() - 4), ab.getCentreX() - panel->getWidth() / 2);
            const int y = juce::jmax(0, juce::jmin(ab.getBottom() + 4, getHeight() - panel->getHeight() - 2)); // inside a small window too
            panel->setTopLeftPosition(x, y);
        }
        void componentMovedOrResized(juce::Component&, bool, bool) override { fit(); }

        juce::Component& host;
        juce::Component& anchor;
        std::unique_ptr<juce::Component> panel;
    };
}

std::unique_ptr<juce::Component> PresetMenu::open(juce::Component& host, juce::Component& anchor, FreeLfoProcessor& proc, int slot,
                                                   std::function<void()> changed, std::function<void()> dismissRequested)
{
    // the panel and the overlay both ask for dismissal; the owner (the editor) closes it on its next message-loop turn
    auto panel = std::make_unique<PresetPanel>(proc, slot, std::move(changed), anchor.getLookAndFeel(), dismissRequested);
    auto overlay = std::make_unique<Overlay>(host, anchor, std::move(panel));
    overlay->onDismiss = std::move(dismissRequested);
    return overlay;
}
