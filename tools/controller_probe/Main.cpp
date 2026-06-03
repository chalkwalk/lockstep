// Throwaway probe tool — confirms X-Touch Mini (MCU) input note numbers,
// encoder sign convention, button LED behaviour, and ring rendering.
// Not wired into the plugin; build with the controller_probe CMake target.

#include <JuceHeader.h>
#include <array>

namespace lockstep
{

// ---------------------------------------------------------------------------
// Confirmed note map (from probe session 2026-06-02)
// Physical top row (near encoders) → steps 0-7: notes 40-45, 94, 95
// Physical bottom row               → steps 8-15: notes 86-93
// Layer A = 84, Layer B = 85
// Encoder push = 32-39 (no LEDs)
// ---------------------------------------------------------------------------
struct ButtonEntry
{
    int         note;
    const char* label;   // human-readable description
};

// Physical position → note, confirmed by ident sweep 2026-06-02.
// Top row = buttons 0-7 (near encoders); bottom row = buttons 8-15.
static constexpr ButtonEntry kButtons[] = {
    // Physical top row, left to right (steps 0-7)
    { 89, "TOP 0  (step 0)" }, { 90, "TOP 1  (step 1)" },
    { 40, "TOP 2  (step 2)" }, { 41, "TOP 3  (step 3)" },
    { 42, "TOP 4  (step 4)" }, { 43, "TOP 5  (step 5)" },
    { 44, "TOP 6  (step 6)" }, { 45, "TOP 7  (step 7)" },
    // Physical bottom row, left to right (steps 8-15)
    { 87, "BOT 8  (step 8)"  }, { 88, "BOT 9  (step 9)"  },
    { 91, "BOT 10 (step 10)" }, { 92, "BOT 11 (step 11)" },
    { 86, "BOT 12 (step 12)" }, { 93, "BOT 13 (step 13)" },
    { 94, "BOT 14 (step 14)" }, { 95, "BOT 15 (step 15)" },
    // Layer A / B
    { 84, "LAYER A" }, { 85, "LAYER B" },
};
static constexpr int kNumButtons = static_cast<int>(std::size(kButtons));

static const char* annotateNote(int n)
{
    for (const auto& b : kButtons)
        if (b.note == n) return b.label;
    if (n >= 32 && n <= 39) return "ENC PUSH (no LED)";
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// Thread-safe single-producer / single-consumer MIDI message queue.
// ---------------------------------------------------------------------------
class MidiQueue
{
public:
    static constexpr int kCapacity = 256;

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

        addAndMakeVisible(deviceBox_);
        deviceBox_.setFont(juce::Font(juce::FontOptions(10.0f)));
        deviceBox_.setMultiLine(true);
        deviceBox_.setReadOnly(true);
        deviceBox_.setScrollbarsShown(true);

        addAndMakeVisible(log_);
        log_.setMultiLine(true);
        log_.setReadOnly(true);
        log_.setScrollbarsShown(true);
        log_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, 0)));

        // Row 1 buttons
        makeBtn(openBtn_,        "Open X-Touch Mini");
        makeBtn(lightsOffBtn_,   "All Lights Off");
        makeBtn(clearBtn_,       "Clear Log");

        // Row 2 buttons
        makeBtn(identBtn_,       "Ident LEDs (1.5s each)");
        makeBtn(flashTestBtn_,   "Flash Test (vel=1, 3s each)");
        makeBtn(brightnessBtn_,  "Velocity Sweep (note 40)");
        makeBtn(ringsBtn_,       "Sweep All Rings");

        openBtn_      .onClick = [this] { openDevice(); };
        lightsOffBtn_ .onClick = [this] { allLightsOff(); };
        clearBtn_     .onClick = [this] { log_.clear(); };
        identBtn_     .onClick = [this] { startIdent(); };
        flashTestBtn_ .onClick = [this] { startFlashTest(); };
        brightnessBtn_.onClick = [this] { startBrightnessSweep(); };
        ringsBtn_     .onClick = [this] { startRingSweep(); };

        // Manual velocity test on note 40 (top-row button 2, step 2)
        addAndMakeVisible(velLabel_);
        velLabel_.setText("Manual vel test (note 40):", juce::dontSendNotification);

        addAndMakeVisible(velSlider_);
        velSlider_.setRange(0, 127, 1);
        velSlider_.setValue(127, juce::dontSendNotification);
        velSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        velSlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 36, 20);
        velSlider_.onValueChange = [this] {
            if (midiOut_) {
                const int vel = static_cast<int>(velSlider_.getValue());
                midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1, 40, (juce::uint8)vel));
                appendLog("Manual: note 40 vel=" + juce::String(vel));
            }
        };

        makeBtn(vel0Btn_,   "vel=0 (off)");
        makeBtn(vel1Btn_,   "vel=1 (flash?)");
        makeBtn(vel127Btn_, "vel=127 (on)");
        vel0Btn_  .onClick = [this] { velSlider_.setValue(0,   juce::sendNotification); };
        vel1Btn_  .onClick = [this] { velSlider_.setValue(1,   juce::sendNotification); };
        vel127Btn_.onClick = [this] { velSlider_.setValue(127, juce::sendNotification); };

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

        // Device list: narrow column on left
        auto top = area.removeFromTop(70);
        deviceBox_.setBounds(top);
        area.removeFromTop(3);

        // Button row 1
        {
            auto row = area.removeFromTop(26);
            const int w = row.getWidth() / 3;
            openBtn_      .setBounds(row.removeFromLeft(w).reduced(2, 0));
            lightsOffBtn_ .setBounds(row.removeFromLeft(w).reduced(2, 0));
            clearBtn_     .setBounds(row.reduced(2, 0));
        }
        area.removeFromTop(3);

        // Button row 2
        {
            auto row = area.removeFromTop(26);
            const int w = row.getWidth() / 4;
            identBtn_     .setBounds(row.removeFromLeft(w).reduced(2, 0));
            flashTestBtn_ .setBounds(row.removeFromLeft(w).reduced(2, 0));
            brightnessBtn_.setBounds(row.removeFromLeft(w).reduced(2, 0));
            ringsBtn_     .setBounds(row.reduced(2, 0));
        }
        area.removeFromTop(3);

        // Manual velocity test row
        {
            auto row = area.removeFromTop(26);
            velLabel_.setBounds(row.removeFromLeft(180).reduced(2, 0));
            vel0Btn_  .setBounds(row.removeFromLeft(80).reduced(2, 0));
            vel1Btn_  .setBounds(row.removeFromLeft(100).reduced(2, 0));
            vel127Btn_.setBounds(row.removeFromLeft(90).reduced(2, 0));
            velSlider_.setBounds(row.reduced(2, 0));
        }
        area.removeFromTop(3);

        log_.setBounds(area);
    }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override
    {
        queue_.push(msg);
    }

    void timerCallback() override
    {
        queue_.drain([this](const juce::MidiMessage& msg) { logMessage(msg); });
        advanceSweep();
    }

private:
    void makeBtn(juce::TextButton& b, const char* label)
    {
        addAndMakeVisible(b);
        b.setButtonText(label);
    }

    void refreshDeviceList()
    {
        juce::String txt = "MIDI devices:  IN: ";
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            txt += d.name + "  ";
        txt += " | OUT: ";
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            txt += d.name + "  ";
        deviceBox_.setText(txt, false);
    }

    void openDevice()
    {
        if (midiIn_) { midiIn_->stop(); midiIn_.reset(); }
        midiOut_.reset();

        juce::String foundIn, foundOut;
        for (const auto& d : juce::MidiInput::getAvailableDevices())
        {
            if (d.name.containsIgnoreCase("X-TOUCH MINI") ||
                d.name.containsIgnoreCase("X TOUCH MINI"))
            {
                midiIn_ = juce::MidiInput::openDevice(d.identifier, this);
                foundIn = d.name;
                break;
            }
        }
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
        {
            if (d.name.containsIgnoreCase("X-TOUCH MINI") ||
                d.name.containsIgnoreCase("X TOUCH MINI"))
            {
                midiOut_ = juce::MidiOutput::openDevice(d.identifier);
                foundOut = d.name;
                break;
            }
        }
        if (midiIn_) midiIn_->start();

        if (midiIn_ && midiOut_)
        {
            statusLabel_.setText("Connected: " + foundIn, juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
            appendLog("--- Opened: " + foundIn + " ---");
        }
        else if (midiIn_ || midiOut_)
        {
            statusLabel_.setText("Partial: in=" + foundIn + " out=" + foundOut,
                                 juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colours::orange);
        }
        else
        {
            statusLabel_.setText("X-Touch Mini NOT found", juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colours::orangered);
            refreshDeviceList();
        }
    }

    // -----------------------------------------------------------------------
    void logMessage(const juce::MidiMessage& msg)
    {
        juce::String s;
        const int ch = msg.getChannel();

        if (msg.isNoteOn())
        {
            const int n = msg.getNoteNumber();
            s << "NoteOn  ch=" << ch << " note=" << n
              << " vel=" << msg.getVelocity()
              << "  [" << annotateNote(n) << "]";
        }
        else if (msg.isNoteOff())
        {
            const int n = msg.getNoteNumber();
            s << "NoteOff ch=" << ch << " note=" << n
              << "  [" << annotateNote(n) << "]";
        }
        else if (msg.isController())
        {
            const int cc  = msg.getControllerNumber();
            const int val = msg.getControllerValue();
            s << "CC      ch=" << ch << " cc=" << cc << " val=" << val;
            if (cc >= 16 && cc <= 23)
            {
                const int delta = (val <= 63) ? val : -(val - 64);
                s << "  [ENC " << (cc - 16 + 1) << " delta=" << delta << "]";
            }
            else if (cc >= 48 && cc <= 55)
            {
                const char* modes[] = { "single", "boost/cut", "wrap", "spread" };
                s << "  [RING " << (cc - 48 + 1)
                  << " mode=" << modes[(val >> 4) & 0x3]
                  << " pos=" << (val & 0x0F) << "]";
            }
        }
        else if (msg.isPitchWheel())
        {
            s << "PitchBend ch=" << ch << " val=" << msg.getPitchWheelValue();
            if (ch == 9) s << "  [FADER]";
        }
        else
        {
            s << "Other: " << msg.getDescription();
        }
        appendLog(s);
    }

    void appendLog(const juce::String& s)
    {
        log_.moveCaretToEnd();
        log_.insertTextAtCaret(s + "\n");
    }

    // -----------------------------------------------------------------------
    void allLightsOff()
    {
        sweepMode_ = SweepMode::Idle;
        if (!midiOut_) return;
        for (const auto& b : kButtons)
            midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1, b.note, (juce::uint8)0));
        for (int cc = 48; cc <= 55; ++cc)
            midiOut_->sendMessageNow(juce::MidiMessage::controllerEvent(1, cc, 0));
        appendLog("--- All lights off ---");
    }

    // -----------------------------------------------------------------------
    // Ident sweep: light each button 1.5 s, one at a time.
    // Lets you verify physical position → note mapping.
    // -----------------------------------------------------------------------
    void startIdent()
    {
        if (!midiOut_) { appendLog("No output — open device first."); return; }
        allLightsOff();
        sweepMode_   = SweepMode::Ident;
        sweepStep_   = 0;
        sweepNextMs_ = juce::Time::getMillisecondCounter();
        appendLog("--- LED ident sweep: each button lit 1.5 s; check which physical button lights ---");
    }

    // -----------------------------------------------------------------------
    // Flash test: velocity 1 held 3 s per button.
    // In MCU spec, vel=1 = flash. Verify whether this device honours it.
    // -----------------------------------------------------------------------
    void startFlashTest()
    {
        if (!midiOut_) { appendLog("No output — open device first."); return; }
        allLightsOff();
        sweepMode_   = SweepMode::Flash;
        sweepStep_   = 0;
        sweepNextMs_ = juce::Time::getMillisecondCounter();
        appendLog("--- Flash test (vel=1): 3 s per button. Does the LED flash? ---");
    }

    // -----------------------------------------------------------------------
    // Velocity sweep: send note 40 (top-row button 1) at vel 0..127.
    // -----------------------------------------------------------------------
    void startBrightnessSweep()
    {
        if (!midiOut_) { appendLog("No output — open device first."); return; }
        sweepMode_   = SweepMode::Brightness;
        sweepStep_   = 0;
        sweepNextMs_ = juce::Time::getMillisecondCounter();
        appendLog("--- Velocity sweep on note 40 (top-row button 1). Does brightness change? ---");
    }

    // -----------------------------------------------------------------------
    // Ring sweep: all 4 modes x 12 positions on all 8 rings.
    // -----------------------------------------------------------------------
    void startRingSweep()
    {
        if (!midiOut_) { appendLog("No output — open device first."); return; }
        sweepMode_   = SweepMode::Rings;
        sweepStep_   = 0;
        sweepNextMs_ = juce::Time::getMillisecondCounter();
        appendLog("--- Ring sweep: 4 modes x 12 positions on all 8 rings ---");
    }

    // -----------------------------------------------------------------------
    enum class SweepMode { Idle, Ident, Flash, Brightness, Rings };
    SweepMode    sweepMode_    = SweepMode::Idle;
    int          sweepStep_    = 0;
    juce::uint32 sweepNextMs_  = 0;

    void advanceSweep()
    {
        if (sweepMode_ == SweepMode::Idle || !midiOut_) return;
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        if (now < sweepNextMs_) return;

        // ---- Ident: one button lit at 127, 1500 ms each ----
        if (sweepMode_ == SweepMode::Ident)
        {
            // Turn off the previous button
            if (sweepStep_ > 0)
                midiOut_->sendMessageNow(
                    juce::MidiMessage::noteOn(1, kButtons[sweepStep_ - 1].note, (juce::uint8)0));

            if (sweepStep_ < kNumButtons)
            {
                const auto& b = kButtons[sweepStep_];
                midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1, b.note, (juce::uint8)127));
                appendLog(">>> note=" + juce::String(b.note) + "  [" + b.label + "]  -- which button is lit?");
                sweepNextMs_ = now + 1500;
                ++sweepStep_;
            }
            else
            {
                appendLog("--- Ident sweep done ---");
                sweepMode_ = SweepMode::Idle;
            }
        }

        // ---- Flash test: velocity 1, 3000 ms each ----
        else if (sweepMode_ == SweepMode::Flash)
        {
            // Phase 0: send vel=1; phase 1: turn off after hold
            const int btn   = sweepStep_ / 2;
            const int phase = sweepStep_ % 2;

            if (btn >= kNumButtons)
            {
                appendLog("--- Flash test done ---");
                sweepMode_ = SweepMode::Idle;
                return;
            }

            if (phase == 0)
            {
                const auto& b = kButtons[btn];
                midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1, b.note, (juce::uint8)1));
                appendLog(">>> Flash vel=1: note=" + juce::String(b.note) + "  [" + b.label + "]  -- does it flash?");
                sweepNextMs_ = now + 3000;
            }
            else
            {
                midiOut_->sendMessageNow(
                    juce::MidiMessage::noteOn(1, kButtons[btn].note, (juce::uint8)0));
                sweepNextMs_ = now + 200;
            }
            ++sweepStep_;
        }

        // ---- Velocity sweep: note 40, vel 0..127 in steps of 8 ----
        else if (sweepMode_ == SweepMode::Brightness)
        {
            static const int kVels[] = { 0,1,2,4,8,12,16,24,32,40,48,56,64,72,80,88,96,104,112,120,124,126,127 };
            static constexpr int kN  = static_cast<int>(std::size(kVels));
            if (sweepStep_ < kN)
            {
                const int vel = kVels[sweepStep_];
                midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1, 40, (juce::uint8)vel));
                appendLog("  vel=" + juce::String(vel) + "  -- any change on top-row button 1?");
                sweepNextMs_ = now + 300;
                ++sweepStep_;
            }
            else
            {
                midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1, 40, (juce::uint8)0));
                appendLog("--- Velocity sweep done ---");
                sweepMode_ = SweepMode::Idle;
            }
        }

        // ---- Ring sweep: all 8 rings, 4 modes x 12 positions ----
        else if (sweepMode_ == SweepMode::Rings)
        {
            // sweepStep_ = ring(0-7) * 48 + mode(0-3) * 12 + pos(0-11)
            constexpr int kStepsPerRing = 4 * 12;
            constexpr int kTotal        = 8 * kStepsPerRing;

            if (sweepStep_ < kTotal)
            {
                const int ring = sweepStep_ / kStepsPerRing;
                const int rem  = sweepStep_ % kStepsPerRing;
                const int mode = rem / 12;
                const int pos  = rem % 12;
                const int val  = (mode << 4) | pos;
                const char* modes[] = { "single", "boost/cut", "wrap", "spread" };
                midiOut_->sendMessageNow(
                    juce::MidiMessage::controllerEvent(1, 48 + ring, val));
                appendLog("  Ring " + juce::String(ring + 1)
                          + " mode=" + modes[mode] + " pos=" + juce::String(pos));
                sweepNextMs_ = now + 250;
                ++sweepStep_;
            }
            else
            {
                for (int cc = 48; cc <= 55; ++cc)
                    midiOut_->sendMessageNow(juce::MidiMessage::controllerEvent(1, cc, 0));
                appendLog("--- Ring sweep done ---");
                sweepMode_ = SweepMode::Idle;
            }
        }
    }

    // -----------------------------------------------------------------------
    juce::Label      statusLabel_;
    juce::TextEditor deviceBox_;
    juce::TextEditor log_;
    juce::TextButton openBtn_, lightsOffBtn_, clearBtn_;
    juce::TextButton identBtn_, flashTestBtn_, brightnessBtn_, ringsBtn_;
    juce::Label      velLabel_;
    juce::Slider     velSlider_;
    juce::TextButton vel0Btn_, vel1Btn_, vel127Btn_;

    std::unique_ptr<juce::MidiInput>  midiIn_;
    std::unique_ptr<juce::MidiOutput> midiOut_;
    MidiQueue queue_;

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
        centreWithSize(960, 640);
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
    const juce::String getApplicationName()    override { return "Lockstep Controller Probe"; }
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
