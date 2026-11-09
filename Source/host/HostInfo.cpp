// windows.h comes first, before any JUCE header, as in JUCE's own Windows sources.
#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #pragma comment(lib, "version.lib")
#endif

#include "HostInfo.h"

#include <vector>

namespace sl::host
{
    juce::String HostInfo::summary() const
    {
        juce::String s = name;
        if (version.isNotEmpty())
            s << " " << version;
        if (format.isNotEmpty())
            s << " (" << format << ")";
        return s;
    }

    juce::String executableVersion (const juce::File& program)
    {
       #ifdef _WIN32
        const juce::String full = program.getFullPathName();
        const wchar_t* path = full.toWideCharPointer();
        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeW (path, &ignored);
        if (size == 0)
            return {};

        std::vector<unsigned char> data ((std::size_t) size);
        if (! GetFileVersionInfoW (path, 0, size, data.data()))
            return {};

        VS_FIXEDFILEINFO* info = nullptr;
        UINT length = 0;
        if (! VerQueryValueW (data.data(), L"\\", reinterpret_cast<LPVOID*> (&info), &length) || info == nullptr
            || length < sizeof (VS_FIXEDFILEINFO))
            return {};

        return juce::String ((int) HIWORD (info->dwFileVersionMS)) + "." + juce::String ((int) LOWORD (info->dwFileVersionMS)) + "."
             + juce::String ((int) HIWORD (info->dwFileVersionLS)) + "." + juce::String ((int) LOWORD (info->dwFileVersionLS));
       #else
        juce::ignoreUnused (program);
        return {};
       #endif
    }

    HostInfo detectHost()
    {
        const juce::PluginHostType type;
        HostInfo h;
        h.name = type.getHostDescription();
        h.path = juce::PluginHostType::getHostPath();
        h.isCubase = type.isCubase();
        h.isSteinberg = type.isSteinberg();
        h.format = juce::AudioProcessor::getWrapperTypeDescription (juce::PluginHostType::getPluginLoadedAs());
        if (h.path.isNotEmpty())
            h.version = executableVersion (juce::File (h.path));
        return h;
    }
}
