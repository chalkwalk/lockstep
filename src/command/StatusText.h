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

    inline juce::String clearedTrackAll(int track)
    {
        return "Cleared Track " + juce::String(track + 1) + " (all phrases)";
    }

    inline juce::String confirmClearTrack(int track)
    {
        return "Clear track " + juce::String(track + 1) + " -- this phrase?  P=CONFIRM  Func+P=CANCEL";
    }

    inline juce::String confirmClearTrackAll(int track)
    {
        return "Clear track " + juce::String(track + 1) + " -- ALL phrases?  P=CONFIRM  Func+P=CANCEL";
    }

    inline juce::String confirmClearPhrase()
    {
        return "Clear this phrase?  P=CONFIRM  Func+P=CANCEL";
    }

    inline juce::String deletedTrack(int track)
    {
        return "Deleted Track " + juce::String(track + 1);
    }

    // ---- phrase / pattern operations ----------------------------------------

    inline juce::String copiedPhrase() { return "Copied Phrase"; }
    inline juce::String pastedPhrase() { return "Pasted Phrase"; }
    inline juce::String clearedPhrase() { return "Cleared Phrase"; }
    inline juce::String deletedPhrase() { return "Deleted Phrase"; }
    inline juce::String deletedPart() { return "Deleted Part"; }

    // ---- scene operations ---------------------------------------------------

    inline juce::String copiedScene() { return "Copied Scene"; }
    inline juce::String pastedScene() { return "Pasted Scene"; }
    inline juce::String pastedSceneFloor() { return "Pasted Scene floor"; }

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

    // Shown when NavRight double-tap reveals an empty page past the end — tells
    // the player how to actually commit the longer length.
    inline juce::String scrolledPastEnd()
    {
        return "Past end - hold Func+Phrase + tap a step to set length";
    }

    // ---- morph / bake -------------------------------------------------------

    inline juce::String morphBaked() { return "Morph baked"; }
    inline juce::String morphErased() { return "Morph erased"; }
    inline juce::String morphMuteSet() { return "Morph mute set"; }
    inline juce::String morphMuteCleared() { return "Morph mute cleared"; }
    inline juce::String baked() { return "Baked"; }
    inline juce::String noDeviationsToBake() { return "No deviations to bake"; }
    inline juce::String capturedAll() { return "Captured all"; }

    // ---- capture ------------------------------------------------------------

    inline juce::String captureArmed(const juce::String& filename)
    {
        return "REC " + filename;
    }

    inline juce::String captureDisarmed(const juce::String& duration,
                                        const juce::String& filepath)
    {
        return "Captured " + duration + " -> " + filepath;
    }

    inline juce::String captureFailed() { return "Capture: could not open file"; }

    // ---- transport / meta ---------------------------------------------------

    inline juce::String panic() { return "Panic"; }
    inline juce::String quantized() { return "Quantized"; }
    inline juce::String cancelled() { return "Cancelled"; }

    // ---- clipboard / paste flow ---------------------------------------------

    inline juce::String nothingCopied() { return "Nothing copied"; }
    inline juce::String pastePickScope() { return "Paste: pick a scope"; }

    // ---- confirm prompts (shown in the status band) -------------------------

    inline juce::String confirmDelete(const juce::String& entityName)
    {
        return "Delete " + entityName + "?  P=CONFIRM  Func+P=CANCEL";
    }

    // Named confirm with 1-based slot index: "Delete PHRASE 3?  P=CONFIRM  Func+P=CANCEL"
    inline juce::String confirmDeleteNamed(const juce::String& entity, int oneBasedIdx)
    {
        return "Delete " + entity + " " + juce::String(oneBasedIdx) + "?  P=CONFIRM  Func+P=CANCEL";
    }

    // Delete picker entry prompt: "Delete which PHRASE?"
    inline juce::String deleteWhich(const juce::String& entity)
    {
        return "Delete which " + entity + "?";
    }

    inline juce::String confirmBake(int numTracks, int rowIdx)
    {
        return "Bake " + juce::String(numTracks) + " track(s) onto row " + juce::String(rowIdx) + "?  P=CONFIRM  Func+P=CANCEL";
    }

    // ---- sound bank ---------------------------------------------------------

    inline juce::String soundSaved(const juce::String& name)
    {
        return "Saved: " + name;
    }

    inline juce::String soundRecalled(const juce::String& name)
    {
        return "Recalled: " + name;
    }

    inline juce::String soundDeleted(const juce::String& name)
    {
        return "Deleted: " + name;
    }

    inline juce::String soundRenamed(const juce::String& name)
    {
        return "Renamed: " + name;
    }

    inline juce::String soundMachineMismatch(const juce::String& entryMachineId,
                                             const juce::String& trackMachineId)
    {
        return "Machine mismatch: bank=" + entryMachineId + " track=" + trackMachineId;
    }

    // Shown at the bottom of the Sound Bank overlay as a hint strip.
    inline juce::String soundBankHint() { return "Fill+SRC: performance recall"; }

} // namespace lockstep::status
