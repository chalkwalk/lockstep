// Throwaway ΔE audit harness (plan Stage A1). NOT shipped.
//
// Resolves each co-occurring on-screen state to its fill ARGB (values mirrored
// from UITheme.h + compatColour()/groupForCell()) and prints the pairwise Oklab
// ΔE within each set, flagging pairs below a legibility threshold. Build:
//   g++ -std=c++20 -I../../src/controller audit.cpp -o audit && ./audit

#include "Oklab.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using lockstep::oklab::Lab;
using lockstep::oklab::distanceSq;
using lockstep::oklab::packedRgbToOklab;

namespace {

constexpr float kThreshold = 0.10f;   // first-cut legibility floor

struct Swatch { std::string name; uint32_t argb; };

// Flatten ARGB over black (premultiply alpha) → opaque RGB, as the Push sees it.
uint32_t flatten(uint32_t argb) {
  const float a = ((argb >> 24) & 0xFF) / 255.0f;
  const int r = int(((argb >> 16) & 0xFF) * a);
  const int g = int(((argb >> 8) & 0xFF) * a);
  const int b = int((argb & 0xFF) * a);
  return uint32_t(r << 16 | g << 8 | b);
}

float deltaE(uint32_t x, uint32_t y) {
  return std::sqrt(distanceSq(packedRgbToOklab(flatten(x)),
                              packedRgbToOklab(flatten(y))));
}

void audit(const char* title, const std::vector<Swatch>& s) {
  std::printf("\n=== %s ===\n", title);
  for (size_t i = 0; i < s.size(); ++i)
    for (size_t j = i + 1; j < s.size(); ++j) {
      const float d = deltaE(s[i].argb, s[j].argb);
      const bool weak = d < kThreshold;
      std::printf("  %-18s vs %-18s  ΔE=%.3f%s\n", s[i].name.c_str(),
                  s[j].name.c_str(), d, weak ? "   <-- WEAK" : "");
    }
}

}  // namespace

int main() {
  // --- Step grid (body fills; overlays are border/dot, catalogued separately) ---
  audit("Step grid", {
    {"Empty",         0xFF2D3741},  // kStepInactive
    {"TrigCertain",   0xFF50B478},  // kStepActive
    {"TrigSuppress",  0xFF304838},
    {"FillAdd",       0xFFF08030},  // kStepFillAdd
    {"FillSuppress",  0xFF3060A0},  // kStepFillSuppress
    {"OutOfRange",    0xFF1C2026},  // kStepOutRange
  });

  // TrigProbable shares kStepActive, dimmed via `level` (~0.5). Same-hue dim is
  // intentional, but check it stays apart from Empty.
  audit("Step trig vs probable-dim vs empty", {
    {"Empty",         0xFF2D3741},
    {"Certain",       0xFF50B478},
    {"Probable~0.5",  0xFF285A3C},  // kStepActive at ~half brightness
  });

  audit("Mute viewer", {
    {"Muted",   0xFFC03030},  // kScopeMute
    {"Audible", 0xFF2D3741},  // kStepInactive
  });

  audit("Selectors", {
    {"Current",   0xFFFFFFFF},
    {"Occupied",  0xFF8898A8},  // kScopeStep
    {"Empty",     0xFF404040},
    {"OutRange",  0xFF1C2026},  // kStepOutRange
    {"Next",      0xFF7050C8},  // kScopePhrase
    {"Chain",     0xFF7050C8},  // kScopePhrase (== Next)
  });

  audit("Machine picker", {
    {"Current",     0xFFFFFFFF},
    {"Available",   0xFF50C030},  // kScopeMachine
    {"Unavailable", 0xFF1C2026},  // kStepOutRange
  });

  audit("NoteEdit", {
    {"Active",  0xFF2888D8},  // kScopeNoteEdit
    {"Staged",  0xFFDC643C},
    {"Other",   0xFF2888D8},  // kScopeNoteEdit (== Active)
    {"Resting", 0xFF1C2026},  // kStepOutRange
  });

  audit("Length", {
    {"InRun",    0xFF7050C8},  // kScopePhrase
    {"Boundary", 0xFFCCAAFF},
    {"OutRun",   0xFF1C2026},  // kStepOutRange
  });

  audit("Modifier scope hues (active)", {
    {"Func/amber",   0xFFC07800},
    {"Track/cyan",   0xFF30A0C0},
    {"Scene/green",  0xFF20A060},
    {"Phrase/indigo",0xFF7050C8},
    {"Song/gold",    0xFFB88800},
    {"Morph/magenta",0xFFBE3898},
    {"Mute/red",     0xFFC03030},
    {"Fill/chartr",  0xFF82C018},
  });

  audit("Modifier scope hues (resting dim)", {
    {"Func",   0xFF2A1A04},
    {"Track",  0xFF133A46},
    {"Scene",  0xFF0D3C24},
    {"Phrase", 0xFF221448},
    {"Song",   0xFF403000},
    {"Morph",  0xFF3C1230},
    {"Mute",   0xFF3E0E0E},
    {"Fill",   0xFF283C08},
  });

  audit("Verb on-active", {
    {"Idle",     0xFF2A3C50},  // kVerbActive
    {"Record",   0xFFA03030},  // kVerbRecActive
    {"Play",     0xFF208040},  // kVerbPlayActive
    {"Clear",    0xFF905020},  // kVerbClearActive
    {"Snapshot", 0xFF3A44A0},  // kVerbSnapActive
    {"Overdub",  0xFFD2821E},  // kVerbODActive
  });

  audit("Section modes", {
    {"Inactive", 0xFF0E2020},  // kSecInactive
    {"Active",   0xFF206060},  // kSecActive
    {"Machine",  0xFF50C030},  // kScopeMachine
    {"NoteEdit", 0xFF2888D8},  // kScopeNoteEdit
    {"Master",   0xFF404010},
  });

  std::printf("\n(threshold ΔE < %.2f flagged WEAK)\n", kThreshold);
  return 0;
}
