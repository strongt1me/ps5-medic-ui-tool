// SPDX-License-Identifier: GPL-3.0-or-later
// Entwicklungswerkzeug (kein Teil des Produkts): startet einen Titel wie ein Druck auf seine Kachel, damit der Weg Kachel ->
// Starter -> Launcher -> UI ohne Fernseher und Controller pruefbar ist. Titel-ID in /data/ps5medic/launch_title.txt
// (Standard FAKE42007); Ergebnis in /data/ps5medic/launch_title.log. Senden: nc <ip> 9021 < build-ps5/ps5medic-launch-title.elf
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <ps5/kernel.h>

extern "C" {
typedef struct app_launch_ctx {
    uint32_t structsize;
    uint32_t user_id;
    uint32_t app_opt;
    uint64_t crash_report;
    uint32_t check_flag;
} app_launch_ctx_t;
int sceUserServiceInitialize(void*);
int sceUserServiceGetForegroundUser(uint32_t* user_id);
int sceSystemServiceLaunchApp(const char* title_id, char** argv, app_launch_ctx_t* ctx);
}

int main() {
    std::FILE* log = std::fopen("/data/ps5medic/launch_title.log", "w");
    char title[32] = "FAKE42007";
    if (std::FILE* f = std::fopen("/data/ps5medic/launch_title.txt", "r")) {
        char line[32] = {0};
        if (std::fgets(line, sizeof line, f) && std::strlen(line) >= 9) {
            std::memcpy(title, line, 9);
            title[9] = 0;
        }
        std::fclose(f);
    }
    sceUserServiceInitialize(nullptr);
    kernel_set_ucred_authid(-1, 0x4801000000000013ULL);
    app_launch_ctx_t ctx = {};
    const int u = sceUserServiceGetForegroundUser(&ctx.user_id);
    const int rc = sceSystemServiceLaunchApp(title, nullptr, &ctx);
    if (log) {
        std::fprintf(log, "GetForegroundUser rc=%d user=%u; LaunchApp(%s) rc=0x%x\n", u, ctx.user_id, title, static_cast<unsigned>(rc));
        std::fclose(log);
    }
    return 0;
}
