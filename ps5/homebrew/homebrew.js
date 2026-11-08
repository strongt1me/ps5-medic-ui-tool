// SPDX-License-Identifier: GPL-3.0-or-later
// Eintrag fuer den websrv-Homebrew-Launcher (ps5-payload-dev/websrv, GPL-3.0 -- nicht Teil dieses Projekts).
// websrv startet den ELF ueber seinen "Big App"-Lader: er laeuft dann mit App-Kontext, nur so oeffnet sich das Pad.
async function main() {
    return {
        mainText: "PS5 Medic",
        secondaryText: "Check-up und Reparatur fuer das Homebrew-Setup",
        onclick: async () => {
            return { path: window.workingDir + '/ps5medicui.elf' };
        }
    };
}
