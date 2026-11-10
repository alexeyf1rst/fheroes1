#include <cstring>
#include <PLATFORM/GameData.h>

#include <PLATFORM/File.h>
#include <PLATFORM/Platform.h>

#include <algorithm>
#include <filesystem>

namespace platform {

namespace {

std::string gScenarioPack;

bool ResolvesUnder(const std::string& directory, const char* path) {
    char resolved[FILE_PATH_CAPACITY];
    std::string saved = FileRoot();
    FileSetRoot(directory.c_str());
    bool found = FileResolve(path, FILE_OPEN_READ, resolved, sizeof(resolved));
    FileSetRoot(saved.c_str());
    return found;
}

}  // namespace

std::string FindGameRoot(const std::string& requested, std::vector<std::string>& searched) {
    searched.clear();
    std::string argument = ConfiguredDirectory(requested);
    if (!argument.empty())
        searched.push_back(argument);
    std::string environment = EnvironmentDirectory("HOMM1_DATA");
    if (!environment.empty())
        searched.push_back(environment);
    searched.push_back(ExecutableDirectory());
    searched.push_back(".");
#if defined(_WIN32)
    std::string local = EnvironmentDirectory("LOCALAPPDATA");
    if (!local.empty())
        searched.push_back(local + "\\homm1-buka\\game");
#else
    std::string xdg = EnvironmentDirectory("XDG_DATA_HOME");
    std::string home = EnvironmentDirectory("HOME");
    if (!xdg.empty())
        searched.push_back(xdg + "/homm1-buka/game");
    if (!home.empty())
        searched.push_back(home + "/.local/share/homm1-buka/game");
#endif
    for (const std::string& candidate : searched) {
        if (ResolvesUnder(candidate, "DATA\\HEROES.AGG"))
            return candidate;
    }
    return std::string();
}

std::string FindCdRoot(const std::string& gameRoot) {
    std::vector<std::string> candidates;
    std::string environment = EnvironmentDirectory("HOMM1_CD");
    if (!environment.empty())
        candidates.push_back(environment);
    candidates.push_back(gameRoot + "/../cd");
    candidates.push_back(gameRoot);
    for (const std::string& candidate : candidates) {
        if (ResolvesUnder(candidate, "TRACKS\\02-AudioTrack 02.ogg"))
            return candidate;
    }
    return std::string();
}

const std::vector<std::string>& ScenarioPacks() {
    static const auto packs = [] {
        std::vector<std::string> result;
        char path[FILE_PATH_CAPACITY];
        if (!FileResolve("EXPANSIONS", FILE_OPEN_READ, path, sizeof(path)))
            return result;
        std::error_code error;
        namespace fs = std::filesystem;
        fs::directory_iterator current(fs::path(std::u8string(path, path + std::strlen(path))), fs::directory_options::skip_permission_denied, error);
        const fs::directory_iterator end;
        for (; !error && current != end; current.increment(error)) {
            const auto utf8 = current->path().filename().u8string();
            const std::string name(utf8.begin(), utf8.end());
            if (name.size() > 64 || name.find_first_of("\r\n") != std::string::npos)
                continue;
            FileFindData data;
            const std::string pattern = "EXPANSIONS/" + name + "/MAPS/*.MAP";
            const i32 find = FileFindFirst(pattern.c_str(), &data);
            if (find == FILE_INVALID)
                continue;
            FileFindClose(find);
            result.push_back(name);
        }
        std::sort(result.begin(), result.end());
        if (result.size() > 16)
            result.resize(16);
        return result;
    }();
    return packs;
}

const std::string& SelectedScenarioPack() {
    return gScenarioPack;
}

void SetScenarioPack(const std::string& name) {
    const auto& packs = ScenarioPacks();
    gScenarioPack = std::find(packs.begin(), packs.end(), name) != packs.end() ? name : std::string();
}

std::string ScenarioDirectory() {
    return gScenarioPack.empty() ? ".\\MAPS\\" : "EXPANSIONS/" + gScenarioPack + "/MAPS/";
}

}  // namespace platform
