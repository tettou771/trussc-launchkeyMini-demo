// =============================================================================
// main.cpp - Entry point
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.title = "tcxMidi - Launchkey Mini";
    settings.setSize(1000, 640);

    return TC_RUN_APP(tcApp, settings);
}
