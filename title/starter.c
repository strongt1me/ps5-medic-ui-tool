/*
 * PS5 Medic als Kachel unter "Medien" (FAKE42007).
 *
 * Der Starter ist ein winziger nativer PS5-Titel. Er tut nur eines: Er bittet websrv (Port 8080, Homebrew-Lader) per
 * /hbldr, die eigentliche Vollbild-UI (ps5medicui.elf) zu starten. websrvs Big-App-Lader gibt der UI einen App-Kontext
 * (nur damit oeffnet sich der DualSense) und keine Sandbox. Der Starter selbst darf weder /data lesen noch den Controller
 * oeffnen, deshalb steckt die ganze Arbeit in der UI.
 *
 * websrv schliesst beim Start eine laufende Big App, und das ist dieser Starter: Normalerweise endet der Prozess, bevor
 * die Antwort kommt. Kommt sie doch (200 = gestartet), beendet sich der Starter selbst. Laeuft websrv nicht, zeigt er einen
 * Hinweis und beendet sich.
 *
 * Gebaut wie ein nativer Titel (title/build_title.sh, gleiche Kette wie der BFpilot-Starter): Die PS5 verlangt einen
 * echten Prozessparameter-Block und eine passende Signatur. Es werden nur Funktionen aus libkernel und libSceSystemService
 * benutzt, keine C-Bibliothek, daher -ffreestanding und kein memset/memcpy. Importiert werden duerfen nur die Funktionen,
 * die build_title.sh auflistet; alles andere waere zur Laufzeit NULL oder wuerde den Titel verweigern lassen
 * (z. B. sceSystemServiceParamGetInt: CE-107750-0), deshalb bleiben die Meldungen deutsch.
 */
#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#define PORT_WEBSRV 8080

/* libkernel / libSceSystemService (Stubs aus target/lib des SDK) */
int close(int fd);
int sceKernelUsleep(unsigned int microseconds);
int sceKernelDebugOutText(int channel, const char *text);
int sceKernelSendNotificationRequest(int device, void *request, size_t size, int blocking);
int sceSystemServiceLoadExec(const char *path, const char *const *argv);

/* vom Linkerskript (title.ld); die PS5 nullt den BSS nicht */
extern char __bss_start[], __bss_end[];

static struct {
    char reserved[45];
    char message[3075];
} notify_request;

static size_t length(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

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

static int connect_local(int port, int timeout_s) {
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
    tv.tv_sec = timeout_s;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (connect(s, (const struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(s);
        return -1;
    }
    return s;
}

static int send_all(int s, const unsigned char *p, size_t n) {
    while (n) {
        ssize_t w = send(s, p, n, MSG_NOSIGNAL);   /* Gegenstelle weg: Fehler statt SIGPIPE */
        if (w <= 0) return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* GET /hbldr: Statuscode der Antwort (200 = App gestartet), -1 = keine lesbare Antwort. Der Pfad steht fest im Titel; die
 * UI und ihr Ordner kommen mit dem Release nach /data/homebrew/PS5MEDIC/. */
static int launch_ui(int s) {
    static const char req[] = "GET /hbldr?path=/data/homebrew/PS5MEDIC/ps5medicui.elf&cwd=/data/homebrew/PS5MEDIC HTTP/1.1\r\n"
                              "Host: 127.0.0.1:8080\r\nConnection: close\r\n\r\n";
    if (send_all(s, (const unsigned char *)req, length(req)) != 0) return -1;
    char buf[256];
    char head[16];
    size_t got = 0;
    for (;;) {   /* "HTTP/1.1 200 ..." - die ersten Zeichen merken, den Rest nur abholen */
        ssize_t r = recv(s, buf, sizeof(buf), 0);
        if (r <= 0) break;
        for (ssize_t i = 0; i < r && got < sizeof(head); i++) head[got++] = buf[i];
    }
    if (got < 12 || head[0] != 'H' || head[8] != ' ') return -1;
    int code = 0;
    for (int i = 9; i < 12; i++) {
        if (head[i] < '0' || head[i] > '9') return -1;
        code = code * 10 + (head[i] - '0');
    }
    return code;
}

static void run(void) {
    int s = connect_local(PORT_WEBSRV, 90);
    if (s < 0) {
        report("PS5 Medic: websrv (Port 8080) laeuft nicht. Bitte websrv starten und PS5 Medic erneut oeffnen.");
        return;
    }
    int code = launch_ui(s);
    close(s);
    if (code == 200) return;   /* gestartet; der Starter beendet sich unten */
    if (code < 0) {
        /* Normalfall: websrv hat den Starter als laufende Big App geschlossen, bevor es antworten konnte. Hierher kommt der
         * Prozess nur, wenn er den Abbruch ueberlebt hat; dann bleibt die UI aus oder sie startet gleich. */
        report("PS5 Medic: Keine Antwort von websrv. Falls die Oberflaeche nicht erscheint, websrv pruefen.");
        return;
    }
    report("PS5 Medic: websrv konnte die Oberflaeche nicht starten. Liegt ps5medicui.elf in /data/homebrew/PS5MEDIC/?");
}

/* Einstieg des Titels. Er endet mit sceSystemServiceLoadExec("exit"): exit() loest in einem nativen Titel SIGSYS aus, und
 * sceSystemServiceKillApp wird fuer normale Apps verweigert. */
__attribute__((noreturn, visibility("default"))) void _start(void *process_parameters, void (*teardown)(void)) {
    (void)process_parameters;
    (void)teardown;
    for (volatile char *p = __bss_start; p < __bss_end; p++) *p = 0;
    run();
    /* Kurz warten, bevor der Titel endet: Endet er genau in dem Moment, in dem websrv die UI startet, kann ShadowMount+ den
     * Titel noch fuer aktiv halten und spaetere Starts verweigern ("Device busy", erst ein Neustart hilft). */
    sceKernelUsleep(1500 * 1000);
    sceSystemServiceLoadExec("exit", NULL);
    for (;;) sceKernelUsleep(100 * 1000);
}
