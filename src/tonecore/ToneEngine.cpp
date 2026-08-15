#include "ToneEngine.h"

#include "tone_preset_swap.h"

#include <fluidlite.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace lockstep::tone
{
namespace
{
    // ── Serving the bank from memory ──────────────────────────────────────
    // FluidLite's loader is written against a file API it lets us replace, so
    // the embedded blob is handed over without ever becoming a file. No temp
    // file, no install path to resolve, and nothing to clean up.
    //
    // The handle is a cursor over caller-owned bytes; the caller guarantees the
    // block outlives the engine (it is BinaryData, i.e. static storage).
    // kFluidOk / kFluidFailed are documented in the PUBLIC file-api contract
    // (sfont.h) but the enum itself lives in a private header. Rather than
    // widen tone_core's include path to reach it, the two documented values are
    // named here -- this file otherwise speaks only FluidLite's public API.
    constexpr int kFluidOk = 0;
    constexpr int kFluidFailed = -1;

    struct MemoryFile
    {
        const unsigned char* data = nullptr;
        long size = 0;
        long pos = 0;
    };

    struct MemorySource
    {
        const unsigned char* data = nullptr;
        long size = 0;
    };

    void* memOpen(fluid_fileapi_t* api, const char*)
    {
        // The filename is ignored on purpose: this api serves exactly one blob,
        // the one stashed in `data` when it was installed.
        auto* src = static_cast<MemorySource*>(api->data);
        if (src == nullptr || src->data == nullptr) return nullptr;
        auto* f = new (std::nothrow) MemoryFile{ src->data, src->size, 0 };
        return f;
    }

    int memRead(void* buf, int count, void* handle)
    {
        auto* f = static_cast<MemoryFile*>(handle);
        if (f == nullptr || count < 0) return kFluidFailed;
        if (f->pos + count > f->size) return kFluidFailed;   // short read is a failure
        std::memcpy(buf, f->data + f->pos, static_cast<std::size_t>(count));
        f->pos += count;
        return kFluidOk;
    }

    int memSeek(void* handle, long offset, int origin)
    {
        auto* f = static_cast<MemoryFile*>(handle);
        if (f == nullptr) return kFluidFailed;
        long target = 0;
        switch (origin)
        {
            case SEEK_SET: target = offset;            break;
            case SEEK_CUR: target = f->pos + offset;   break;
            case SEEK_END: target = f->size + offset;  break;
            default: return kFluidFailed;
        }
        if (target < 0 || target > f->size) return kFluidFailed;
        f->pos = target;
        return kFluidOk;
    }

    int memClose(void* handle)
    {
        delete static_cast<MemoryFile*>(handle);
        return kFluidOk;
    }

    long memTell(void* handle)
    {
        auto* f = static_cast<MemoryFile*>(handle);
        return f != nullptr ? f->pos : kFluidFailed;
    }

    // The sentinel handed to fluid_synth_sfload. It is never opened as a path.
    constexpr const char* kMemoryBankName = "lockstep://bank.sf3";
}   // namespace

// ---------------------------------------------------------------------------

struct ToneEngine::Impl
{
    fluid_settings_t* settings = nullptr;
    fluid_synth_t* synth = nullptr;
    fluid_sfont_t* sfont = nullptr;

    fluid_fileapi_t fileapi{};
    MemorySource source{};

    // Every preset the bank offers, resolved ONCE at load and owned here for
    // the engine's whole life. Keyed by (bank << 16) | program. Nothing in this
    // map is ever handed to FluidLite to own -- see tone_preset_swap.h.
    std::unordered_map<int, fluid_preset_t*> presets;

    // Render targets. FluidLite writes `audio_channels` separate stereo pairs,
    // one per group, and group N is channel N is track N.
    std::vector<float> storage;                      // 16 * 2 * maxBlock
    std::array<float*, ToneEngine::kNumChannels> left{};
    std::array<float*, ToneEngine::kNumChannels> right{};

    static int key(int bank, int program) noexcept { return (bank << 16) | program; }

    ~Impl()
    {
        if (synth != nullptr)
        {
            // RULE 4 (tone_preset_swap.h): detach before delete, or
            // delete_fluid_channel frees presets the cache still owns.
            lockstep_tone_detach_all_presets(synth);
            // The cache owns every preset; release through the preset's own
            // free callback, which is what delete_fluid_preset would call.
            for (auto& [k, p] : presets)
                if (p != nullptr && p->free != nullptr) p->free(p);
            presets.clear();
            delete_fluid_synth(synth);
        }
        if (settings != nullptr) delete_fluid_settings(settings);
        // fluid_set_default_fileapi is PROCESS-GLOBAL and we pointed it at a
        // member of this object. Put the stock api back, or a synth created
        // after this engine dies would capture a dangling pointer.
        fluid_set_default_fileapi(nullptr);
    }
};

ToneEngine::ToneEngine() : impl_(std::make_unique<Impl>()) {}
ToneEngine::~ToneEngine() = default;

void ToneEngine::setMaxBlockSize(int n)
{
    maxBlock_ = std::max(0, n);
    auto& im = *impl_;
    im.storage.assign(static_cast<std::size_t>(kNumChannels) * 2u
                          * static_cast<std::size_t>(maxBlock_),
                      0.0f);
    for (int c = 0; c < kNumChannels; ++c)
    {
        const std::size_t base = static_cast<std::size_t>(c) * 2u
                                 * static_cast<std::size_t>(maxBlock_);
        im.left[static_cast<std::size_t>(c)] = im.storage.data() + base;
        im.right[static_cast<std::size_t>(c)] =
            im.storage.data() + base + static_cast<std::size_t>(maxBlock_);
    }
}

bool ToneEngine::load(const void* sf3Data, int sf3Bytes, double sampleRate, int polyphony)
{
    auto& im = *impl_;
    ready_ = false;
    lastError_.clear();

    if (sf3Data == nullptr || sf3Bytes <= 0)
    {
        lastError_ = "no bank data";
        return false;
    }

    im.settings = new_fluid_settings();
    if (im.settings == nullptr) { lastError_ = "settings"; return false; }

    // 16 audio channels AND 16 groups. Groups are what routes a voice
    // (`auchan = channel_num % audio_groups`); channels are what
    // nwrite_float copies out. Both must be 16 for channel==group==track.
    fluid_settings_setint(im.settings, "synth.audio-channels", kNumChannels);
    fluid_settings_setint(im.settings, "synth.audio-groups", kNumChannels);
    fluid_settings_setint(im.settings, "synth.midi-channels", kNumChannels);
    fluid_settings_setint(im.settings, "synth.polyphony", std::max(16, polyphony));
    fluid_settings_setnum(im.settings, "synth.sample-rate",
                          std::clamp(sampleRate, 22050.0, 96000.0));

    // Mandatory, not stylistic: nwrite_float discards the fx buffers, so these
    // would cost CPU and produce nothing. See the header.
    fluid_settings_setstr(im.settings, "synth.reverb.active", "no");
    fluid_settings_setstr(im.settings, "synth.chorus.active", "no");

    // Free channel 9. Left on, FluidLite pins it to bank 128 and track 9 could
    // only ever be drums; off, any track reaches a kit by selecting bank 128.
    fluid_settings_setstr(im.settings, "synth.drums-channel.active", "no");

    // The file api must be installed BEFORE new_fluid_synth, not after: the
    // synth builds its default SoundFont loader during construction
    // (fluid_synth.c:435 -> new_fluid_defsfloader) and the loader captures
    // whatever `fluid_default_fileapi` points at THEN. Installing afterwards
    // leaves the loader on the stock file api, which tries to fopen our
    // sentinel as a path and fails -- measured, as an sfload error with nothing
    // to say why.
    //
    // `free` stays null deliberately: the api is a member of Impl, so nothing
    // owns it but us, and fluid_sfloader_delete would otherwise call free on it
    // when the synth goes.
    im.source = MemorySource{ static_cast<const unsigned char*>(sf3Data),
                              static_cast<long>(sf3Bytes) };
    im.fileapi.data = &im.source;
    im.fileapi.free = nullptr;
    im.fileapi.fopen = &memOpen;
    im.fileapi.fread = &memRead;
    im.fileapi.fseek = &memSeek;
    im.fileapi.fclose = &memClose;
    im.fileapi.ftell = &memTell;
    fluid_set_default_fileapi(&im.fileapi);

    im.synth = new_fluid_synth(im.settings);
    if (im.synth == nullptr) { lastError_ = "synth"; return false; }

    // FluidLite's default gain is a very conservative 0.2.
    fluid_synth_set_gain(im.synth, 1.0f);

    // RULE 3: reset_presets = 0. A reset would walk fluid_channel_reset, which
    // frees channel presets -- and after this load those are cache-owned.
    const int sfId = fluid_synth_sfload(im.synth, kMemoryBankName, 0);
    if (sfId == -1) { lastError_ = "sfload"; return false; }

    im.sfont = fluid_synth_get_sfont_by_id(im.synth, static_cast<unsigned int>(sfId));
    if (im.sfont == nullptr) { lastError_ = "sfont"; return false; }

    // ── Resolve every preset ONCE, here, on the message thread ────────────
    // fluid_sfont_get_preset mallocs on every call, so this is the only place
    // it may be called. Everything after this swaps cached pointers.
    melodic_.clear();
    drumKits_.clear();
    for (int prog = 0; prog < kNumPrograms; ++prog)
    {
        for (const int bank : { kMelodicBank, kDrumBank })
        {
            fluid_preset_t* p = im.sfont->get_preset(im.sfont,
                                                     static_cast<unsigned int>(bank),
                                                     static_cast<unsigned int>(prog));
            if (p == nullptr) continue;
            im.presets[Impl::key(bank, prog)] = p;

            const char* nm = (p->get_name != nullptr) ? p->get_name(p) : nullptr;
            Instrument inst{ bank, prog, nm != nullptr ? nm : "" };
            (bank == kDrumBank ? drumKits_ : melodic_).push_back(std::move(inst));
        }
    }

    if (melodic_.empty()) { lastError_ = "bank has no melodic presets"; return false; }

    // Park every channel on a real preset so a track makes sound before anyone
    // touches the program knob. Channel N = track N from the very first block.
    for (int c = 0; c < kNumChannels; ++c)
        selectProgram(c, kMelodicBank, 0);

    ready_ = true;
    return true;
}

void ToneEngine::selectProgram(int chan, int bank, int program) noexcept
{
    if (chan < 0 || chan >= kNumChannels) return;
    auto& im = *impl_;
    const auto it = im.presets.find(Impl::key(bank, program));
    if (it == im.presets.end()) return;    // bank does not carry it; leave as-is

    // RULE 1: assign, never fluid_channel_set_preset. No malloc, no free.
    lockstep_tone_set_channel_preset(im.synth, chan, it->second);
    fluid_synth_bank_select(im.synth, chan, static_cast<unsigned int>(bank));
}

void ToneEngine::noteOn(int chan, int note, int velocity) noexcept
{
    if (chan < 0 || chan >= kNumChannels) return;

    // The return value is NOT discarded. fluid_synth_noteon has silent failure
    // modes -- a channel with no preset, and voice allocation giving up -- and
    // each one is a note the sequencer believes it played. That is exactly the
    // "the trig fired, the VU dot flashed, nothing sounded" report, so a failure
    // here is counted (and, under LOCKSTEP_TRACE_TONE, printed with the state
    // needed to tell the two causes apart).
    if (traceNotes())
        std::fprintf(stderr, "[tone] blk=%lld ON  ch=%d note=%d vel=%d voices=%d\n",
                     blocks_.load(std::memory_order_relaxed), chan, note, velocity,
                     lockstep_tone_active_voice_count(impl_->synth));

    if (fluid_synth_noteon(impl_->synth, chan, note, velocity) != 0)   // FLUID_OK == 0; the public header does not export the enum
    {
        noteOnFailures_.fetch_add(1, std::memory_order_relaxed);
        if (std::getenv("LOCKSTEP_TRACE_TONE") != nullptr)
            std::fprintf(stderr,
                         "[tone] noteOn FAILED ch=%d note=%d vel=%d  preset=%s  "
                         "activeVoices=%d\n",
                         chan, note, velocity,
                         lockstep_tone_get_channel_preset(impl_->synth, chan) != nullptr ? "yes" : "NULL",
                         lockstep_tone_active_voice_count(impl_->synth));
    }
}

void ToneEngine::noteOff(int chan, int note) noexcept
{
    if (chan < 0 || chan >= kNumChannels) return;
    if (traceNotes())
        std::fprintf(stderr, "[tone] blk=%lld OFF ch=%d note=%d\n",
                     blocks_.load(std::memory_order_relaxed), chan, note);
    fluid_synth_noteoff(impl_->synth, chan, note);
}

void ToneEngine::controlChange(int chan, int cc, int value) noexcept
{
    if (chan < 0 || chan >= kNumChannels) return;
    fluid_synth_cc(impl_->synth, chan, cc, value);
}

void ToneEngine::pitchBend(int chan, int value14) noexcept
{
    if (chan < 0 || chan >= kNumChannels) return;
    fluid_synth_pitch_bend(impl_->synth, chan, value14);
}

void ToneEngine::allNotesOff(int chan) noexcept
{
    // The loudest line in the trace: this kills every sounding note on a track
    // at once, which is what a whole chord vanishing mid-sustain looks like.
    if (traceNotes())
        std::fprintf(stderr, "[tone] blk=%lld ALL-NOTES-OFF ch=%d  <-- kills the chord\n",
                     blocks_.load(std::memory_order_relaxed), chan);
    if (chan < 0 || chan >= kNumChannels) return;
    // CC 123. Not fluid_synth_system_reset -- RULE 2: that walks
    // fluid_channel_reset, which frees cache-owned presets.
    fluid_synth_cc(impl_->synth, chan, 123, 0);
}

bool ToneEngine::traceNotes() noexcept
{
    // Read once: getenv on the audio thread, every note, would be worse than the
    // bug being traced.
    static const bool on = std::getenv("LOCKSTEP_TRACE_TONE") != nullptr;
    return on;
}

void ToneEngine::render(int numSamples) noexcept
{
    auto& im = *impl_;
    blocks_.fetch_add(1, std::memory_order_relaxed);
    const int n = std::min(numSamples, maxBlock_);
    if (n <= 0 || !ready_) return;

    fluid_synth_nwrite_float(im.synth, n, im.left.data(), im.right.data(),
                             nullptr, nullptr);
}

const float* ToneEngine::groupLeft(int chan) const noexcept
{
    if (chan < 0 || chan >= kNumChannels) return nullptr;
    return impl_->left[static_cast<std::size_t>(chan)];
}

const float* ToneEngine::groupRight(int chan) const noexcept
{
    if (chan < 0 || chan >= kNumChannels) return nullptr;
    return impl_->right[static_cast<std::size_t>(chan)];
}
}   // namespace lockstep::tone
