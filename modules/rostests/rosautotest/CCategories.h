/*
 * PROJECT:     LiberNT Automatic Testing Utility
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test categories selected with the /g option
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

class CCategories
{
public:
    static const char* Find(const string& Program, const string& Test);
    static bool IsKnown(const string& Name);
    static bool WantsProgram(const vector<string>& Selected, const string& Program);
    static bool WantsTest(const vector<string>& Selected, const string& Program, const string& Test);
    static string Describe();
};
