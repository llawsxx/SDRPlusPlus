#include <gui/widgets/folder_select.h>
#include <regex>
#include <filesystem>
#include <gui/file_dialogs.h>
#include <core.h>
#ifdef __ANDROID__
#include <android_backend.h>
#endif

FolderSelect::FolderSelect(std::string defaultPath) {
    root = (std::string)core::args["root"];
    setPath(defaultPath);
}

bool FolderSelect::render(std::string id) {
    bool _pathChanged = false;
#ifdef __ANDROID__
    if (dialogOpen) {
        std::string result = backend::recordingDirectoryResult();
        if (!result.empty()) {
            dialogOpen = false;
            if (result.rfind("content://", 0) == 0) { setPath(result, true); }
            else if (result != "cancelled") { selectionError = result; }
        }
    }
#endif
    float menuColumnWidth = ImGui::GetContentRegionAvail().x;

    float buttonWidth = ImGui::CalcTextSize("...").x + 20.0f;
    bool lastPathValid = pathValid;
    if (!lastPathValid) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    }
    ImGui::SetNextItemWidth(menuColumnWidth - buttonWidth);
    if (ImGui::InputText(id.c_str(), strPath, 2047,
#ifdef __ANDROID__
        ImGuiInputTextFlags_ReadOnly
#else
        ImGuiInputTextFlags_None
#endif
    )) {
        path = std::string(strPath);
        std::string expandedPath = expandString(strPath);
        if (!std::filesystem::is_directory(expandedPath)) {
            pathValid = false;
        }
        else {
            pathValid = true;
            _pathChanged = true;
        }
    }
    if (!lastPathValid) {
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (ImGui::Button(("..." + id + "_winselect").c_str(), ImVec2(buttonWidth - 8.0f, 0)) && !dialogOpen) {
        dialogOpen = true;
#ifdef __ANDROID__
        selectionError.clear();
        backend::chooseRecordingDirectory();
#else
        if (workerThread.joinable()) { workerThread.join(); }
        workerThread = std::thread(&FolderSelect::worker, this);
#endif
    }

    _pathChanged |= pathChanged;
    pathChanged = false;
    if (!selectionError.empty()) { ImGui::TextWrapped("%s", selectionError.c_str()); }
    return _pathChanged;
}

void FolderSelect::setPath(std::string path, bool markChanged) {
    this->path = path;
    std::string expandedPath = expandString(path);
    pathValid = std::filesystem::is_directory(expandedPath);
#ifdef __ANDROID__
    if (path.rfind("content://", 0) == 0) { pathValid = backend::canWriteRecordingDirectory(path); }
#endif
    if (markChanged) { pathChanged = true; }
    snprintf(strPath, sizeof(strPath), "%s", path.c_str());
}

std::string FolderSelect::expandString(std::string input) {
    if (input.rfind("content://", 0) == 0) { return input; }
    input = std::regex_replace(input, std::regex("%ROOT%"), root);
    return std::regex_replace(input, std::regex("//"), "/");
}

bool FolderSelect::pathIsValid() {
    return pathValid;
}

void FolderSelect::worker() {
    auto fold = pfd::select_folder("Select Folder", pathValid ? std::filesystem::path(expandString(path)).parent_path().string() : "");
    std::string res = fold.result();

    if (res != "") {
        path = res;
        strcpy(strPath, path.c_str());
        pathChanged = true;
    }

    pathValid = std::filesystem::is_directory(expandString(path));
    dialogOpen = false;
}
