// Lockstep Palette Capture — offline image tool (no MIDI).
//
// Captures the Ableton Push 1's *as-displayed* LED palette from a photograph,
// so the runtime surface can match on-screen RGB to the nearest factory entry in
// Oklab. Workflow:
//   1. push_probe -> "Palette 0-63" / "Palette 64-127" lights all 64 pads with a
//      fixed index page; photograph each page roughly top-down.
//   2. Here: Load the photo, pick the 4 pad-array corners (TL, TR, BR, BL), tune
//      the pad/gap ratios + sample-square size until the overlay squares sit on
//      the lit pad centres, choose the page, and Sample. Repeat for the other
//      page. Then Export the Push1Palette.h constant.
//
// Sampling is perspective-correct: a homography maps grid space [0,1]^2 to the
// photo, and each pad's colour is the linear-light average of pixels inside its
// mapped sample square. index 0 = off (true black).

#include <JuceHeader.h>
#include "controller/Oklab.h"
#include <array>

namespace lockstep
{

// ---------------------------------------------------------------------------
// Heckbert unit-square -> quad homography. Corners in order:
//   p0 = (0,0) TL, p1 = (1,0) TR, p2 = (1,1) BR, p3 = (0,1) BL.
// ---------------------------------------------------------------------------
struct Homography
{
    double a=1,b=0,c=0,d=0,e=1,f=0,g=0,h=0;
    bool valid = false;

    static Homography fromQuad(juce::Point<float> p0, juce::Point<float> p1,
                               juce::Point<float> p2, juce::Point<float> p3)
    {
        Homography H;
        const double x0=p0.x, y0=p0.y, x1=p1.x, y1=p1.y;
        const double x2=p2.x, y2=p2.y, x3=p3.x, y3=p3.y;
        const double sx = x0 - x1 + x2 - x3;
        const double sy = y0 - y1 + y2 - y3;

        if (std::abs(sx) < 1e-9 && std::abs(sy) < 1e-9)
        {
            H.a = x1 - x0; H.b = x2 - x1; H.c = x0;
            H.d = y1 - y0; H.e = y2 - y1; H.f = y0;
            H.g = 0; H.h = 0;
        }
        else
        {
            const double dx1 = x1 - x2, dx2 = x3 - x2;
            const double dy1 = y1 - y2, dy2 = y3 - y2;
            const double den = dx1 * dy2 - dx2 * dy1;
            if (std::abs(den) < 1e-12) return H;  // degenerate
            H.g = (sx * dy2 - dx2 * sy) / den;
            H.h = (dx1 * sy - sx * dy1) / den;
            H.a = x1 - x0 + H.g * x1;
            H.b = x3 - x0 + H.h * x3;
            H.c = x0;
            H.d = y1 - y0 + H.g * y1;
            H.e = y3 - y0 + H.h * y3;
            H.f = y0;
        }
        H.valid = true;
        return H;
    }

    juce::Point<float> map(double u, double v) const
    {
        const double w = g * u + h * v + 1.0;
        const double x = (a * u + b * v + c) / w;
        const double y = (d * u + e * v + f) / w;
        return { static_cast<float>(x), static_cast<float>(y) };
    }
};

// ---------------------------------------------------------------------------
class CaptureComponent : public juce::Component
{
public:
    CaptureComponent()
    {
        addAndMakeVisible(loadBtn_);     loadBtn_.setButtonText("Load Image...");
        addAndMakeVisible(cornersBtn_);  cornersBtn_.setButtonText("Set Corners");
        addAndMakeVisible(sampleBtn_);   sampleBtn_.setButtonText("Sample Page");
        addAndMakeVisible(exportBtn_);   exportBtn_.setButtonText("Export Header...");
        addAndMakeVisible(pageBox_);
        pageBox_.addItem("Page 0  (idx 0-63)",  1);
        pageBox_.addItem("Page 1  (idx 64-127)", 2);
        pageBox_.setSelectedId(1, juce::dontSendNotification);

        addAndMakeVisible(status_);
        status_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        status_.setText("Load a photo of one lit palette page.", juce::dontSendNotification);

        auto addSlider = [this](juce::Slider& s, juce::Label& lab, const char* name,
                                double lo, double hi, double val)
        {
            addAndMakeVisible(s);
            s.setRange(lo, hi, 0.001);
            s.setValue(val, juce::dontSendNotification);
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 18);
            s.onValueChange = [this] { repaint(); };
            addAndMakeVisible(lab);
            lab.setText(name, juce::dontSendNotification);
            lab.setColour(juce::Label::textColourId, juce::Colours::grey);
        };
        addSlider(padGapH_, padGapHLab_, "pad/gap H", 0.3, 6.0, 2.0);
        addSlider(padGapV_, padGapVLab_, "pad/gap V", 0.3, 6.0, 2.0);
        addSlider(sample_,  sampleLab_,  "sample %",  0.1, 0.95, 0.5);

        loadBtn_   .onClick = [this] { loadImage(); };
        cornersBtn_.onClick = [this] { arming_ = true; cornerCount_ = 0;
                                       setStatus("Click corner: TL"); repaint(); };
        sampleBtn_ .onClick = [this] { samplePage(); };
        exportBtn_ .onClick = [this] { exportHeader(); };

        captured_.fill(0);
        capturedSet_.fill(false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(6);
        auto top = area.removeFromTop(28);
        loadBtn_   .setBounds(top.removeFromLeft(120).reduced(2,0));
        cornersBtn_.setBounds(top.removeFromLeft(110).reduced(2,0));
        pageBox_   .setBounds(top.removeFromLeft(170).reduced(2,0));
        sampleBtn_ .setBounds(top.removeFromLeft(110).reduced(2,0));
        exportBtn_ .setBounds(top.removeFromLeft(140).reduced(2,0));

        auto row2 = area.removeFromTop(24);
        auto sl = [&row2](juce::Label& lab, juce::Slider& s)
        {
            lab.setBounds(row2.removeFromLeft(70));
            s.setBounds(row2.removeFromLeft(190).reduced(2,0));
        };
        sl(padGapHLab_, padGapH_);
        sl(padGapVLab_, padGapV_);
        sl(sampleLab_,  sample_);

        status_.setBounds(area.removeFromBottom(22));
        swatchArea_ = area.removeFromBottom(70).toFloat();
        imageArea_  = area.toFloat();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff202024));

        // --- Image ---
        if (image_.isValid())
        {
            computeImagePlacement();
            g.drawImage(image_, drawRect_,
                        juce::RectanglePlacement::stretchToFit);

            // Corner markers
            g.setColour(juce::Colours::yellow);
            for (int i = 0; i < cornerCount_; ++i)
            {
                const auto p = imgToComp(corners_[(size_t)i]);
                g.drawEllipse(p.x-5, p.y-5, 10, 10, 2.0f);
                g.drawText(kCornerNames[i], (int)p.x+6, (int)p.y-8, 24, 16,
                           juce::Justification::left);
            }

            // Sample-square overlay (when all 4 corners are set)
            if (cornerCount_ == 4)
            {
                const auto H = currentHomography();
                g.setColour(juce::Colours::cyan.withAlpha(0.8f));
                const float sf = (float)sample_.getValue();
                for (int row = 0; row < 8; ++row)
                    for (int col = 0; col < 8; ++col)
                    {
                        double cu, cv, pu, pv;
                        cellCentre(row, col, cu, cv, pu, pv);
                        const float hu = 0.5f * sf * (float)pu;
                        const float hv = 0.5f * sf * (float)pv;
                        juce::Path quad;
                        const auto q0 = imgToComp(H.map(cu-hu, cv-hv));
                        const auto q1 = imgToComp(H.map(cu+hu, cv-hv));
                        const auto q2 = imgToComp(H.map(cu+hu, cv+hv));
                        const auto q3 = imgToComp(H.map(cu-hu, cv+hv));
                        quad.startNewSubPath(q0); quad.lineTo(q1);
                        quad.lineTo(q2); quad.lineTo(q3); quad.closeSubPath();
                        g.strokePath(quad, juce::PathStrokeType(1.0f));
                    }
            }
        }
        else
        {
            g.setColour(juce::Colours::grey);
            g.drawText("No image loaded", imageArea_, juce::Justification::centred);
        }

        // --- Swatch preview: 128 captured colours, page 0 top row, page 1 below ---
        const float sw = swatchArea_.getWidth() / 64.0f;
        const float sh = swatchArea_.getHeight() / 2.0f;
        for (int idx = 0; idx < 128; ++idx)
        {
            const int rowp = idx / 64;       // 0 = page 0, 1 = page 1
            const int colp = idx % 64;
            const juce::Rectangle<float> r(swatchArea_.getX() + (float)colp * sw,
                                           swatchArea_.getY() + (float)rowp * sh, sw, sh);
            if (capturedSet_[(size_t)idx])
                g.setColour(juce::Colour(0xff000000u | captured_[(size_t)idx]));
            else
                g.setColour(juce::Colour(0xff141414));
            g.fillRect(r.reduced(0.5f));
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!arming_ || !image_.isValid()) return;
        if (!imageArea_.contains(e.position)) return;
        const auto img = compToImg(e.position);
        corners_[(size_t)cornerCount_] = img;
        ++cornerCount_;
        if (cornerCount_ >= 4)
        {
            arming_ = false;
            setStatus("Corners set. Tune sliders so the cyan squares sit on the pads, then Sample.");
        }
        else
            setStatus(juce::String("Click corner: ") + kCornerNames[cornerCount_]);
        repaint();
    }

private:
    static constexpr const char* kCornerNames[4] = { "TL", "TR", "BR", "BL" };

    void setStatus(const juce::String& s)
    {
        status_.setText(s, juce::dontSendNotification);
    }

    // Grid cell centre + pad size in grid space (col -> u, row 0 = top -> v).
    void cellCentre(int row, int col, double& u, double& v,
                    double& padU, double& padV) const
    {
        const double Rh = padGapH_.getValue();
        const double Rv = padGapV_.getValue();
        const double gH = 1.0 / (8.0 * Rh + 7.0), pH = Rh * gH;
        const double gV = 1.0 / (8.0 * Rv + 7.0), pV = Rv * gV;
        u = (double)col * (pH + gH) + pH * 0.5;
        v = (double)row * (pV + gV) + pV * 0.5;
        padU = pH; padV = pV;
    }

    Homography currentHomography() const
    {
        return Homography::fromQuad(corners_[0], corners_[1], corners_[2], corners_[3]);
    }

    // Palette index for an overlay cell. Image top row = highest pad row
    // (note 92-99 = rowFromBottom 7); probe index = base + rowFromBottom*8 + col.
    int paletteIndex(int row, int col) const
    {
        const int base = (pageBox_.getSelectedId() - 1) * 64;
        const int rowFromBottom = 7 - row;
        return base + rowFromBottom * 8 + col;
    }

    void samplePage()
    {
        if (!image_.isValid()) { setStatus("Load an image first."); return; }
        if (cornerCount_ < 4)  { setStatus("Set the 4 corners first."); return; }

        const auto H = currentHomography();
        if (!H.valid) { setStatus("Degenerate corners — re-pick."); return; }

        const float sf = (float)sample_.getValue();
        constexpr int N = 9;  // NxN sample points per cell

        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col)
            {
                double cu, cv, pu, pv;
                cellCentre(row, col, cu, cv, pu, pv);
                const double hu = 0.5 * sf * pu;
                const double hv = 0.5 * sf * pv;

                double lr = 0, lg = 0, lb = 0;
                int n = 0;
                for (int sy = 0; sy < N; ++sy)
                    for (int sx = 0; sx < N; ++sx)
                    {
                        const double fu = (N == 1) ? 0.0 : ((double)sx / (N - 1) - 0.5);
                        const double fv = (N == 1) ? 0.0 : ((double)sy / (N - 1) - 0.5);
                        const auto ip = H.map(cu + fu * 2.0 * hu, cv + fv * 2.0 * hv);
                        const int px = juce::jlimit(0, image_.getWidth()  - 1, (int)std::lround(ip.x));
                        const int py = juce::jlimit(0, image_.getHeight() - 1, (int)std::lround(ip.y));
                        const auto c = image_.getPixelAt(px, py);
                        lr += oklab::srgbToLinear((float)c.getRed()   / 255.0f);
                        lg += oklab::srgbToLinear((float)c.getGreen() / 255.0f);
                        lb += oklab::srgbToLinear((float)c.getBlue()  / 255.0f);
                        ++n;
                    }

                const float inv = 1.0f / (float)n;
                const int r8 = juce::jlimit(0, 255, (int)std::lround(oklab::linearToSrgb((float)lr * inv) * 255.0f));
                const int g8 = juce::jlimit(0, 255, (int)std::lround(oklab::linearToSrgb((float)lg * inv) * 255.0f));
                const int b8 = juce::jlimit(0, 255, (int)std::lround(oklab::linearToSrgb((float)lb * inv) * 255.0f));

                const int idx = paletteIndex(row, col);
                captured_[(size_t)idx] = ((juce::uint32)r8 << 16)
                                       | ((juce::uint32)g8 << 8)
                                       |  (juce::uint32)b8;
                capturedSet_[(size_t)idx] = true;
            }

        setStatus("Sampled page " + juce::String(pageBox_.getSelectedId() - 1)
                  + ". " + reportFloor());
        repaint();
    }

    // Oklab distance between off (index 0) and the darkest LIT entry.
    juce::String reportFloor() const
    {
        if (!capturedSet_[0]) return {};
        const auto off = oklab::packedRgbToOklab(captured_[0]);
        float bestL = 1e9f; int bestIdx = -1;
        for (int i = 1; i < 128; ++i)
            if (capturedSet_[(size_t)i])
            {
                const auto lab = oklab::packedRgbToOklab(captured_[(size_t)i]);
                if (lab.L < bestL) { bestL = lab.L; bestIdx = i; }
            }
        if (bestIdx < 0) return {};
        const auto dk = oklab::packedRgbToOklab(captured_[(size_t)bestIdx]);
        const float dist = std::sqrt(oklab::distanceSq(off, dk));
        return "off<->darkest-lit (idx " + juce::String(bestIdx)
             + ") Oklab dist = " + juce::String(dist, 3);
    }

    void loadImage()
    {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Select a palette-page photo", juce::File(), "*.png;*.jpg;*.jpeg");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f == juce::File()) return;
                auto img = juce::ImageFileFormat::loadFrom(f);
                if (img.isValid())
                {
                    image_ = img;
                    cornerCount_ = 0; arming_ = false;
                    setStatus("Loaded " + f.getFileName()
                              + ". Click 'Set Corners' then pick TL, TR, BR, BL.");
                    repaint();
                }
                else setStatus("Could not load image.");
            });
    }

    void exportHeader()
    {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Export Push1Palette.h", juce::File(), "*.h");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                if (f.getFileExtension().isEmpty()) f = f.withFileExtension(".h");
                f.replaceWithText(buildHeader());
                setStatus("Wrote " + f.getFullPathName() + ". " + reportFloor());
            });
    }

    juce::String buildHeader() const
    {
        const auto date = juce::Time::getCurrentTime().toString(true, false);
        juce::String s;
        s << "#pragma once\n#include <array>\n#include <cstdint>\n\n"
          << "namespace lockstep\n{\n"
          << "    // Ableton Push 1 *as-displayed* palette, captured " << date << "\n"
          << "    // via tools/palette_capture. index 0 = off (true black).\n"
          << "    // Values are linear-averaged sRGB, packed 0xRRGGBB.\n"
          << "    inline constexpr std::array<std::uint32_t, 128> kCapturedPalette = {{\n";
        for (int i = 0; i < 128; ++i)
        {
            if (i % 4 == 0) s << "        ";
            s << "0x" << juce::String::toHexString((int)captured_[(size_t)i])
                              .paddedLeft('0', 6).toUpperCase() << "u,";
            s << ((i % 4 == 3) ? "\n" : " ");
        }
        s << "    }};\n}\n";
        return s;
    }

    // --- coordinate transforms (image px <-> component) ---
    void computeImagePlacement()
    {
        if (!image_.isValid()) return;
        const float iw = (float)image_.getWidth();
        const float ih = (float)image_.getHeight();
        const float scale = juce::jmin(imageArea_.getWidth() / iw,
                                       imageArea_.getHeight() / ih);
        const float w = iw * scale, h = ih * scale;
        drawRect_ = { imageArea_.getCentreX() - w * 0.5f,
                      imageArea_.getCentreY() - h * 0.5f, w, h };
        scale_ = scale;
    }
    juce::Point<float> imgToComp(juce::Point<float> p) const
    {
        return { drawRect_.getX() + p.x * scale_, drawRect_.getY() + p.y * scale_ };
    }
    juce::Point<float> compToImg(juce::Point<float> p) const
    {
        return { (p.x - drawRect_.getX()) / scale_, (p.y - drawRect_.getY()) / scale_ };
    }

    juce::TextButton loadBtn_, cornersBtn_, sampleBtn_, exportBtn_;
    juce::ComboBox   pageBox_;
    juce::Label      status_;
    juce::Slider     padGapH_, padGapV_, sample_;
    juce::Label      padGapHLab_, padGapVLab_, sampleLab_;

    juce::Image image_;
    juce::Rectangle<float> imageArea_, swatchArea_, drawRect_;
    float scale_ = 1.0f;

    std::array<juce::Point<float>, 4> corners_{};
    int  cornerCount_ = 0;
    bool arming_ = false;

    std::array<juce::uint32, 128> captured_{};   // 0xRRGGBB
    std::array<bool, 128>         capturedSet_{};

    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CaptureComponent)
};

// ---------------------------------------------------------------------------
class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow(const juce::String& name)
        : juce::DocumentWindow(name,
              juce::Desktop::getInstance().getDefaultLookAndFeel()
                  .findColour(juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new CaptureComponent(), true);
        setResizable(true, true);
        centreWithSize(1100, 820);
        setVisible(true);
    }
    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class App : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Lockstep Palette Capture"; }
    const juce::String getApplicationVersion() override { return "0.1"; }
    void initialise(const juce::String&) override { window_ = std::make_unique<MainWindow>(getApplicationName()); }
    void shutdown() override { window_.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    std::unique_ptr<MainWindow> window_;
};

} // namespace lockstep

START_JUCE_APPLICATION(lockstep::App)
