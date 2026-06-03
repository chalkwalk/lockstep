// Throwaway probe tool — confirms X-Touch Mini (MCU) input note numbers,
// encoder sign convention, button brightness curve, and ring rendering.
// Not wired into the plugin; build with the controller_probe CMake target.

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstring>

namespace lockstep
{

// ---------------------------------------------------------------------------
// Thread-safe single-producer / single-consumer MIDI message queue.
// MidiInputCallback runs on JUCE's MIDI thread; UI drains on message thread.
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

    // Calls fn(MidiMessage) for each pending message. Must be called from one thread only.
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
// ProbeComponent — the main UI panel
// ---------------------------------------------------------------------------
class ProbeComponent : public juce::Component,
                       public juce::MidiInputCallback,
                       public juce::Timer
{
public:
    ProbeComponent()
    {
        addAndMakeVisible(statusLabel_);
        statusLabel_.setText("Not connected", juce::dontSendNotification);
        statusLabel_.setFont(juce::Font(juce::FontOptions(13.0f)));

        addAndMakeVisible(deviceLabel_);
        deviceLabel_.setText("Available MIDI outputs:", juce::dontSendNotification);
        deviceLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));

        addAndMakeVisible(deviceList_);
        deviceList_.setFont(juce::Font(juce::FontOptions(10.0f)));
        deviceList_.setMultiLine(true);
        deviceList_.setReadOnly(true);
        deviceList_.setScrollbarsShown(true);

        addAndMakeVisible(logLabel_);
        logLabel_.setText("MIDI Log (incoming):", juce::dontSendNotification);

        addAndMakeVisible(log_);
        log_.setMultiLine(true);
        log_.setReadOnly(true);
        log_.setScrollbarsShown(true);
        log_.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, 0)));

        auto makeBtn = [&](juce::TextButton& b, const char* label) {
            addAndMakeVisible(b);
            b.setButtonText(label);
        };

        makeBtn(openBtn_,          "Open X-Touch Mini");
        makeBtn(clearBtn_,         "Clear Log");
        makeBtn(sweepBrightnessBtn_,"Sweep Button Brightness (note 86)");
        makeBtn(sweepAllNotesBtn_, "Find Button Notes (0-127)");
        makeBtn(sweepRingsBtn_,    "Sweep Rings");
        makeBtn(lightsOffBtn_,     "All Lights Off");

        openBtn_.onClick = [this]          { openDevice(); };
        clearBtn_.onClick = [this]         { log_.clear(); };
        sweepBrightnessBtn_.onClick = [this]{ startBrightnessSweep(); };
        sweepAllNotesBtn_.onClick = [this]  { startNoteSweep(); };
        sweepRingsBtn_.onClick = [this]     { startRingSweep(); };
        lightsOffBtn_.onClick = [this]      { allLightsOff(); };

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

        // Status
        statusLabel_.setBounds(area.removeFromTop(22));
        area.removeFromTop(4);

        // Device list (top-right quadrant)
        auto top = area.removeFromTop(80);
        deviceLabel_.setBounds(top.removeFromTop(16));
        deviceList_.setBounds(top);
        area.removeFromTop(4);

        // Button row
        auto btnRow = area.removeFromTop(28);
        const int bw = btnRow.getWidth() / 6;
        openBtn_          .setBounds(btnRow.removeFromLeft(bw).reduced(2, 0));
        sweepBrightnessBtn_.setBounds(btnRow.removeFromLeft(bw * 2).reduced(2, 0));
        sweepAllNotesBtn_ .setBounds(btnRow.removeFromLeft(bw).reduced(2, 0));
        sweepRingsBtn_    .setBounds(btnRow.removeFromLeft(bw).reduced(2, 0));
        lightsOffBtn_     .setBounds(btnRow.removeFromLeft(bw / 2).reduced(2, 0));
        clearBtn_         .setBounds(btnRow.reduced(2, 0));
        area.removeFromTop(4);

        // Log
        logLabel_.setBounds(area.removeFromTop(16));
        log_.setBounds(area);
    }

    // juce::MidiInputCallback — MIDI thread
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override
    {
        queue_.push(msg);
    }

    // juce::Timer — message thread
    void timerCallback() override
    {
        queue_.drain([this](const juce::MidiMessage& msg) { logMessage(msg); });
        advanceSweep();
    }

private:
    // -----------------------------------------------------------------------
    void refreshDeviceList()
    {
        juce::String txt;
        txt += "IN:\n";
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            txt += "  " + d.name + "\n";
        txt += "OUT:\n";
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            txt += "  " + d.name + "\n";
        deviceList_.setText(txt, false);
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

        if (midiIn_)  midiIn_->start();

        if (midiIn_ && midiOut_)
        {
            statusLabel_.setText("Connected: " + foundIn, juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
            appendLog("--- Device opened: " + foundIn + " ---");
        }
        else if (midiIn_ || midiOut_)
        {
            statusLabel_.setText("Partial: in=" + foundIn + " out=" + foundOut,
                                 juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colours::orange);
        }
        else
        {
            statusLabel_.setText("X-Touch Mini NOT found. Available devices listed above.",
                                 juce::dontSendNotification);
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
            s << "NoteOn   ch=" << ch
              << "  note=" << msg.getNoteNumber()
              << "  vel="  << msg.getVelocity();

            // Annotate known controls
            const int n = msg.getNoteNumber();
            if (n >= 32 && n <= 39) s << "  [ENC PUSH " << (n - 32 + 1) << "]";
            else if (n >= 86 && n <= 93) s << "  [BTN TOP "  << (n - 86 + 1) << "]";
            else if (n == 46) s << "  [LAYER A]";
            else if (n == 47) s << "  [LAYER B]";
            else s << "  [UNKNOWN]";
        }
        else if (msg.isNoteOff())
        {
            const int n = msg.getNoteNumber();
            s << "NoteOff  ch=" << ch << "  note=" << n;
            if (n >= 32 && n <= 39) s << "  [ENC PUSH " << (n - 32 + 1) << "]";
            else if (n >= 86 && n <= 93) s << "  [BTN TOP " << (n - 86 + 1) << "]";
            else if (n == 46) s << "  [LAYER A]";
            else if (n == 47) s << "  [LAYER B]";
            else s << "  [UNKNOWN]";
        }
        else if (msg.isController())
        {
            const int cc  = msg.getControllerNumber();
            const int val = msg.getControllerValue();
            s << "CC       ch=" << ch << "  cc=" << cc << "  val=" << val;

            if (cc >= 16 && cc <= 23)
            {
                // Signed-magnitude relative: 1-63=CW, 65-127=CCW
                int delta = (val <= 63) ? val : -(val - 64);
                s << "  [ENC " << (cc - 16 + 1) << " delta=" << delta << "]";
            }
            else if (cc >= 48 && cc <= 55)
            {
                const int mode = (val >> 4) & 0x3;
                const int pos  = val & 0x0F;
                const char* modes[] = { "single", "boost/cut", "wrap", "spread" };
                s << "  [RING " << (cc - 48 + 1) << " mode=" << modes[mode] << " pos=" << pos << "]";
            }
        }
        else if (msg.isPitchWheel())
        {
            s << "PitchBend ch=" << ch << "  val=" << msg.getPitchWheelValue();
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
    // Sweep state machine — driven by timerCallback
    // -----------------------------------------------------------------------
    enum class SweepMode { Idle, Brightness, Notes, Rings };
    SweepMode   sweepMode_     = SweepMode::Idle;
    int         sweepStep_     = 0;
    juce::uint32 sweepNextMs_  = 0;

    void startBrightnessSweep()
    {
        if (!midiOut_) { appendLog("No output device — open device first."); return; }
        sweepMode_   = SweepMode::Brightness;
        sweepStep_   = 0;
        sweepNextMs_ = juce::Time::getMillisecondCounter();
        appendLog("--- Brightness sweep on note 86 (watch top-row button 1) ---");
    }

    void startNoteSweep()
    {
        if (!midiOut_) { appendLog("No output device — open device first."); return; }
        sweepMode_   = SweepMode::Notes;
        sweepStep_   = 0;
        sweepNextMs_ = juce::Time::getMillisecondCounter();
        appendLog("--- Note sweep 0-127 at vel 127 (watch for lit buttons) ---");
    }

    void startRingSweep()
    {
        if (!midiOut_) { appendLog("No output device — open device first."); return; }
        sweepMode_   = SweepMode::Rings;
        sweepStep_   = 0;
        sweepNextMs_ = juce::Time::getMillisecondCounter();
        appendLog("--- Ring sweep: all modes x 12 positions on encoder 1 ---");
    }

    void allLightsOff()
    {
        if (!midiOut_) return;
        sweepMode_ = SweepMode::Idle;
        // Zero out all known button notes
        const int notes[] = { 32,33,34,35,36,37,38,39,
                               46,47,
                               86,87,88,89,90,91,92,93 };
        for (int n : notes)
            midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1, n, (juce::uint8)0));
        // Zero all rings
        for (int cc = 48; cc <= 55; ++cc)
            midiOut_->sendMessageNow(juce::MidiMessage::controllerEvent(1, cc, 0));
        appendLog("--- All lights off ---");
    }

    void advanceSweep()
    {
        if (sweepMode_ == SweepMode::Idle || !midiOut_) return;
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        if (now < sweepNextMs_) return;

        if (sweepMode_ == SweepMode::Brightness)
        {
            // Steps: vel 0, 8, 16, 24, ... 120, 127, then done.
            const int velocities[] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 127 };
            constexpr int kSteps = static_cast<int>(std::size(velocities));
            if (sweepStep_ < kSteps)
            {
                const int vel = velocities[sweepStep_];
                midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1, 86, (juce::uint8)vel));
                appendLog("  Brightness: note 86 vel=" + juce::String(vel));
                sweepNextMs_ = now + 200;
                ++sweepStep_;
            }
            else
            {
                // Restore to off
                midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1, 86, (juce::uint8)0));
                appendLog("--- Brightness sweep done ---");
                sweepMode_ = SweepMode::Idle;
            }
        }
        else if (sweepMode_ == SweepMode::Notes)
        {
            // Step through notes 0-127: send NoteOn vel=127 then schedule NoteOff 30ms later.
            // We abuse sweepStep_ to encode (note * 2) + phase (0=on, 1=off)
            const int note  = sweepStep_ / 2;
            const int phase = sweepStep_ % 2;
            if (note >= 128)
            {
                appendLog("--- Note sweep done ---");
                sweepMode_ = SweepMode::Idle;
                return;
            }
            if (phase == 0)
            {
                midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1, note, (juce::uint8)127));
                sweepNextMs_ = now + 40;
            }
            else
            {
                midiOut_->sendMessageNow(juce::MidiMessage::noteOn(1, note, (juce::uint8)0));
                sweepNextMs_ = now + 10;
            }
            ++sweepStep_;
        }
        else if (sweepMode_ == SweepMode::Rings)
        {
            // 4 modes x 12 positions = 48 steps, then done.
            constexpr int kTotal = 4 * 12;
            if (sweepStep_ < kTotal)
            {
                const int mode = sweepStep_ / 12;
                const int pos  = sweepStep_ % 12;
                const int val  = (mode << 4) | pos;
                midiOut_->sendMessageNow(juce::MidiMessage::controllerEvent(1, 48, val));
                const char* modes[] = { "single", "boost/cut", "wrap", "spread" };
                appendLog("  Ring 1: mode=" + juce::String(modes[mode]) + " pos=" + juce::String(pos) + " val=" + juce::String(val));
                sweepNextMs_ = now + 300;
                ++sweepStep_;
            }
            else
            {
                // Reset ring to off
                midiOut_->sendMessageNow(juce::MidiMessage::controllerEvent(1, 48, 0));
                appendLog("--- Ring sweep done ---");
                sweepMode_ = SweepMode::Idle;
            }
        }
    }

    // -----------------------------------------------------------------------
    juce::Label      statusLabel_;
    juce::Label      deviceLabel_;
    juce::TextEditor deviceList_;
    juce::Label      logLabel_;
    juce::TextEditor log_;
    juce::TextButton openBtn_;
    juce::TextButton clearBtn_;
    juce::TextButton sweepBrightnessBtn_;
    juce::TextButton sweepAllNotesBtn_;
    juce::TextButton sweepRingsBtn_;
    juce::TextButton lightsOffBtn_;

    std::unique_ptr<juce::MidiInput>  midiIn_;
    std::unique_ptr<juce::MidiOutput> midiOut_;
    MidiQueue queue_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProbeComponent)
};

// ---------------------------------------------------------------------------
// Application boilerplate
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
        centreWithSize(900, 600);
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
