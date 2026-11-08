// Gemeinsame Test-Hilfen (kein Fremd-Framework).
#pragma once
#include <iostream>

extern int g_fail, g_pass;

#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (cond) ++g_pass;                                                \
        else {                                                             \
            ++g_fail;                                                      \
            std::cerr << "FAIL: " << (msg) << "  [" #cond "]" << "\n";     \
        }                                                                  \
    } while (0)

void fix_tests();  // tests/test_fixes.cpp
void ui_tests();   // tests/test_ui.cpp
void report_tests();  // tests/test_report.cpp
