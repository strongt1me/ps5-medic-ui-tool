// SPDX-License-Identifier: GPL-3.0-or-later
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
void update_tests();  // tests/test_update.cpp
void sign_tests();    // tests/test_sign.cpp
void error_code_tests();  // tests/test_error_codes.cpp
void codes_update_tests();  // tests/test_codes_update.cpp
void text_policy_tests();   // tests/test_text_policy.cpp
