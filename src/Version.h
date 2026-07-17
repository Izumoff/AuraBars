#pragma once

// Single source of truth for the plugin's version number. Both the C++
// build (System tab display, OptionsFrame.cpp) and package.bat (which
// parses the "#define AURABARS_VERSION_STRING" line below to stamp
// ReadMe.txt) read this exact file, so the two can never drift apart -
// change the version in this one place only.
#define AURABARS_VERSION_STRING "1.1"

// Widens the narrow macro above into a wchar_t string literal without
// duplicating the version text anywhere.
#define AURABARS_VERSION_WSTR_PASTE(x) L##x
#define AURABARS_VERSION_WSTR_EXPAND(x) AURABARS_VERSION_WSTR_PASTE(x)
#define AURABARS_VERSION_WSTR AURABARS_VERSION_WSTR_EXPAND(AURABARS_VERSION_STRING)
