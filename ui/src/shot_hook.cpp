// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic_ui/shot_hook.hpp"

#include "medic_ui/png.hpp"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <system_error>

namespace medic::ui {

bool serve_shot_request(const Canvas& canvas, const std::string& request, const std::string& folder, std::string* written) {
    // Die Anforderung entfernen ist die Pruefung: ein einziger Systemaufruf, der ohne Anforderung scheitert.
    if (::unlink(request.c_str()) != 0) return false;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    ::chmod(folder.c_str(), 0777);  // Payload und FTP muessen die Dateien entfernen koennen
    int files = 0;
    for (const auto& e : std::filesystem::directory_iterator(folder, ec)) {
        (void)e;
        ++files;
    }
    if (ec || files >= kShotMaxFiles) return false;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string path = folder + "/medic-" + std::to_string(ms) + ".png";
    if (!write_png(path, canvas, 1)) return false;
    ::chmod(path.c_str(), 0666);
    if (written) *written = path;
    return true;
}

}  // namespace medic::ui
