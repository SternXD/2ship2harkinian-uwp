#pragma once

#ifdef _UWP
#include <string>

// Makes sure an mm.o2r exists for UWP builds. Returns true on success.
bool EnsureO2rPresentUwp(const std::string& appShortName);
#endif

