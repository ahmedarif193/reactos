/*
 * PROJECT:     ReactOS Automatic Testing Utility
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Class for managing all the configuration parameters
 * COPYRIGHT:   Copyright 2009 Colin Finck (colin@reactos.org)
 */

class CConfiguration
{
private:
    bool m_CrashRecovery;
    bool m_IsInteractive;
    bool m_IsReactOS;
    bool m_PrintToConsole;
    unsigned long m_RepeatCount;
    bool m_Shutdown;
    bool m_Submit;
    bool m_ListModules;
    bool m_ListCategories;
    vector<string> m_Categories;
    string m_Comment;
    wstring m_Module;
    string m_Test;

    string m_AuthenticationRequestString;
    string m_SystemInfoRequestString;

    void SelectCategories(const string& Names);

public:
    CConfiguration();
    void ParseParameters(int argc, wchar_t* argv[]);
    void GetSystemInformation();
    void GetConfigurationFromFile();

    bool DoCrashRecovery() const { return m_CrashRecovery; }
    bool DoPrint() const { return m_PrintToConsole; }
    bool DoShutdown() const { return m_Shutdown; }
    bool DoSubmit() const { return m_Submit; }
    bool IsInteractive() const { return m_IsInteractive; }
    bool IsReactOS() const { return m_IsReactOS; }
    unsigned long GetRepeatCount() const { return m_RepeatCount; }
    bool ListModulesOnly() const { return m_ListModules; }
    bool ListCategoriesOnly() const { return m_ListCategories; }
    const vector<string>& GetCategories() const { return m_Categories; }
    const string& GetComment() const { return m_Comment; }
    const wstring& GetModule() const { return m_Module; }
    const string& GetTest() const { return m_Test; }

    const string& GetAuthenticationRequestString() const { return m_AuthenticationRequestString; }
    const string& GetSystemInfoRequestString() const { return m_SystemInfoRequestString; }
};
