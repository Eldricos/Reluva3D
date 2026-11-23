// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The common dialogs' Open and Save (comdlg32.dll), shown as the system's
// own, and ShellExecuteA's web pages (docs/common/x86/windows.md).

#include "emulation/x86/win32.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace reluva::x86 {

namespace {

constexpr uint32_t kOpenFileNameSize = 0x4c; // OPENFILENAMEA, as Windows 95 and NT 4 had it
constexpr uint32_t kCdErrStructSize = 0x0001;
constexpr uint32_t kFnErrBufferTooSmall = 0x3003;
constexpr uint32_t kShellExecuteDone = 42; // any number above 32 says it was done
constexpr uint32_t kSeErrAccessDenied = 5;
constexpr uint32_t kSeErrNoAssociation = 31;

std::string lowerCase(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A web address as a browser takes it: Windows took "http:\\host" too.
std::string webAddress(std::string target) {
    std::replace(target.begin(), target.end(), '\\', '/');
    const std::string low = lowerCase(target);
    if (low.rfind("http://", 0) == 0 || low.rfind("https://", 0) == 0)
        return target;
    if (low.rfind("www.", 0) == 0)
        return "http://" + target;
    return {};
}

} // namespace

void Win32Process::addShellStandIns() {
    const char *d = "comdlg32.dll";
    // GetOpenFileNameA, GetSaveFileNameA(OPENFILENAMEA*): the player picks a
    // file (chooseFile); its folder becomes drive X:, so the program can
    // read or write it, and the program's buffer gets X:\name. The
    // program's hook procedure and template, if it has them, aren't used.
    auto fileDialog = [this](Cpu &c, bool save) -> uint32_t {
        _dialogError = 0;
        const uint32_t p = c.arg(0);
        if (!c.valid(p, kOpenFileNameSize) || c.read32(p) < kOpenFileNameSize) {
            _dialogError = kCdErrStructSize;
            return 0;
        }
        FileRequest request;
        request.save = save;
        if (const uint32_t title = c.read32(p + 48))
            request.title = c.string(title);
        // The filter: pairs of strings, a name and its patterns, ending in
        // an empty one.
        for (uint32_t at = c.read32(p + 12); at && request.filters.size() < 64;) {
            const std::string name = c.string(at);
            if (name.empty())
                break;
            at += uint32_t(name.size()) + 1;
            const std::string patterns = c.string(at);
            at += uint32_t(patterns.size()) + 1;
            request.filters.emplace_back(name, patterns);
        }
        const uint32_t buffer = c.read32(p + 28);
        const uint32_t size = c.read32(p + 32);
        if (buffer && size) {
            const std::string given = c.string(buffer, size);
            request.name =
                given.substr(given.find_last_of("\\/:") == std::string::npos ? 0 : given.find_last_of("\\/:") + 1);
        }
        const auto picked = chooseFile ? chooseFile(request) : std::nullopt;
        if (!picked)
            return 0; // cancelled: CommDlgExtendedError says 0
        std::string name = picked->filename().string();
        // Saving, a name without an extension takes the program's own.
        const uint32_t defaultExtension = c.read32(p + 60);
        if (save && defaultExtension && name.find('.') == std::string::npos && !c.string(defaultExtension).empty())
            name += "." + c.string(defaultExtension);
        Drive x;
        x.letter = 'X';
        x.root = picked->parent_path();
        x.writable = x.root;
        x.label = "PICKED";
        addDrive(x);
        const std::string path = "X:\\" + name;
        if (!buffer || size < path.size() + 1) {
            if (buffer && size >= 2)
                c.write16(buffer, uint16_t(path.size() + 1)); // the size it needs
            _dialogError = kFnErrBufferTooSmall;
            return 0;
        }
        c.writeString(buffer, path, uint32_t(path.size() + 1));
        const size_t dot = name.rfind('.');
        c.write16(p + 56, 3); // nFileOffset: after "X:\"
        c.write16(p + 58, uint16_t(dot == std::string::npos ? 0 : 3 + dot + 1));
        const uint32_t title = c.read32(p + 36);
        const uint32_t titleSize = c.read32(p + 40);
        if (title && titleSize > name.size())
            c.writeString(title, name, uint32_t(name.size() + 1));
        return 1;
    };
    stdcall(d, "GetOpenFileNameA", 1, [fileDialog](Cpu &c) { return fileDialog(c, false); });
    stdcall(d, "GetSaveFileNameA", 1, [fileDialog](Cpu &c) { return fileDialog(c, true); });
    stdcall(d, "CommDlgExtendedError", 0, [this](Cpu &) { return _dialogError; });

    // ShellExecuteA(window, verb, file, parameters, folder, show): a web
    // page opened, if the player agrees (openUrl); anything else isn't
    // run, as no program but this one is there to run.
    stdcall("SHELL32.dll", "ShellExecuteA", 6, [this](Cpu &c) {
        const std::string verb = c.arg(1) ? lowerCase(c.string(c.arg(1))) : "open";
        const std::string file = c.arg(2) ? c.string(c.arg(2)) : std::string();
        const std::string url = verb == "open" ? webAddress(file) : std::string();
        if (url.empty()) {
            if (output)
                output("ShellExecuteA " + verb + " " + file + ": nothing to run it with\n");
            return kSeErrNoAssociation;
        }
        return openUrl && openUrl(url) ? kShellExecuteDone : kSeErrAccessDenied;
    });
}

} // namespace reluva::x86
