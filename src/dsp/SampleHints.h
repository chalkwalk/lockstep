#pragma once

// SampleHints.h — priors parsed from a sample's filename and embedded metadata
// (ACID WAV tags), and the fusion of those priors with the detected tempo/key
// (DESIGN §28, 4.9). Pure and header-only; the filename/metadata parsers are
// JUCE-light (StringPairArray in, no audio), and fuseAnalysis() is a pure
// function of the detection result + hints.
//
// Precedence is detection-first: the estimators (dsp/TempoEstimate.h,
// dsp/KeyEstimate.h) read the actual audio and win outright when confident. A
// hint only (a) resolves the tempo estimator's octave fold (a loop tagged 174
// that autocorrelates to 87), or (b) fills a gap the estimator left unknown.
// Metadata beats filename when both carry the same field.

#include "KeyEstimate.h"
#include "../core/Scale.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace lockstep
{
    struct SampleHints
    {
        double bpm = 0.0;               // 0 = no hint
        int    keyRoot = -1;            // -1 = no hint
        int    keyBrightness = kAeolian;// kIonian for maj, kAeolian for min/m
        bool   oneShot = false;         // ACID one-shot flag
    };

    namespace hint_detail
    {
        // Split a lowercased string into alnum(+'#') tokens; every other byte is
        // a delimiter. '#' is kept so "f#maj" survives as one token.
        inline std::vector<std::string> tokenize(const std::string& s)
        {
            std::vector<std::string> out;
            std::string cur;
            for (char ch : s)
            {
                const unsigned char c = static_cast<unsigned char>(ch);
                if (std::isalnum(c) || ch == '#')
                    cur += static_cast<char>(std::tolower(c));
                else if (!cur.empty())
                {
                    out.push_back(cur);
                    cur.clear();
                }
            }
            if (!cur.empty())
                out.push_back(cur);
            return out;
        }

        inline bool allDigits(const std::string& t)
        {
            if (t.empty()) return false;
            for (char c : t)
                if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            return true;
        }

        // Parse a bare integer if the whole token is digits, else -1.
        inline int asInt(const std::string& t)
        {
            if (!allDigits(t)) return -1;
            try { return std::stoi(t); } catch (...) { return -1; }
        }

        inline int noteLetterPc(char c)
        {
            switch (c)
            {
                case 'c': return 0;
                case 'd': return 2;
                case 'e': return 4;
                case 'f': return 5;
                case 'g': return 7;
                case 'a': return 9;
                case 'b': return 11;
                default:  return -1;
            }
        }

        // Try to read a key at the START of a token: [a-g](#|b)?(maj|major|
        // min|minor|m). A quality suffix is REQUIRED — a bare "a" is too
        // ambiguous to be a key hint. Returns true + fills root/brightness.
        inline bool parseKeyToken(const std::string& t, int& root, int& brightness)
        {
            if (t.size() < 2) return false;
            int pc = noteLetterPc(t[0]);
            if (pc < 0) return false;
            std::size_t i = 1;
            if (t[i] == '#') { pc = (pc + 1) % 12; ++i; }
            else if (t[i] == 'b') { pc = (pc + 11) % 12; ++i; }
            if (i >= t.size()) return false;   // accidental with no quality
            const std::string q = t.substr(i);
            if (q == "maj" || q == "major")
            {
                root = pc; brightness = kIonian; return true;
            }
            if (q == "min" || q == "minor" || q == "m")
            {
                root = pc; brightness = kAeolian; return true;
            }
            return false;
        }
    }

    inline SampleHints parseFilenameHints(const std::string& stem)
    {
        SampleHints h;
        const auto toks = hint_detail::tokenize(stem);

        // BPM pass (a): a 2-3 digit run adjacent to a "bpm" marker, either fused
        // ("120bpm" / "bpm120") or as a neighbouring token ("120 bpm").
        auto inBpmRange = [](int v) { return v >= 40 && v <= 300; };
        for (std::size_t i = 0; i < toks.size() && h.bpm <= 0.0; ++i)
        {
            const std::string& t = toks[i];
            // Fused: "<digits>bpm"
            if (t.size() > 3 && t.compare(t.size() - 3, 3, "bpm") == 0)
            {
                const int v = hint_detail::asInt(t.substr(0, t.size() - 3));
                if (v > 0 && inBpmRange(v)) { h.bpm = static_cast<double>(v); break; }
            }
            // Fused: "bpm<digits>"
            if (t.size() > 3 && t.compare(0, 3, "bpm") == 0)
            {
                const int v = hint_detail::asInt(t.substr(3));
                if (v > 0 && inBpmRange(v)) { h.bpm = static_cast<double>(v); break; }
            }
            // Neighbouring token "bpm".
            if (t == "bpm")
            {
                if (i > 0)
                {
                    const int v = hint_detail::asInt(toks[i - 1]);
                    if (v > 0 && inBpmRange(v)) { h.bpm = static_cast<double>(v); break; }
                }
                if (i + 1 < toks.size())
                {
                    const int v = hint_detail::asInt(toks[i + 1]);
                    if (v > 0 && inBpmRange(v)) { h.bpm = static_cast<double>(v); break; }
                }
            }
        }
        // BPM pass (b): first standalone integer in a tighter musical range.
        if (h.bpm <= 0.0)
        {
            for (const auto& t : toks)
            {
                const int v = hint_detail::asInt(t);
                if (v >= 60 && v <= 200) { h.bpm = static_cast<double>(v); break; }
            }
        }

        // Key: first token that parses as note+quality wins.
        for (const auto& t : toks)
        {
            int root = -1, brightness = kAeolian;
            if (hint_detail::parseKeyToken(t, root, brightness))
            {
                h.keyRoot = root;
                h.keyBrightness = brightness;
                break;
            }
        }
        return h;
    }

    inline SampleHints parseMetadataHints(const juce::StringPairArray& md)
    {
        SampleHints h;
        const auto tempo = md.getValue(juce::WavAudioFormat::acidTempo, "").getDoubleValue();
        if (tempo > 0.0)
            h.bpm = tempo;

        const int rootSet = md.getValue(juce::WavAudioFormat::acidRootSet, "0").getIntValue();
        if (rootSet != 0)
        {
            const int note = md.getValue(juce::WavAudioFormat::acidRootNote, "0").getIntValue();
            h.keyRoot = ((note % 12) + 12) % 12;
            // ACID carries no mode; brightness stays default and this root-only
            // hint is used only when detection produced no key of its own.
        }

        if (md.getValue(juce::WavAudioFormat::acidOneShot, "0").getIntValue() != 0)
            h.oneShot = true;
        return h;
    }

    // Metadata beats filename per field.
    inline SampleHints mergeHints(const SampleHints& meta, const SampleHints& fname)
    {
        SampleHints h;
        h.bpm = meta.bpm > 0.0 ? meta.bpm : fname.bpm;
        if (meta.keyRoot >= 0)
        {
            h.keyRoot = meta.keyRoot;
            h.keyBrightness = meta.keyBrightness;
        }
        else
        {
            h.keyRoot = fname.keyRoot;
            h.keyBrightness = fname.keyBrightness;
        }
        h.oneShot = meta.oneShot || fname.oneShot;
        return h;
    }

    // The fused, authoritative analysis written into the pool (and cache).
    struct FusedAnalysis
    {
        double bpm = 0.0;
        int    keyRoot = -1;
        int    keyBrightness = kAeolian;
        double tuningCents = 0.0;
    };

    inline FusedAnalysis fuseAnalysis(double detectedBpm, const KeyEstimate& key,
                                      const SampleHints& h)
    {
        FusedAnalysis out;

        // A one-shot has no meaningful tempo; ignore any bpm hint on it.
        const double hintBpm = h.oneShot ? 0.0 : h.bpm;

        if (detectedBpm > 0.0)
        {
            // The hint only overrides to resolve TempoEstimate's octave fold:
            // trust it when detection landed on ~2x or ~0.5x the hint.
            const double tol = 0.03 * detectedBpm;
            if (hintBpm > 0.0
                && (std::abs(detectedBpm - 2.0 * hintBpm) <= tol
                    || std::abs(detectedBpm - 0.5 * hintBpm) <= tol))
                out.bpm = hintBpm;
            else
                out.bpm = detectedBpm;
        }
        else
        {
            out.bpm = (hintBpm >= 40.0 && hintBpm <= 300.0) ? hintBpm : 0.0;
        }

        if (key.root >= 0)
        {
            out.keyRoot = key.root;
            out.keyBrightness = key.brightness;
        }
        else
        {
            out.keyRoot = h.keyRoot;
            out.keyBrightness = h.keyBrightness;
        }
        out.tuningCents = key.tuningCents;
        return out;
    }
}
