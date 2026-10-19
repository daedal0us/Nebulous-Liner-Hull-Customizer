// Small entry point shipped at the package root. A .lnk cannot do this job:
// Windows stores an absolute target path, and the relative path it also records
// is not used to rescue a missing absolute one, so a shortcut created on the
// build machine dies as soon as the archive is extracted somewhere else.
#include <windows.h>
#include <shellapi.h>

#include <string>

namespace {

std::wstring directoryOf(const std::wstring &path)
{
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

} // namespace

int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    wchar_t module[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return 1;

    const std::wstring root = directoryOf(std::wstring(module, length));
    if (root.empty())
        return 1;

    const std::wstring target = root + L"\\bin\\NLHC.exe";
    if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr,
                    L"bin\\NLHC.exe was not found next to this program.\n"
                    L"Extract the whole archive before starting it.",
                    L"Liner Hull Customizer", MB_ICONERROR | MB_OK);
        return 2;
    }

    const std::wstring workingDirectory = root + L"\\bin";
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", target.c_str(), nullptr,
                                           workingDirectory.c_str(), SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32 ? 0 : 3;
}
