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

    // ---- cue balance (6.4, DESIGN §31) --------------------------------------

    inline juce::String cuedTrack(int track)
    {
        return "Cue -> Track " + juce::String(track + 1) + " (to headphones)";
    }

    inline juce::String uncuedTrack(int track)
    {
        return "Cue off -- Track " + juce::String(track + 1) + " back to main";
    }

    // ---- scene operations ---------------------------------------------------

    inline juce::String copiedScene() { return "Copied Scene"; }
    inline juce::String pastedScene() { return "Pasted Scene"; }
    inline juce::String pastedSceneFloor() { return "Pasted Scene floor"; }

    // 9.4 item C: SYNC, rehomed from Scene+Y to Scene+O. Says what it threw away, because
    // that is the part you cannot get back -- the deviations were live-only.
    inline juce::String sceneSynced(int sceneNumber)
    {
        return "Synced to Scene " + juce::String(sceneNumber) + " -- deviations discarded";
    }

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

    inline juce::String captureArmedWaiting() { return "ARMED - capture starts on Play"; }

    inline juce::String captureDisarmedIdle() { return "Capture disarmed"; }

    inline juce::String captureDiscarded() { return "Take discarded"; }

    // ---- transport / meta ---------------------------------------------------

    inline juce::String panic() { return "Panic"; }
    inline juce::String quantized() { return "Quantized"; }
    inline juce::String cancelled() { return "Cancelled"; }
    inline juce::String transportReset() { return "Reset to start"; }

    // ---- clipboard / paste flow ---------------------------------------------

    // 9.29 -- the Machine scope's verbs (the sound, not the channel).
    inline juce::String copiedSound(const juce::String& machineId)
    {
        return "Copied sound (" + machineId + ")";
    }
    inline juce::String pastedSound(int track, const juce::String& machineId)
    {
        return "Pasted sound (" + machineId + ") to track " + juce::String(track + 1);
    }
    inline juce::String initedMachine(int track, const juce::String& machineId)
    {
        return "Init " + machineId + " on track " + juce::String(track + 1);
    }
    inline juce::String noSoundCopied() { return "No sound copied"; }

    inline juce::String nothingCopied() { return "Nothing copied"; }

    // 9.4 item A: the scope has no marks. Restore does NOTHING and says so; it used to
    // fall through to the project baseline, silently discarding everything since load.
    inline juce::String nothingToRestore() { return "Nothing to restore"; }

    // 9.4 item E: undo (Func+O). The scope noun is passed in (mapped from
    // CheckpointScope at the call site, mirroring markedTrack taking an int) so this
    // text header stays decoupled from the core model.
    inline juce::String nothingToUndo() { return "Nothing to undo"; }
    inline juce::String undid(const juce::String& scope) { return "Undid " + scope; }
    inline juce::String pastePickScope() { return "Paste: pick a scope"; }

    // ---- marks (9.4 item B) -------------------------------------------------
    //
    // "Mark" is DESIGN §13.6's term for an explicit snapshot -- the one you asked for,
    // as against the undo the system arms behind you. The depth is the scope's stack
    // depth after the push, so the message answers the question the pip will answer
    // too: how many deep am I? The scope is named because Y snapshots in EVERY scope
    // now, and the only thing telling them apart is the modifier under the other hand.
    inline juce::String markedSong(int depth)
    {
        return "Marked Song [" + juce::String(depth) + "]";
    }

    inline juce::String markedTrack(int track, int depth)
    {
        return "Marked Track " + juce::String(track + 1) + " [" + juce::String(depth) + "]";
    }

    inline juce::String markedPhrase(int depth)
    {
        return "Marked Phrase [" + juce::String(depth) + "]";
    }

    inline juce::String markedScene(int sceneNumber, int depth)
    {
        return "Marked Scene " + juce::String(sceneNumber) + " [" + juce::String(depth) + "]";
    }

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

    // ---- copy/paste discoverability (9.14 Stage 5) -------------------------
    //
    // The scaffolded copyHint()/pasteHint() that sat here were deleted when st.5 was
    // built. They had never had a caller, and they named the wrong key: COPY is on
    // Record (U) and PASTE on Play (I), while "P" is CONFIRM. Wiring them would have
    // shipped a frame that advertised a key it does not honour -- the exact defect
    // class 9.12 exists to prevent. The hint is now derived from the verb matrix in
    // InspectorModel (buildClipHint), where it cannot disagree with dispatch.

    inline juce::String copiedTrackWithScope(int track, bool allPhrases)
    {
        return allPhrases
            ? "Copied Track " + juce::String(track + 1) + " (all phrases)"
            : "Copied Track " + juce::String(track + 1) + " — current phrase";
    }

    inline juce::String pastedTrackWithSource(int track)
    {
        return "Pasted track clip -> Track " + juce::String(track + 1);
    }

} // namespace lockstep::status
