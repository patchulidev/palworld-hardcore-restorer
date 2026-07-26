// palsave.hpp -- Palworld save decode/encode + targeted hardcore edit.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pal {
namespace fs = std::filesystem;

struct Status {
    bool ok = false;
    bool hardcore = false;
    std::string difficulty = "?";     // enum tail, e.g. "Custom"
    std::string deathPenalty = "?";   // enum tail, e.g. "None"
    std::string error;
};

// Where Palworld keeps single-player saves: %LOCALAPPDATA%\Pal\Saved\SaveGames
fs::path defaultSaveRoot();

// Folder next to the running .exe (where the backups/ folder is created).
fs::path exeDir();

// Every folder under `root` that contains a WorldOption.sav.
std::vector<fs::path> findWorlds(const fs::path& root);

// Read hardcore + difficulty + death penalty for one world folder.
Status readStatus(const fs::path& worldFolder);

// Flip bHardcore. On success writes WorldOption.sav (as PlZ) after backing up.
// Returns false + error on failure; changeSummary empty means "already in state".
bool setHardcore(const fs::path& worldFolder, bool enabled,
                 std::string& changeSummary, fs::path& backupPath, std::string& error);

// Restore the most recent backup this tool made for the world.
bool restoreLatest(const fs::path& worldFolder, fs::path& restoredFrom, std::string& error);
bool hasBackup(const fs::path& worldFolder);

// Best-effort: is the game currently running?
bool palworldRunning();

}  // namespace pal
