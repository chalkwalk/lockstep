#pragma once

// StatusText.h — single source of truth for every user-visible status string.
//
// Rule (DESIGN §37.4): no string literal may be passed to status() / setStatus()
// outside this header. All user-visible status text is authored here and consumed
// at the call site. This makes status copy reviewable in one place and testable.
//
// All functions are inline so no .cpp is required.

#include <juce_core/juce_core.h>

namespace lockstep::status
{
    // ---- track operations --------------------------------------------------

    inline juce::String copiedTrack(int track)
    {
        return "Copied Track " + juce::String(track + 1);
    }

    inline juce::String pastedTrack(int track)
    {
        return "Pasted -> Track " + juce::String(track + 1);
    }

    inline juce::String clearedTrack(int track)
    {
        return "Cleared Track " + juce::String(track + 1);
    }

    inline juce::String deletedTrack(int track)
    {
        return "Deleted Track " + juce::String(track + 1);
    }

    // ---- phrase / pattern operations ----------------------------------------

    inline juce::String copiedPhrase()    { return "Copied Phrase"; }
    inline juce::String pastedPhrase()    { return "Pasted Phrase"; }
    inline juce::String clearedPhrase()   { return "Cleared Phrase"; }
    inline juce::String deletedPhrase()   { return "Deleted Phrase"; }
    inline juce::String deletedPart()     { return "Deleted Part"; }

    // ---- scene operations ---------------------------------------------------

    inline juce::String copiedScene()     { return "Copied Scene"; }
    inline juce::String pastedScene()     { return "Pasted Scene"; }
    inline juce::String pastedSceneFloor(){ return "Pasted Scene floor"; }

    inline juce::String sceneCreated(int sceneNumber)
    {
        return "Scene " + juce::String(sceneNumber) + " created";
    }

    inline juce::String sceneBaseline(int sceneNumber)
    {
        return "Scene " + juce::String(sceneNumber) + " (baseline)";
    }

    inline juce::String sceneBlank(int sceneNumber)
    {
        return "Scene " + juce::String(sceneNumber) + " (blank)";
    }

    // ---- length authoring ---------------------------------------------------

    inline juce::String length(int steps)
    {
        return "Length " + juce::String(steps);
    }

    inline juce::String lengthAllTracks(int steps)
    {
        return "Length " + juce::String(steps) + " (all tracks)";
    }

    // ---- morph / bake -------------------------------------------------------

    inline juce::String morphBaked()         { return "Morph baked"; }
    inline juce::String morphErased()        { return "Morph erased"; }
    inline juce::String morphMuteSet()       { return "Morph mute set"; }
    inline juce::String morphMuteCleared()   { return "Morph mute cleared"; }
    inline juce::String baked()              { return "Baked"; }
    inline juce::String noDeviationsToBake() { return "No deviations to bake"; }
    inline juce::String capturedAll()        { return "Captured all"; }

    // ---- transport / meta ---------------------------------------------------

    inline juce::String panic()        { return "Panic"; }
    inline juce::String quantized()    { return "Quantized"; }
    inline juce::String cancelled()    { return "Cancelled"; }

    // ---- clipboard / paste flow ---------------------------------------------

    inline juce::String nothingCopied()  { return "Nothing copied"; }
    inline juce::String pastePickScope() { return "Paste: pick a scope"; }

    // ---- confirm prompts (shown in the status band) -------------------------

    inline juce::String confirmDelete(const juce::String& entityName)
    {
        return "Delete " + entityName + "?  P=Yes  Func+P=No";
    }

    // Named confirm with 1-based slot index: "Delete PHRASE 3?  P=Yes  Func+P=No"
    inline juce::String confirmDeleteNamed(const juce::String& entity, int oneBasedIdx)
    {
        return "Delete " + entity + " " + juce::String(oneBasedIdx) + "?  P=Yes  Func+P=No";
    }

    inline juce::String confirmBake(int numTracks, int rowIdx)
    {
        return "Bake " + juce::String(numTracks) + " track(s) onto row "
             + juce::String(rowIdx) + "?  P=Yes  Func+P=No";
    }

} // namespace lockstep::status
