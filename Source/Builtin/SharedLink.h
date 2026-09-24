#pragma once

#include "LinkSettings.h"

#if JUCE_MAC
 #include <fcntl.h>
 #include <signal.h>
 #include <sys/file.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <sys/sysctl.h>
 #include <unistd.h>
#elif JUCE_WINDOWS
 #include <windows.h>
#endif

// One LinkSettings for the whole process, shared between separately built plugins.
//
// Every DAW_LAB plugin is its own binary, so a static in one of them is invisible to the others. What
// they do share is the host process, so the link lives in a small memory-mapped file named after that
// process (pid and start time); the first plugin to ask creates and initialises it, the rest map the same
// bytes. Nothing else touches it, so a plugin loaded in another DAW instance gets its own.
//
// The block is never unmapped (plugins come and go; the link has to outlive all of them), and files left
// by processes that no longer exist are removed on the way in. If the file can't be used (sandboxed host,
// or a plugin built from a different LinkSettings layout), acquire() hands back a private link instead, so
// that plugin still works, it just isn't connected to the others.
namespace SharedLink
{
    inline constexpr uint32_t layoutMagic = 0x44574C31;   // "DWL1": bump when LinkSettings changes layout

    struct alignas (64) Header   // padded so the LinkSettings behind it stays aligned for its atomics
    {
        uint32_t magic;
        uint32_t size;
        uint32_t ready;
    };

    inline constexpr size_t blockSize = (sizeof (Header) + sizeof (LinkSettings) + 4095) & ~(size_t) 4095;

   #if JUCE_MAC
    inline long processStartSeconds()
    {
        int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, (int) getpid() };
        struct kinfo_proc info {};
        size_t length = sizeof (info);
        return sysctl (mib, 4, &info, &length, nullptr, 0) == 0 ? (long) info.kp_proc.p_un.__p_starttime.tv_sec : 0L;
    }

    inline void removeStaleFiles (const juce::File& directory)
    {
        for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, "link-*"))
        {
            const auto pid = file.getFileName().fromFirstOccurrenceOf ("link-", false, false)
                                 .upToFirstOccurrenceOf ("-", false, false).getIntValue();

            if (pid > 0 && pid != (int) getpid() && kill ((pid_t) pid, 0) != 0 && errno == ESRCH)
                file.deleteFile();
        }
    }

    inline LinkSettings* mapSharedBlock()
    {
        // A fixed place: JUCE's temp directory on macOS is per application name, which differs for every plugin.
        const auto directory = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Caches/DAW_LAB/link");
        if (! directory.createDirectory().wasOk())
            return nullptr;

        removeStaleFiles (directory);

        const auto path = directory.getChildFile ("link-" + juce::String ((int) getpid()) + "-" + juce::String (processStartSeconds()));
        const int fd = open (path.getFullPathName().toRawUTF8(), O_RDWR | O_CREAT, 0600);
        if (fd < 0)
            return nullptr;

        flock (fd, LOCK_EX);   // two plugins loading at the same moment must not both initialise it

        LinkSettings* result = nullptr;
        struct stat status {};
        const bool fresh = fstat (fd, &status) == 0 && (size_t) status.st_size < blockSize;

        if (! fresh || ftruncate (fd, (off_t) blockSize) == 0)
        {
            void* memory = mmap (nullptr, blockSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

            if (memory != MAP_FAILED)
            {
                auto* header = static_cast<Header*> (memory);
                auto* slot = reinterpret_cast<LinkSettings*> (header + 1);

                if (fresh)
                {
                    new (slot) LinkSettings();
                    header->magic = layoutMagic;
                    header->size = (uint32_t) sizeof (LinkSettings);
                    header->ready = 1;
                }

                if (header->ready == 1 && header->magic == layoutMagic && header->size == (uint32_t) sizeof (LinkSettings))
                    result = slot;
                else
                    munmap (memory, blockSize);
            }
        }

        flock (fd, LOCK_UN);
        close (fd);
        return result;
    }
   #elif JUCE_WINDOWS
    // Windows named kernel objects (a mutex, a page-file-backed mapping) are reference-counted rather than
    // filesystem entries, so there is nothing to clean up: the OS drops them once every plugin that mapped
    // them has exited. A name scoped to this process ID is enough to keep separate host instances apart.
    inline LinkSettings* mapSharedBlock()
    {
        const auto name = "DAW_LAB_link_" + juce::String ((juce::int64) GetCurrentProcessId());

        HANDLE mutex = CreateMutexA (nullptr, FALSE, (name + "_mutex").toRawUTF8());
        if (mutex == nullptr)
            return nullptr;

        WaitForSingleObject (mutex, INFINITE);   // two plugins loading at the same moment must not both initialise it

        LinkSettings* result = nullptr;
        HANDLE mapping = CreateFileMappingA (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                             0, (DWORD) blockSize, name.toRawUTF8());
        const bool fresh = mapping != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;

        if (mapping != nullptr)
        {
            void* memory = MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, blockSize);

            if (memory != nullptr)
            {
                auto* header = static_cast<Header*> (memory);
                auto* slot = reinterpret_cast<LinkSettings*> (header + 1);

                if (fresh)
                {
                    new (slot) LinkSettings();
                    header->magic = layoutMagic;
                    header->size = (uint32_t) sizeof (LinkSettings);
                    header->ready = 1;
                }

                if (header->ready == 1 && header->magic == layoutMagic && header->size == (uint32_t) sizeof (LinkSettings))
                    result = slot;
                else
                    UnmapViewOfFile (memory);
            }

            // `mapping` (and `mutex` below) are deliberately never closed: they need to outlive every plugin
            // that mapped them, and Windows reclaims process handles automatically on exit.
        }

        ReleaseMutex (mutex);
        return result;
    }
   #endif

    // The process-wide link, or a private one when sharing isn't possible.
    inline std::shared_ptr<LinkSettings> acquire()
    {
       #if JUCE_MAC || JUCE_WINDOWS
        static LinkSettings* shared = mapSharedBlock();   // per binary; every binary maps the same bytes

        if (shared != nullptr)
            return std::shared_ptr<LinkSettings> (shared, [] (LinkSettings*) {});
       #endif

        return std::make_shared<LinkSettings>();
    }
}
