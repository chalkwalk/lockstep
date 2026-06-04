// Throwaway probe tool — confirms Ableton Push 1 input numbers, encoder/relative
// encoding, the three button colour spaces, pad RGB palette, touch strip
// (in + LED), and the 4-line display.
//
// PORT + MODE MUST MATCH (confirmed on hardware 2026-06-03):
//   * Live Port (port 1)  <-> Live mode (MODE_CHANGE byte 0)
//   * User Port (port 2)  <-> User mode (MODE_CHANGE byte 1)
// Within a matched pair the device is a host-driven "dumb surface" — host owns
// every LED/display, controls send raw MIDI (like the X-Touch in MCU mode).
// Neither mode is "autonomous" in practice; the names are just Ableton's. CC
// button LEDs respond on either pairing, but the *pads* (note-addressed) only
// light when the mode matches the connected port — hence Open auto-sends the
// matching mode. Lockstep will use the User Port + User mode (DrivenByMoss's
// port choice; avoids clashing with Ableton Live on the Live Port).
//
// The proprietary Ableton "dongle" handshake is Live AUTHENTICATING the device;
// it is NOT required for surface operation. DrivenByMoss drives Push 1 as a full
// surface on Bitwig with only a Device Inquiry, so the probe does the same.
//
// Not wired into the plugin; build with the push_probe CMake target.
// Cross-check values against PUSH1.md.

#include <JuceHeader.h>
#include <array>
#include <vector>

namespace lockstep
{

// ---------------------------------------------------------------------------
// Colour spaces. Each LED target on Push 1 lives in exactly one of these.
//   PadRGB   : pads (Note On, vel = palette index 0-127)
//   LowerRGB : lower display row CC 102-109 (CC val = palette index 0-127)
//   BiColor  : upper display row CC 20-27 + scenes CC 36-43 (CC val 0-24)
//   Mono     : white function buttons (CC val 0/1/4 + blink 2/3/5/6)
// ---------------------------------------------------------------------------
enum class Space { PadRGB, LowerRGB, BiColor, Mono };

struct CCEntry
{
    int         cc;
    const char* label;
    Space       space;
};

// All CC buttons with an LED, from PUSH1.md Tables 2 & 3.
// (Encoders 14/15/71-79 and footswitch 64/69 have no LEDs — handled in input
// annotation only, not here.)
static constexpr CCEntry kButtonCCs[] = {
    // Mono white function buttons
    {   3, "Tap Tempo",     Space::Mono },
    {   9, "Metronome",     Space::Mono },
    {  28, "Master",        Space::Mono },
    {  29, "Stop Clip",     Space::Mono },
    {  44, "Left",          Space::Mono },
    {  45, "Right",         Space::Mono },
    {  46, "Up",            Space::Mono },
    {  47, "Down",          Space::Mono },
    {  48, "Select",        Space::Mono },
    {  49, "Shift",         Space::Mono },
    {  50, "Note",          Space::Mono },
    {  51, "Session",       Space::Mono },
    {  52, "Add Device",    Space::Mono },
    {  53, "Add Track",     Space::Mono },
    {  54, "Octave Down",   Space::Mono },
    {  55, "Octave Up",     Space::Mono },
    {  56, "Repeat",        Space::Mono },
    {  57, "Accent",        Space::Mono },
    {  58, "Scales",        Space::Mono },
    {  59, "User",          Space::Mono },
    {  60, "Mute",          Space::Mono },
    {  61, "Solo",          Space::Mono },
    {  62, "In (device <)", Space::Mono },
    {  63, "Out (device >)",Space::Mono },
    {  85, "Play",          Space::Mono },
    {  86, "Record",        Space::Mono },
    {  87, "New",           Space::Mono },
    {  88, "Duplicate",     Space::Mono },
    {  89, "Automation",    Space::Mono },
    {  90, "Fixed Length",  Space::Mono },
    { 110, "Device",        Space::Mono },
    { 111, "Browse",        Space::Mono },
    { 112, "Track",         Space::Mono },
    { 113, "Clip",          Space::Mono },
    { 114, "Volume",        Space::Mono },
    { 115, "Pan & Send",    Space::Mono },
    { 116, "Quantize",      Space::Mono },
    { 117, "Double",        Space::Mono },
    { 118, "Delete",        Space::Mono },
    { 119, "Undo",          Space::Mono },
    // Upper display row (bi-colour)
    {  20, "Upper 1", Space::BiColor }, {  21, "Upper 2", Space::BiColor },
    {  22, "Upper 3", Space::BiColor }, {  23, "Upper 4", Space::BiColor },
    {  24, "Upper 5", Space::BiColor }, {  25, "Upper 6", Space::BiColor },
    {  26, "Upper 7", Space::BiColor }, {  27, "Upper 8", Space::BiColor },
    // Scene / side buttons (bi-colour). CC 43 = top ... CC 36 = bottom.
    {  43, "Scene 1 (top)", Space::BiColor }, {  42, "Scene 2", Space::BiColor },
    {  41, "Scene 3",       Space::BiColor }, {  40, "Scene 4", Space::BiColor },
    {  39, "Scene 5",       Space::BiColor }, {  38, "Scene 6", Space::BiColor },
    {  37, "Scene 7",       Space::BiColor }, {  36, "Scene 8 (bot)", Space::BiColor },
    // Lower display row (full RGB)
    { 102, "Lower 1", Space::LowerRGB }, { 103, "Lower 2", Space::LowerRGB },
    { 104, "Lower 3", Space::LowerRGB }, { 105, "Lower 4", Space::LowerRGB },
    { 106, "Lower 5", Space::LowerRGB }, { 107, "Lower 6", Space::LowerRGB },
    { 108, "Lower 7", Space::LowerRGB }, { 109, "Lower 8", Space::LowerRGB },
};
static constexpr int kNumCCs = static_cast<int>(std::size(kButtonCCs));

static const CCEntry* findCC(int cc)
{
    for (const auto& e : kButtonCCs)
        if (e.cc == cc) return &e;
    return nullptr;
}

// Annotate any incoming CC (LED buttons + encoders + footswitch).
static juce::String annotateCC(int cc)
{
    if (const auto* e = findCC(cc)) return juce::String(e->label);
    if (cc >= 71 && cc <= 78) return "ENC " + juce::String(cc - 71 + 1) + " turn";
    if (cc == 79) return "Master encoder turn";
    if (cc == 14) return "Tempo encoder turn";
    if (cc == 15) return "Swing encoder turn";
    if (cc == 64) return "Footswitch tip";
    if (cc == 69) return "Footswitch ring";
    if (cc == 1)  return "TOUCH STRIP (MODWHEEL mode, CC1)";
    if (cc == 7)  return "TOUCH STRIP (VOLUME mode, CC7)";
    if (cc == 10) return "TOUCH STRIP (PAN mode, CC10)";
    return "UNKNOWN CC";
}

// Annotate any incoming Note (pads + encoder/ribbon touch).
static juce::String annotateNote(int n)
{
    if (n >= 36 && n <= 99)
    {
        const int idx = n - 36;
        return "PAD col=" + juce::String(idx % 8) + " row=" + juce::String(idx / 8)
             + " (row 0 = bottom)";
    }
    if (n >= 0 && n <= 7) return "ENC " + juce::String(n + 1) + " touch";
    if (n == 8)  return "Master encoder touch";
    if (n == 9)  return "Swing encoder touch";
    if (n == 10) return "Tempo encoder touch";
    if (n == 12) return "TOUCH STRIP touch";
    return "UNKNOWN note";
}

// ---------------------------------------------------------------------------
// Thread-safe single-producer / single-consumer MIDI message queue.
// ---------------------------------------------------------------------------
class MidiQueue
{
public:
    static constexpr int kCapacity = 1024;

    void push(const juce::MidiMessage& m)
    {
        int s1, n1, s2, n2;
        fifo_.prepareToWrite(1, s1, n1, s2, n2);
        if (n1 > 0)      msgs_[static_cast<std::size_t>(s1)] = m;
        else if (n2 > 0) msgs_[static_cast<std::size_t>(s2)] = m;
        fifo_.finishedWrite((n1 + n2) > 0 ? 1 : 0);
    }

    template<typename F>
    void drain(F&& fn)
    {
        int s1, n1, s2, n2;
        fifo_.prepareToRead(fifo_.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) fn(msgs_[static_cast<std::size_t>(s1 + i)]);
        for (int i = 0; i < n2; ++i) fn(msgs_[static_cast<std::size_t>(s2 + i)]);
        fifo_.finishedRead(n1 + n2);
    }

private:
    juce::AbstractFifo fifo_{ kCapacity };
    std::array<juce::MidiMessage, kCapacity> msgs_;
};

// ---------------------------------------------------------------------------
// ProbeComponent
// ---------------------------------------------------------------------------
class ProbeComponent : public juce::Component,
                       public juce::MidiInputCallback,
                       public juce::Timer
{
public:
    ProbeComponent()
    {
        addAndMakeVisible(statusLabel_);
        statusLabel_.setFont(juce::Font(juce::FontOptions(13.0f)));
        statusLabel_.setText("Not connected", juce::dontSendNotification);

        addAndMakeVisible(inBox_);
        addAndMakeVisible(outBox_);
        inBox_.setTextWhenNothingSelected("MIDI In...");
        outBox_.setTextWhenNothingSelected("MIDI Out...");

        addAndMakeVisible(log_);
        log_.setMultiLine(true);
        log_.setReadOnly(true);
        log_.setScrollbarsShown(true);
        log_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, 0)));

        // Connection row
        makeBtn(refreshBtn_, "Refresh");
        makeBtn(openBtn_,    "Open");
        makeBtn(clearBtn_,   "Clear Log");
        refreshBtn_.onClick = [this] { refreshDeviceList(); };
        openBtn_   .onClick = [this] { openDevice(); };
        clearBtn_  .onClick = [this] { log_.clear(); };

        // Mode / identity row. NOTE: mode must MATCH the chosen port —
        // Live Port -> Live mode (0); User Port -> User mode (1). Open auto-sends
        // the matching mode; these are manual overrides.
        makeBtn(identBtn_,    "Identity Request");
        makeBtn(liveOnBtn_,   "Live mode (Live Port)");
        makeBtn(userOnBtn_,   "User mode (User Port)");
        makeBtn(allOffBtn_,   "All Lights Off");
        identBtn_ .onClick = [this] { sendIdentityRequest(); };
        liveOnBtn_.onClick = [this] { sendModeChange(0); };
        userOnBtn_.onClick = [this] { sendModeChange(1); };
        allOffBtn_.onClick = [this] { allLightsOff(); };

        // Sweep row
        makeBtn(padIdentBtn_, "Pad Ident");
        makeBtn(padPalBtn_,   "Pad Palette");
        makeBtn(lowerBtn_,    "Lower RGB");
        makeBtn(biColorBtn_,  "Bi-colour (upper+scene)");
        makeBtn(monoBtn_,     "Mono buttons");
        makeBtn(perBtnBtn_,   "Per-button verify");
        padIdentBtn_.onClick = [this] { startSweep(SweepMode::PadIdent); };
        padPalBtn_  .onClick = [this] { startSweep(SweepMode::PadPalette); };
        lowerBtn_   .onClick = [this] { startSweep(SweepMode::LowerRGB); };
        biColorBtn_ .onClick = [this] { startSweep(SweepMode::BiColor); };
        monoBtn_    .onClick = [this] { startSweep(SweepMode::Mono); };
        perBtnBtn_  .onClick = [this] { startSweep(SweepMode::PerButton); };

        // Touch-strip row. The strip's MODE governs both its input behaviour and
        // who owns its LEDs (CUSTOM_* = host owns LEDs; non-custom = device
        // self-lights). Default at power-on is PITCHBEND (5): springy, self-lit,
        // ignores host LED writes.
        addAndMakeVisible(stripModeBox_);
        // Modes 0-3 (CUSTOM_*) are the DrivenByMoss-proven Push 1 ribbon modes:
        // the device RENDERS the position you send via pitch-bend output, styled
        // by the mode. Drive them with "Strip value", NOT the flaky 0x64 path.
        const char* stripModes[] = { "0 CUSTOM_PITCHBEND (Push1)", "1 CUSTOM_VOLUME (Push1)",
            "2 CUSTOM_PAN (Push1)", "3 CUSTOM_DISCRETE (Push1)", "4 CUSTOM_FREE",
            "5 PITCHBEND (power-on)", "6 VOLUME", "7 PAN", "8 DISCRETE", "9 MODWHEEL" };
        for (int i = 0; i < 10; ++i) stripModeBox_.addItem(juce::String("Strip mode ") + stripModes[i], i + 1);
        stripModeBox_.setSelectedId(4, juce::dontSendNotification);   // CUSTOM_DISCRETE
        stripMode_ = 3;
        stripModeBox_.onChange = [this] {
            stripMode_ = stripModeBox_.getSelectedId() - 1;
            setStripMode(stripMode_);
            appendLog(">>> Strip mode -> " + juce::String(stripMode_)
                      + (stripMode_ <= 3 ? " (Push 1: drive the LEDs with 'Strip value')"
                                         : " (Push 2+ / device self-lights)"));
        };
        // "Strip value" (pitch-bend to device) is the reliable Push 1 method;
        // the 0x64 segment fill is the Push-2-style path and is flaky on Push 1.
        makeBtn(stripValBtn_, "Strip value (pitch-bend) [reliable]");
        makeBtn(stripBtn_,    "Strip 0x64 fill [Push2-style, flaky]");
        stripValBtn_.onClick = [this] { startSweep(SweepMode::StripValue); };
        stripBtn_   .onClick = [this] { startSweep(SweepMode::StripLED); };

        // Display row
        addAndMakeVisible(displayEntry_);
        displayEntry_.setText("Lockstep Push Probe", juce::dontSendNotification);
        displayEntry_.setTextToShowWhenEmpty("text for display line 1...", juce::Colours::grey);
        makeBtn(dispWriteBtn_, "Write display");
        makeBtn(dispRulerBtn_, "Ruler");
        makeBtn(dispClearBtn_, "Clear display");
        dispWriteBtn_.onClick = [this] { writeDisplayDemo(); };
        dispRulerBtn_.onClick = [this] { writeDisplayRuler(); };
        dispClearBtn_.onClick = [this] { clearDisplay(); };

        // Push 1 refinement test row: palette capture, display glyphs, animation.
        makeBtn(palGrid0Btn_,  "Palette 0-63");
        makeBtn(palGrid1Btn_,  "Palette 64-127");
        makeBtn(glyphBtn_,     "Glyph/Bars");
        makeBtn(blinkBtn_,     "Blink/Pulse");
        makeBtn(blinkStopBtn_, "Stop anim");
        palGrid0Btn_ .onClick = [this] { paletteGrid(0); };
        palGrid1Btn_ .onClick = [this] { paletteGrid(1); };
        glyphBtn_    .onClick = [this] { glyphBarsTest(); };
        blinkBtn_    .onClick = [this] { blinkPulseTest(); };
        blinkStopBtn_.onClick = [this] { stopAnim(); };

        // Paint toggle
        addAndMakeVisible(paintToggle_);
        paintToggle_.setButtonText("Paint / round-trip mode (press a control -> it lights)");
        paintToggle_.onClick = [this] {
            paintMode_ = paintToggle_.getToggleState();
            if (paintMode_) { sweepMode_ = SweepMode::Idle; appendLog("--- Paint mode ON ---"); }
            else appendLog("--- Paint mode OFF ---");
        };

        refreshDeviceList();
        startTimerHz(20);
    }

    ~ProbeComponent() override
    {
        stopTimer();
        if (midiIn_) midiIn_->stop();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(6);
        statusLabel_.setBounds(area.removeFromTop(22));
        area.removeFromTop(3);

        auto rowConn = area.removeFromTop(26);
        inBox_     .setBounds(rowConn.removeFromLeft(260).reduced(2, 0));
        outBox_    .setBounds(rowConn.removeFromLeft(260).reduced(2, 0));
        refreshBtn_.setBounds(rowConn.removeFromLeft(90).reduced(2, 0));
        openBtn_   .setBounds(rowConn.removeFromLeft(90).reduced(2, 0));
        clearBtn_  .setBounds(rowConn.reduced(2, 0));
        area.removeFromTop(3);

        layoutRow(area, { &identBtn_, &liveOnBtn_, &userOnBtn_, &allOffBtn_ });
        layoutRow(area, { &padIdentBtn_, &padPalBtn_, &lowerBtn_, &biColorBtn_,
                          &monoBtn_, &perBtnBtn_ });

        auto rowStrip = area.removeFromTop(26);
        stripModeBox_.setBounds(rowStrip.removeFromLeft(230).reduced(2, 0));
        stripValBtn_.setBounds(rowStrip.removeFromLeft(240).reduced(2, 0));
        stripBtn_   .setBounds(rowStrip.removeFromLeft(240).reduced(2, 0));
        area.removeFromTop(3);

        auto rowDisp = area.removeFromTop(26);
        displayEntry_.setBounds(rowDisp.removeFromLeft(rowDisp.getWidth() - 330).reduced(2, 0));
        dispWriteBtn_.setBounds(rowDisp.removeFromLeft(110).reduced(2, 0));
        dispRulerBtn_.setBounds(rowDisp.removeFromLeft(70).reduced(2, 0));
        dispClearBtn_.setBounds(rowDisp.reduced(2, 0));
        area.removeFromTop(3);

        layoutRow(area, { &palGrid0Btn_, &palGrid1Btn_, &glyphBtn_,
                          &blinkBtn_, &blinkStopBtn_ });

        paintToggle_.setBounds(area.removeFromTop(24));
        area.removeFromTop(3);

        log_.setBounds(area);
    }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override
    {
        queue_.push(msg);
    }

    void timerCallback() override
    {
        queue_.drain([this](const juce::MidiMessage& msg) { processIncoming(msg); });
        advanceSweep();
    }

private:
    void makeBtn(juce::TextButton& b, const char* label)
    {
        addAndMakeVisible(b);
        b.setButtonText(label);
    }

    void layoutRow(juce::Rectangle<int>& area, std::initializer_list<juce::TextButton*> btns)
    {
        auto row = area.removeFromTop(26);
        const int w = row.getWidth() / static_cast<int>(btns.size());
        for (auto* b : btns) b->setBounds(row.removeFromLeft(w).reduced(2, 0));
        area.removeFromTop(3);
    }

    // -----------------------------------------------------------------------
    // Device handling
    // -----------------------------------------------------------------------
    void refreshDeviceList()
    {
        ins_  = juce::MidiInput::getAvailableDevices();
        outs_ = juce::MidiOutput::getAvailableDevices();
        inBox_.clear(juce::dontSendNotification);
        outBox_.clear(juce::dontSendNotification);

        int inGuess = 0, outGuess = 0;
        for (int i = 0; i < ins_.size(); ++i)
        {
            inBox_.addItem(ins_[i].name, i + 1);
            if (ins_[i].name.containsIgnoreCase("Push")) inGuess = i + 1;
        }
        for (int i = 0; i < outs_.size(); ++i)
        {
            outBox_.addItem(outs_[i].name, i + 1);
            if (outs_[i].name.containsIgnoreCase("Push")) outGuess = i + 1;
        }
        if (inGuess  > 0) inBox_ .setSelectedId(inGuess,  juce::dontSendNotification);
        if (outGuess > 0) outBox_.setSelectedId(outGuess, juce::dontSendNotification);

        statusLabel_.setText("Found " + juce::String(ins_.size()) + " in / "
                             + juce::String(outs_.size())
                             + " out. Pick the Push USER PORT (port 2 / 'User Port' / MIDIIN2), then Open.",
                             juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    }

    void openDevice()
    {
        if (midiIn_) { midiIn_->stop(); midiIn_.reset(); }
        midiOut_.reset();

        const int inId  = inBox_.getSelectedId();
        const int outId = outBox_.getSelectedId();
        if (inId > 0 && inId <= ins_.size())
        {
            midiIn_ = juce::MidiInput::openDevice(ins_[inId - 1].identifier, this);
            if (midiIn_) midiIn_->start();
        }
        if (outId > 0 && outId <= outs_.size())
            midiOut_ = juce::MidiOutput::openDevice(outs_[outId - 1].identifier);

        if (midiIn_ && midiOut_)
        {
            const juce::String outName = outId > 0 ? outs_[outId - 1].name : juce::String();
            const juce::String inName  = inId  > 0 ? ins_[inId - 1].name   : juce::String();
            statusLabel_.setText("Connected. Mode auto-set to match the port; run Identity to confirm the unit.",
                                 juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
            appendLog("--- Opened in=" + (inName.isNotEmpty() ? inName : juce::String("?"))
                      + "  out=" + (outName.isNotEmpty() ? outName : juce::String("?")) + " ---");

            // Mode must match the port. The User Port wants User mode (1); the Live
            // Port wants Live mode (0). Auto-send the match so pads work regardless
            // of the device's prior state. Heuristic: port name contains "user".
            const bool isUserPort = outName.containsIgnoreCase("user")
                                 || inName.containsIgnoreCase("user");
            sendModeChange(isUserPort ? 1 : 0);
            appendLog(juce::String("    (auto-matched ") + (isUserPort ? "User" : "Live")
                      + " mode to the selected port; use the mode buttons to override)");
        }
        else
        {
            statusLabel_.setText("Could not open both ports — select In and Out and retry.",
                                 juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colours::orangered);
        }
    }

    // -----------------------------------------------------------------------
    // Output primitives
    // -----------------------------------------------------------------------
    void sendSysEx(const std::vector<juce::uint8>& inner)
    {
        if (!midiOut_) return;
        midiOut_->sendMessageNow(
            juce::MidiMessage::createSysExMessage(inner.data(), static_cast<int>(inner.size())));
    }

    void setPad(int note, int paletteIdx)
    {
        if (midiOut_)
            midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1, note, (juce::uint8)(paletteIdx & 0x7F)));
    }

    void setCC(int cc, int value)
    {
        if (midiOut_)
            midiOut_->sendMessageNow(juce::MidiMessage::controllerEvent(1, cc, value & 0x7F));
    }

    void sendIdentityRequest()
    {
        sendSysEx({ 0x7E, 0x00, 0x06, 0x01 });
        appendLog(">>> Identity Request sent (F0 7E 00 06 01 F7) — expect a Push 1 reply below.");
    }

    void sendModeChange(int mode /* 0 = Live, 1 = User */)
    {
        sendSysEx({ 0x47, 0x7F, 0x15, 0x62, 0x00, 0x01, (juce::uint8)mode });
        appendLog(">>> MODE_CHANGE byte=" + juce::String(mode) + " -> "
                  + juce::String(mode == 0 ? "Live mode (match the Live Port)"
                                           : "User mode (match the User Port)"));
    }

    // Touch-strip mode. 0-4 = CUSTOM_* (host owns LEDs); 5-9 = device self-lights
    // (5 PITCHBEND default, 6 VOLUME, 7 PAN, 8 DISCRETE, 9 MODWHEEL).
    void setStripMode(int mode)
    {
        sendSysEx({ 0x47, 0x7F, 0x15, 0x63, 0x00, 0x01, (juce::uint8)mode });
    }

    void writeDisplayLine(int line /* 0-3 */, const juce::String& text)
    {
        std::vector<juce::uint8> v { 0x47, 0x7F, 0x15,
                                     (juce::uint8)(0x18 + line), 0x00, 0x45, 0x00 };
        for (int i = 0; i < 68; ++i)
        {
            int c = (i < text.length()) ? text[i] : ' ';
            if (c < 32 || c > 126) c = ' ';
            v.push_back((juce::uint8)c);
        }
        sendSysEx(v);
    }

    void clearDisplayLine(int line)
    {
        sendSysEx({ 0x47, 0x7F, 0x15, (juce::uint8)(0x1C + line), 0x00, 0x00 });
    }

    void clearDisplay()
    {
        for (int l = 0; l < 4; ++l) clearDisplayLine(l);
        appendLog(">>> Display cleared.");
    }

    void writeDisplayDemo()
    {
        writeDisplayLine(0, displayEntry_.getText());
        writeDisplayLine(1, "Line 2: 0123456789 abcdefghijklmnopqrstuvwxyz");
        writeDisplayLine(2, "Line 3: confirm 68 chars + 4x17 blocks below");
        writeDisplayLine(3, "Bottom line (line 4).");
        appendLog(">>> Wrote 4 display lines.");
    }

    void writeDisplayRuler()
    {
        // Block diagnostic: each of the four 17-char blocks filled with its own
        // digit (1111…2222…3333…4444…). If the device shows four clean runs the
        // line is a linear 68-char buffer; scrambling reveals column reordering.
        juce::String r;
        for (int i = 0; i < 68; ++i) r += (juce::juce_wchar)('1' + (i / 17));
        writeDisplayLine(0, r);
        // Line 2: a position ruler 0-9 repeating, to read exact column indices.
        juce::String r2;
        for (int i = 0; i < 68; ++i) r2 += (juce::juce_wchar)('0' + (i % 10));
        writeDisplayLine(1, r2);
        writeDisplayLine(2, "L3 ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz");
        writeDisplayLine(3, "L4 ....+....1....+....2....+....3....+....4....+....5....+....6....+..7");
        appendLog(">>> Ruler: L1 = four 17-char blocks (1111..2222..3333..4444..); "
                  "L2 = column index mod 10. Confirm both render in order.");
    }

    // -----------------------------------------------------------------------
    // Push 1 refinement tests (colour capture / display glyphs / animation)
    // -----------------------------------------------------------------------

    // Raw display write that PASSES low control codes (1-6) through, unlike
    // writeDisplayLine which coerces them to space. Needed to test the block-bar
    // glyphs pushbase uses (GRAPH_VOL/PAN/SIN = \x03-\x06).
    void writeDisplayLineRaw(int line, const std::vector<juce::uint8>& chars68)
    {
        std::vector<juce::uint8> v { 0x47, 0x7F, 0x15,
                                     (juce::uint8)(0x18 + line), 0x00, 0x45, 0x00 };
        for (int i = 0; i < 68; ++i)
            v.push_back(i < (int)chars68.size() ? (juce::uint8)(chars68[(std::size_t)i] & 0x7F)
                                                : (juce::uint8)' ');
        sendSysEx(v);
    }

    // Palette capture: light all 64 pads with a fixed index page so a single
    // photo maps pad -> index -> *displayed* colour (note 36+i -> index page*64+i).
    // Logs a top-row-first legend that can be read straight off the photo.
    void paletteGrid(int page)
    {
        if (!midiOut_) { appendLog("No output — open device first."); return; }
        sweepMode_ = SweepMode::Idle;
        paintMode_ = false;
        paintToggle_.setToggleState(false, juce::dontSendNotification);

        const int base = page * 64;
        for (int n = 36; n <= 99; ++n)
            setPad(n, (base + (n - 36)) & 0x7F);

        appendLog("--- Palette grid page " + juce::String(page) + " (indices "
                  + juce::String(base) + "-" + juce::String(base + 63)
                  + "); photograph and read each cell's hex ---");
        for (int row = 7; row >= 0; --row)
        {
            juce::String l = "  ";
            for (int col = 0; col < 8; ++col)
                l += juce::String(base + row * 8 + col).paddedLeft(' ', 4);
            appendLog(l);
        }
        appendLog("  ^ grid as seen (top row first = pad note 92..99).");
    }

    // Reveal which low control codes render as bar segments (pushbase uses
    // \x03-\x06), and compare an ASCII fallback bar.
    void glyphBarsTest()
    {
        if (!midiOut_) { appendLog("No output — open device first."); return; }
        sweepMode_ = SweepMode::Idle;

        // L1: codes 0x01..0x1F once each — eyeball which are bar glyphs.
        std::vector<juce::uint8> l0;
        for (int c = 1; c <= 0x1F; ++c) l0.push_back((juce::uint8)c);
        writeDisplayLineRaw(0, l0);

        // L2: partial-block ramp using the pushbase set 03 04 05 06, repeated.
        std::vector<juce::uint8> l1;
        for (int i = 0; i < 17; ++i)
        { l1.push_back(0x03); l1.push_back(0x04); l1.push_back(0x05); l1.push_back(0x06); }
        writeDisplayLineRaw(1, l1);

        // L3: eight 8-char cells, cell i filled i/8 with code 0x06 + spaces.
        std::vector<juce::uint8> l2;
        for (int cell = 0; cell < 8; ++cell)
            for (int k = 0; k < 8; ++k) l2.push_back((juce::uint8)(k < cell ? 0x06 : ' '));
        writeDisplayLineRaw(2, l2);

        // L4: ASCII fallback bar for comparison.
        writeDisplayLine(3, "L4 ASCII: [#####   ][##      ][####    ][####### ]");
        appendLog(">>> Glyph/Bars: L1=codes 01-1F, L2=ramp 03 04 05 06, "
                  "L3=0..7/8 fill with code 06, L4=ASCII. Note which codes draw bars.");
    }

    // Animation test: a pad shows colour A (ch1) and colour B (2nd channel); the
    // device animates A<->B. pushbase: Pulse(fade) ch6-10, Blink(toggle) ch11-15
    // (0-based status); DrivenByMoss uses ch10/14. The 0/1-based convention is
    // unconfirmed, so drive MIDI channels 2..16 on notes 36..50 and observe.
    void blinkPulseTest()
    {
        if (!midiOut_) { appendLog("No output — open device first."); return; }
        sweepMode_ = SweepMode::Idle;
        const int aColour = 5;    // red hi
        const int bColour = 21;   // green hi — contrast so fade vs toggle is obvious
        appendLog("--- Blink/Pulse: notes 36..50, A=red(ch1) + B=green(ch 2..16) ---");
        for (int ch = 2; ch <= 16; ++ch)
        {
            const int note = 36 + (ch - 2);
            midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1,  note, (juce::uint8)aColour));
            midiOut_->sendMessageNow(juce::MidiMessage::noteOn(ch, note, (juce::uint8)bColour));
            appendLog("  note " + juce::String(note) + " : B on MIDI ch " + juce::String(ch));
        }
        appendLog("  Watch which pads fade (pulse) vs hard-toggle (blink) and the "
                  "rate per channel; then 'Stop anim' to learn how to cancel.");
    }

    // Cancel test: re-assert a steady colour on both the animation channel and
    // ch1, to learn which write stops the animation.
    void stopAnim()
    {
        if (!midiOut_) return;
        for (int ch = 2; ch <= 16; ++ch)
        {
            const int note = 36 + (ch - 2);
            midiOut_->sendMessageNow(juce::MidiMessage::noteOn(ch, note, (juce::uint8)5));
            midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1,  note, (juce::uint8)5));
        }
        appendLog(">>> Stop anim: steady colour re-sent on both anim-channel and ch1. "
                  "Note whether pads stop, and which write did it.");
    }

    // Strip LED: 24 segments, value per segment 0=off, 1=half, 3=full.
    void sendStripLeds(const std::array<int, 24>& seg)
    {
        std::vector<juce::uint8> v { 0x47, 0x7F, 0x15, 0x64, 0x00, 0x08 };
        for (int g = 0; g < 8; ++g)
        {
            int byte = 0;
            for (int k = 0; k < 3; ++k)
                byte |= (seg[static_cast<std::size_t>(g * 3 + k)] & 0x3) << (2 * k);
            v.push_back((juce::uint8)byte);
        }
        sendSysEx(v);
    }

    void allLightsOff()
    {
        sweepMode_ = SweepMode::Idle;
        if (!midiOut_) return;
        for (int n = 36; n <= 99; ++n) setPad(n, 0);
        for (const auto& e : kButtonCCs) setCC(e.cc, 0);
        sendStripLeds({});
        clearDisplay();
        appendLog("--- All lights off ---");
    }

    // -----------------------------------------------------------------------
    // Incoming message processing
    // -----------------------------------------------------------------------
    void processIncoming(const juce::MidiMessage& msg)
    {
        logMessage(msg);
        if (sweepMode_ == SweepMode::PerButton) checkPerButton(msg);
        if (paintMode_) handlePaint(msg);
    }

    void logMessage(const juce::MidiMessage& msg)
    {
        juce::String s;
        const int ch = msg.getChannel();

        if (msg.isNoteOn())
        {
            const int n = msg.getNoteNumber();
            s << "NoteOn  ch=" << ch << " note=" << n << " vel=" << msg.getVelocity()
              << "  [" << annotateNote(n) << "]";
        }
        else if (msg.isNoteOff())
        {
            const int n = msg.getNoteNumber();
            s << "NoteOff ch=" << ch << " note=" << n << "  [" << annotateNote(n) << "]";
        }
        else if (msg.isController())
        {
            const int cc  = msg.getControllerNumber();
            const int val = msg.getControllerValue();
            s << "CC      ch=" << ch << " cc=" << cc << " val=" << val
              << "  [" << annotateCC(cc) << "]";
            if ((cc >= 71 && cc <= 79) || cc == 14 || cc == 15)
            {
                const int delta = (val <= 63) ? val : -(128 - val);
                s << "  delta=" << (delta >= 0 ? "+" : "") << delta;
            }
        }
        else if (msg.isPitchWheel())
        {
            s << "PitchBend ch=" << ch << " val=" << msg.getPitchWheelValue() << "  [TOUCH STRIP]";
        }
        else if (msg.isAftertouch())
        {
            s << "PolyAT  ch=" << ch << " note=" << msg.getNoteNumber()
              << " val=" << msg.getAfterTouchValue() << "  [pad pressure]";
        }
        else if (msg.isChannelPressure())
        {
            s << "ChanAT  ch=" << ch << " val=" << msg.getChannelPressureValue() << "  [pad pressure]";
        }
        else if (msg.isSysEx())
        {
            s << "SysEx   " << sysexToHex(msg) << identityNote(msg);
        }
        else
        {
            s << "Other: " << msg.getDescription();
        }
        appendLog(s);
    }

    static juce::String sysexToHex(const juce::MidiMessage& msg)
    {
        const juce::uint8* d = msg.getSysExData();
        const int n = msg.getSysExDataSize();
        juce::String h = "F0";
        for (int i = 0; i < n; ++i)
            h += " " + juce::String::toHexString(d[i]).paddedLeft('0', 2).toUpperCase();
        h += " F7";
        return h;
    }

    // Identity reply prefix (after F0): 7E xx 06 02 47 15  → Akai/Ableton Push.
    static juce::String identityNote(const juce::MidiMessage& msg)
    {
        const juce::uint8* d = msg.getSysExData();
        const int n = msg.getSysExDataSize();
        if (n >= 6 && d[0] == 0x7E && d[2] == 0x06 && d[3] == 0x02 && d[4] == 0x47 && d[5] == 0x15)
            return "  [<< PUSH 1 IDENTITY confirmed]";
        return {};
    }

    void appendLog(const juce::String& s)
    {
        log_.moveCaretToEnd();
        log_.insertTextAtCaret(s + "\n");
    }

    // -----------------------------------------------------------------------
    // Paint / round-trip mode
    // -----------------------------------------------------------------------
    void handlePaint(const juce::MidiMessage& msg)
    {
        if (msg.isNoteOn() && msg.getVelocity() > 0)
        {
            const int n = msg.getNoteNumber();
            if (n >= 36 && n <= 99)
            {
                int& idx = padIdx_[static_cast<std::size_t>(n)];
                idx = (idx + 5) % 128;
                if (idx == 0) idx = 1;
                setPad(n, idx);
            }
        }
        else if (msg.isController() && msg.getControllerValue() == 127)
        {
            const int cc = msg.getControllerNumber();
            if (const auto* e = findCC(cc)) cyclePaintCC(*e);
        }
        else if (msg.isPitchWheel())
        {
            // Light the strip the reliable Push 1 way: echo the touched position
            // back as pitch-bend output (DrivenByMoss method). Needs a CUSTOM
            // strip mode (0-3); the device renders it per the selected mode.
            if (midiOut_)
                midiOut_->sendMessageNow(juce::MidiMessage::pitchWheel(1, msg.getPitchWheelValue()));
        }
    }

    void cyclePaintCC(const CCEntry& e)
    {
        int& v = ccVal_[static_cast<std::size_t>(e.cc)];
        switch (e.space)
        {
            case Space::Mono:     v = (v == 0) ? 1 : (v == 1) ? 4 : 0; break;
            case Space::BiColor:  v = nextBi(v); break;
            case Space::PadRGB:
            case Space::LowerRGB: v = (v + 7) % 128; if (v == 0) v = 1; break;
        }
        setCC(e.cc, v);
    }

    // Cycle through a few representative bi-colour values (off/red/orange/yellow/green hi).
    static int nextBi(int v)
    {
        static const int seq[] = { 0, 4, 10, 16, 22 };
        for (int i = 0; i < 5; ++i) if (seq[i] == v) return seq[(i + 1) % 5];
        return seq[1];
    }

    // -----------------------------------------------------------------------
    // Sweeps
    // -----------------------------------------------------------------------
    enum class SweepMode { Idle, PadIdent, PadPalette, LowerRGB, BiColor, Mono,
                           StripLED, StripValue, PerButton };

    void startSweep(SweepMode m)
    {
        if (!midiOut_) { appendLog("No output — open device first."); return; }
        paintMode_ = false;
        paintToggle_.setToggleState(false, juce::dontSendNotification);

        // Strip sweeps: don't flood the device with a full all-lights-off (that
        // races the mode change and is the likely cause of the "occasionally
        // works" 0x64 behaviour). Instead re-assert the strip mode and let it
        // settle before the first write.
        const bool stripSweep = (m == SweepMode::StripLED || m == SweepMode::StripValue);
        if (stripSweep)
        {
            // 0x64 needs a CUSTOM mode (0-4); the pitch-bend method uses whatever
            // mode is selected (CUSTOM 0-3 render the value on Push 1).
            const int mode = (m == SweepMode::StripLED) ? (stripMode_ <= 4 ? stripMode_ : 0)
                                                        : stripMode_;
            setStripMode(mode);
        }
        else
            allLightsOff();

        sweepMode_   = m;
        sweepStep_   = 0;
        sweepNextMs_ = juce::Time::getMillisecondCounter() + (stripSweep ? 250 : 0);  // settle after mode set
        switch (m)
        {
            case SweepMode::PadIdent:  appendLog("--- Pad ident: each pad lit in turn; confirm note 36 = bottom-left ---"); break;
            case SweepMode::PadPalette:appendLog("--- Pad palette: whole grid stepped through index 0-127 ---"); break;
            case SweepMode::LowerRGB:  appendLog("--- Lower row (CC 102-109): palette 0-127 ---"); break;
            case SweepMode::BiColor:   appendLog("--- Bi-colour (upper 20-27 + scenes 36-43): value 0-24 ---"); break;
            case SweepMode::Mono:      appendLog("--- Mono buttons: value 0/1/2/3/4/5/6 (off/dim/+blink/bright/+blink) ---"); break;
            case SweepMode::StripLED:
                appendLog("--- Strip 0x64 fill in mode " + juce::String(stripMode_ <= 4 ? stripMode_ : 0)
                          + ": rising fill 0-24 (Push2-style; flaky on Push 1) ---");
                break;
            case SweepMode::StripValue:
                appendLog("--- Strip value: pitch-bend 0->16383 to device in mode " + juce::String(stripMode_)
                          + " (DrivenByMoss method — reliable on Push 1 in CUSTOM modes 0-3) ---");
                break;
            case SweepMode::PerButton: appendLog("--- Per-button verify: each lit button waits ~6s for its press ---"); break;
            case SweepMode::Idle:      break;
        }
    }

    void advanceSweep()
    {
        if (sweepMode_ == SweepMode::Idle || !midiOut_) return;
        const juce::uint32 now = juce::Time::getMillisecondCounter();

        if (sweepMode_ == SweepMode::PerButton) { advancePerButton(now); return; }
        if (now < sweepNextMs_) return;

        switch (sweepMode_)
        {
            case SweepMode::PadIdent:
            {
                if (sweepStep_ > 0) setPad(36 + sweepStep_ - 1, 0);
                if (sweepStep_ < 64)
                {
                    const int note = 36 + sweepStep_;
                    setPad(note, 22);   // green-hi
                    appendLog(">>> pad note=" + juce::String(note) + "  [" + annotateNote(note) + "]");
                    sweepNextMs_ = now + 500;
                    ++sweepStep_;
                }
                else finishSweep("Pad ident");
                break;
            }
            case SweepMode::PadPalette:
            {
                if (sweepStep_ < 128)
                {
                    for (int n = 36; n <= 99; ++n) setPad(n, sweepStep_);
                    appendLog("  palette index = " + juce::String(sweepStep_));
                    sweepNextMs_ = now + 450;
                    ++sweepStep_;
                }
                else finishSweep("Pad palette");
                break;
            }
            case SweepMode::LowerRGB:
            {
                if (sweepStep_ < 128)
                {
                    for (int cc = 102; cc <= 109; ++cc) setCC(cc, sweepStep_);
                    appendLog("  lower-row palette index = " + juce::String(sweepStep_));
                    sweepNextMs_ = now + 400;
                    ++sweepStep_;
                }
                else finishSweep("Lower RGB");
                break;
            }
            case SweepMode::BiColor:
            {
                if (sweepStep_ <= 24)
                {
                    for (int cc = 20; cc <= 27; ++cc) setCC(cc, sweepStep_);
                    for (int cc = 36; cc <= 43; ++cc) setCC(cc, sweepStep_);
                    appendLog("  bi-colour value = " + juce::String(sweepStep_));
                    sweepNextMs_ = now + 600;
                    ++sweepStep_;
                }
                else finishSweep("Bi-colour");
                break;
            }
            case SweepMode::Mono:
            {
                static const char* meaning[] = { "off", "dim", "dim slow-blink",
                    "dim fast-blink", "bright", "bright slow-blink", "bright fast-blink" };
                if (sweepStep_ < 7)
                {
                    for (const auto& e : kButtonCCs)
                        if (e.space == Space::Mono) setCC(e.cc, sweepStep_);
                    appendLog("  mono value = " + juce::String(sweepStep_) + " (" + meaning[sweepStep_] + ")");
                    sweepNextMs_ = now + 900;
                    ++sweepStep_;
                }
                else finishSweep("Mono buttons");
                break;
            }
            case SweepMode::StripLED:
            {
                if (sweepStep_ <= 24)
                {
                    std::array<int, 24> seg{};
                    for (int i = 0; i < sweepStep_; ++i) seg[static_cast<std::size_t>(i)] = 3;
                    sendStripLeds(seg);
                    sweepNextMs_ = now + 150;
                    ++sweepStep_;
                }
                else { sendStripLeds({}); finishSweep("Strip LEDs"); }
                break;
            }
            case SweepMode::StripValue:
            {
                if (sweepStep_ <= 16)
                {
                    const int pb = juce::jlimit(0, 16383, sweepStep_ * 1024);
                    if (midiOut_) midiOut_->sendMessageNow(juce::MidiMessage::pitchWheel(1, pb));
                    appendLog("  strip pitch-bend value = " + juce::String(pb));
                    sweepNextMs_ = now + 220;
                    ++sweepStep_;
                }
                else
                {
                    if (midiOut_) midiOut_->sendMessageNow(juce::MidiMessage::pitchWheel(1, 0));
                    finishSweep("Strip value");
                }
                break;
            }
            case SweepMode::PerButton:   // handled before the switch
            case SweepMode::Idle:
                break;
        }
    }

    void finishSweep(const char* name)
    {
        appendLog(juce::String("--- ") + name + " done ---");
        sweepMode_ = SweepMode::Idle;
    }

    // Per-button verify: light one button, wait for its press, PASS/skip, advance.
    void advancePerButton(juce::uint32 now)
    {
        if (sweepStep_ >= kNumCCs) { finishSweep("Per-button verify"); return; }

        if (!perBtnArmed_)
        {
            const auto& e = kButtonCCs[static_cast<std::size_t>(sweepStep_)];
            lightButtonOn(e);
            perBtnArmed_   = true;
            perBtnPressed_ = false;
            sweepNextMs_   = now + 6000;
            appendLog(">>> press: " + juce::String(e.label) + " (cc " + juce::String(e.cc) + ")");
            return;
        }

        if (perBtnPressed_ || now >= sweepNextMs_)
        {
            const auto& e = kButtonCCs[static_cast<std::size_t>(sweepStep_)];
            setCC(e.cc, 0);
            appendLog(perBtnPressed_ ? juce::String("    PASS")
                                     : juce::String("    (no press — skipped)"));
            perBtnArmed_ = false;
            ++sweepStep_;
        }
    }

    void checkPerButton(const juce::MidiMessage& msg)
    {
        if (!perBtnArmed_ || sweepStep_ >= kNumCCs) return;
        if (msg.isController() && msg.getControllerValue() == 127
            && msg.getControllerNumber() == kButtonCCs[static_cast<std::size_t>(sweepStep_)].cc)
            perBtnPressed_ = true;
    }

    void lightButtonOn(const CCEntry& e)
    {
        switch (e.space)
        {
            case Space::Mono:     setCC(e.cc, 4);   break;   // bright
            case Space::BiColor:  setCC(e.cc, 22);  break;   // green hi
            case Space::LowerRGB: setCC(e.cc, 22);  break;   // green palette
            case Space::PadRGB:   break;
        }
    }

    // -----------------------------------------------------------------------
    juce::Label      statusLabel_;
    juce::ComboBox   inBox_, outBox_;
    juce::TextEditor log_;
    juce::TextButton refreshBtn_, openBtn_, clearBtn_;
    juce::TextButton identBtn_, userOnBtn_, liveOnBtn_, allOffBtn_;
    juce::TextButton padIdentBtn_, padPalBtn_, lowerBtn_, biColorBtn_, monoBtn_, perBtnBtn_;
    juce::ComboBox   stripModeBox_;
    juce::TextButton stripBtn_, stripValBtn_;
    juce::TextEditor displayEntry_;
    juce::TextButton dispWriteBtn_, dispRulerBtn_, dispClearBtn_;
    juce::TextButton palGrid0Btn_, palGrid1Btn_, glyphBtn_, blinkBtn_, blinkStopBtn_;
    juce::ToggleButton paintToggle_;

    juce::Array<juce::MidiDeviceInfo> ins_, outs_;
    std::unique_ptr<juce::MidiInput>  midiIn_;
    std::unique_ptr<juce::MidiOutput> midiOut_;
    MidiQueue queue_;

    SweepMode    sweepMode_   = SweepMode::Idle;
    int          sweepStep_   = 0;
    juce::uint32 sweepNextMs_ = 0;

    bool paintMode_     = false;
    bool perBtnArmed_   = false;
    bool perBtnPressed_ = false;
    int  stripMode_     = 3;   // current touch-strip mode (CUSTOM_DISCRETE)

    std::array<int, 128> padIdx_{};   // paint: last colour per pad note
    std::array<int, 128> ccVal_{};    // paint: last value per CC

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProbeComponent)
};

// ---------------------------------------------------------------------------
class ProbeMainWindow : public juce::DocumentWindow
{
public:
    explicit ProbeMainWindow(const juce::String& name)
        : juce::DocumentWindow(name,
                               juce::Desktop::getInstance().getDefaultLookAndFeel()
                                   .findColour(juce::ResizableWindow::backgroundColourId),
                               DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new ProbeComponent(), true);
        setResizable(true, true);
        centreWithSize(1040, 720);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProbeMainWindow)
};

class ProbeApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return "Lockstep Push Probe"; }
    const juce::String getApplicationVersion() override { return "0.1"; }
    bool               moreThanOneInstanceAllowed() override { return true; }
    void initialise(const juce::String&) override
    {
        mainWindow_ = std::make_unique<ProbeMainWindow>(getApplicationName());
    }
    void shutdown() override { mainWindow_.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<ProbeMainWindow> mainWindow_;
};

} // namespace lockstep

START_JUCE_APPLICATION(lockstep::ProbeApp)
