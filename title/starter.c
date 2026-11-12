// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * PS5 Medic als Kachel unter "Medien" (FAKE42007, Ordner-App fuer ShadowMount+).
 *
 * Der Starter ist ein winziger nativer PS5-Titel und braucht weder websrv noch einen Homebrew-Lader. Er tut nur eines: Er sendet
 * den Starter-Launcher (payload.elf, steckt per .incbin in dieser eboot.bin) an den ELF-Lader des Jailbreaks (127.0.0.1:9021),
 * genau wie das Senden einer ELF von Hand. Der Launcher startet danach die Vollbild-UI (ps5medicui.elf) mit App-Kontext (nur damit
 * oeffnet sich der DualSense), schliesst dabei diesen Starter und beendet sich selbst, wenn die UI endet. Der Starter selbst darf
 * weder /data lesen noch den Controller oeffnen, deshalb steckt die ganze Arbeit im Launcher und in der UI.
 *
 * Gebaut wie ein nativer Titel (title/build_title.sh, gleiche Kette wie die Kacheln von PS5 Cooling & System Center): Die PS5
 * verlangt einen echten Prozessparameter-Block und eine passende Signatur. Es werden nur Funktionen aus libkernel und
 * libSceSystemService benutzt, keine C-Bibliothek, daher -ffreestanding und kein memset/memcpy. Importiert werden duerfen nur die
 * Funktionen, die build_title.sh auflistet; alles andere waere zur Laufzeit NULL.
 */
#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#define PORT_LOADER 9021

/* libkernel / libSceSystemService (Stubs aus target/lib des SDK) */
int close(int fd);
int sceKernelUsleep(unsigned int microseconds);
int sceKernelDebugOutText(int channel, const char *text);
int sceKernelSendNotificationRequest(int device, void *request, size_t size, int blocking);
int sceSystemServiceLoadExec(const char *path, const char *const *argv);

/* der Starter-Launcher (ps5medic-start.elf); build_title.sh legt ihn als payload.elf neben diese Datei */
__asm__(".section .rodata\n"
        ".global payload_elf\n"
        "payload_elf:\n"
        ".incbin \"payload.elf\"\n"
        ".global payload_elf_end\n"
        "payload_elf_end:\n"
        ".previous\n");
extern const unsigned char payload_elf[], payload_elf_end[];

/* vom Linkerskript (title.ld); die PS5 nullt den BSS nicht */
extern char __bss_start[], __bss_end[];

static struct {
    char reserved[45];
    char message[3075];
} notify_request;

/* Meldung im Kernel-Log und als Benachrichtigung auf dem Bildschirm */
static void report(const char *msg) {
    volatile char *dst = notify_request.message;
    size_t i = 0;
    for (; msg[i] && i + 1 < sizeof(notify_request.message); i++) dst[i] = msg[i];
    dst[i] = 0;
    sceKernelDebugOutText(0, msg);
    sceKernelDebugOutText(0, "\n");
    sceKernelSendNotificationRequest(0, &notify_request, sizeof(notify_request), 0);
}

static int connect_local(int port) {
    struct sockaddr_in sa;
    volatile unsigned char *p = (volatile unsigned char *)&sa;
    for (size_t i = 0; i < sizeof(sa); i++) p[i] = 0;
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    /* nie unbegrenzt warten: sonst wuerde der Titel ohne Meldung haengen */
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(s, (const struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(s);
        return -1;
    }
    return s;
}

static int send_all(int s, const unsigned char *p, size_t n) {
    while (n) {
        ssize_t w = send(s, p, n, MSG_NOSIGNAL); /* Gegenstelle weg: Fehler statt SIGPIPE */
        if (w <= 0) return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* die eingebettete ELF an den Lader; 0 = gesendet. Wenige Versuche, falls der Lader gerade beschaeftigt ist. */
static int send_payload(void) {
    int s = -1;
    for (int i = 0; i < 5 && s < 0; i++) {
        s = connect_local(PORT_LOADER);
        if (s < 0) sceKernelUsleep(500 * 1000);
    }
    if (s < 0) {
        report("PS5 Medic: ELF-Lader (Port 9021) nicht erreichbar. Ist der Jailbreak geladen?");
        return -1;
    }
    const int rc = send_all(s, payload_elf, (size_t)(payload_elf_end - payload_elf));
    close(s);
    if (rc != 0) report("PS5 Medic: Senden an den ELF-Lader fehlgeschlagen.");
    return rc;
}

/* Einstieg des Titels. Er endet mit sceSystemServiceLoadExec("exit"): exit() loest in einem nativen Titel SIGSYS aus, und
 * sceSystemServiceKillApp wird fuer normale Apps verweigert. Vorher eine Wartezeit: Endet der Titel genau in dem Moment, in dem der
 * Launcher die UI startet, kann ShadowMount+ ihn noch fuer aktiv halten und spaetere Starts verweigern ("Device busy"). */
__attribute__((noreturn, visibility("default"))) void _start(void *process_parameters, void (*teardown)(void)) {
    (void)process_parameters;
    (void)teardown;
    for (volatile char *p = __bss_start; p < __bss_end; p++) *p = 0;
    if (send_payload() == 0) {
        /* Der Launcher schliesst diesen Starter beim Start der UI; kommt es nicht dazu, bleibt ein Hinweis. */
        sceKernelUsleep(40 * 1000 * 1000);
        report("PS5 Medic: Die Oberflaeche ist nicht erschienen. Protokoll: /data/ps5medic/launcher.log");
    }
    sceKernelUsleep(1500 * 1000);
    sceSystemServiceLoadExec("exit", NULL);
    for (;;) sceKernelUsleep(100 * 1000);
}
