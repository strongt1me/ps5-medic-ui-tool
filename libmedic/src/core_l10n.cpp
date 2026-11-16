// SPDX-License-Identifier: GPL-3.0-or-later
// Französische, italienische und spanische Fassung der englischen Meldungen der Fix-Engine (Notizen, Hinweise, Warnungen, Fehler beim Anwenden und
// Rückgängigmachen). Ausgangspunkt ist der englische Text (bei den deutschen Meldungen nach core_message_en); Pfade, Namen und Zahlen bleiben stehen.
// tests/test_review4.cpp prüft, dass jede englische Meldung und jede Notiz in fixes.cpp/engine.cpp hier vorkommt.
#include "medic/core_l10n.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace medic {
namespace {

struct Entry {
    const char* en;
    const char* fr;
    const char* it;
    const char* es;
};

const Entry kEntries[] = {
    // ---- Notizen und Hinweise der Fixes ----
    {"Remove parentheses from the file name", "Retirer les parenthèses du nom de fichier", "Rimuovere le parentesi dal nome del file", "Quitar los paréntesis del nombre del archivo"},
    {"Rename the file to the expected letter case (", "Mettre le nom du fichier dans la casse attendue (", "Portare il nome del file alle maiuscole/minuscole previste (",
     "Poner el nombre del archivo en las mayúsculas/minúsculas esperadas ("},
    {"Only the name changes (same folder); undo restores the old name.", "Seul le nom change (même dossier) ; annuler rétablit l'ancien nom.",
     "Cambia solo il nome (stessa cartella); annullare ripristina il vecchio nome.", "Solo cambia el nombre (misma carpeta); deshacer restaura el nombre anterior."},
    {"ShadowMount+ accepts at most the value ", "ShadowMount+ n'accepte au maximum que la valeur ", "ShadowMount+ accetta al massimo il valore ", "ShadowMount+ acepta como máximo el valor "},
    {"; games on level ", " ; les jeux du niveau ", "; i giochi al livello ", "; los juegos del nivel "},
    {" are still not found afterwards. Move them closer to the scan path.", " restent introuvables ensuite. Rapprochez-les du chemin d'analyse.",
     " restano introvabili anche dopo. Spostali più vicino al percorso di scansione.", " seguirán sin encontrarse después. Acércalos a la ruta de análisis."},
    {" (backup first)", " (sauvegarde d'abord)", " (prima il backup)", " (antes, copia de seguridad)"},
    {"Change ", "Modifier ", "Modificare ", "Cambiar "},
    {"config.ini: move ", "config.ini : transférer ", "config.ini: trasferire ", "config.ini: pasar "},
    {" flat v1 line(s) into their v2 sections", " ligne(s) v1 à plat dans leurs sections v2", " riga/righe v1 piatte nelle sezioni v2", " línea(s) v1 planas a sus secciones v2"},
    {" more v1 line(s) have no v2 equivalent or an invalid value and stay; set them in the ShadowMount+ web interface.",
     " autre(s) ligne(s) v1 n'ont pas d'équivalent v2 ou ont une valeur invalide et restent ; réglez-les dans l'interface web de ShadowMount+.",
     " altra/e riga/righe v1 non hanno un equivalente v2 o hanno un valore non valido e restano; impostale nell'interfaccia web di ShadowMount+.",
     " línea(s) v1 más no tienen equivalente v2 o tienen un valor no válido y se quedan; defínelas en la interfaz web de ShadowMount+."},
    {"Autoload list: insert the delay !4000 after elfldr (original kept in the backup)", "Liste d'autoload : insérer le délai !4000 après elfldr (original conservé dans la sauvegarde)",
     "Elenco di autoload: inserire l'attesa !4000 dopo elfldr (originale conservato nel backup)", "Lista de autocarga: insertar la espera !4000 tras elfldr (original guardado en la copia de seguridad)"},
    {"BackPork keeps running until the next restart. Afterwards it is no longer loaded.", "BackPork continue de tourner jusqu'au prochain redémarrage. Ensuite il n'est plus chargé.",
     "BackPork continua a funzionare fino al prossimo riavvio. Dopo non viene più caricato.", "BackPork sigue funcionando hasta el próximo reinicio. Después ya no se carga."},
    {"Remove the BackPork entry from the autoload list (original kept in the backup)", "Retirer l'entrée BackPork de la liste d'autoload (original conservé dans la sauvegarde)",
     "Rimuovere la voce BackPork dall'elenco di autoload (originale conservato nel backup)", "Quitar la entrada de BackPork de la lista de autocarga (original guardado en la copia de seguridad)"},
    {"Autoload list: remove ", "Liste d'autoload : supprimer ", "Elenco di autoload: rimuovere ", "Lista de autocarga: quitar "},
    {" duplicate line(s)", " ligne(s) en double", " riga/righe duplicata/e", " línea(s) duplicada(s)"},
    {"The first line of each entry stays; delays and all other lines stay unchanged.", "La première ligne de chaque entrée reste ; les délais et toutes les autres lignes restent inchangés.",
     "La prima riga di ogni voce resta; le attese e tutte le altre righe restano invariate.", "La primera línea de cada entrada se queda; las esperas y todas las demás líneas no cambian."},
    {"Autoload list: correct the spelling of ", "Liste d'autoload : corriger l'orthographe de ", "Elenco di autoload: correggere la grafia di ", "Lista de autocarga: corregir la ortografía de "},
    {"Autoload list: comment out ", "Liste d'autoload : mettre en commentaire ", "Elenco di autoload: commentare ", "Lista de autocarga: comentar "},
    {" name(s)", " nom(s)", " nome/i", " nombre(s)"},
    {", comment out ", ", mettre en commentaire ", ", commentare ", ", comentar "},
    {", remove ", ", supprimer ", ", rimuovere ", ", quitar "},
    {" entry/entries without a file", " entrée(s) sans fichier", " voce/i senza file", " entrada(s) sin archivo"},
    {" (original kept in the backup)", " (original conservé dans la sauvegarde)", " (originale conservato nel backup)", " (original guardado en la copia de seguridad)"},
    {"Payload files are not touched. If a payload is really missing, it has to be installed again by hand.",
     "Les fichiers payload ne sont pas touchés. Si un payload manque vraiment, il faut le réinstaller à la main.",
     "I file payload non vengono toccati. Se un payload manca davvero, va reinstallato a mano.",
     "Los archivos de payload no se tocan. Si de verdad falta un payload, hay que volver a instalarlo a mano."},
    {"Move the broken ELF file (", "Déplacer le fichier ELF défectueux (", "Spostare il file ELF difettoso (", "Mover el archivo ELF defectuoso ("},
    {") into the folder .medic_broken: ", ") dans le dossier .medic_broken : ", ") nella cartella .medic_broken: ", ") a la carpeta .medic_broken: "},
    {"The file is not deleted but moved to .medic_broken next to it (undo brings it back). Then transfer the payload again (FTP in binary mode).",
     "Le fichier n'est pas supprimé mais déplacé à côté dans .medic_broken (annuler le ramène). Retransférez ensuite le payload (FTP en mode binaire).",
     "Il file non viene eliminato ma spostato accanto in .medic_broken (annullare lo riporta). Poi trasferisci di nuovo il payload (FTP in modalità binaria).",
     "El archivo no se borra, sino que se mueve a .medic_broken a su lado (deshacer lo devuelve). Después vuelve a transferir el payload (FTP en modo binario)."},
    {"Move the empty or interrupted image into the folder .medic_broken: ", "Déplacer l'image vide ou interrompue dans le dossier .medic_broken : ",
     "Spostare l'immagine vuota o interrotta nella cartella .medic_broken: ", "Mover la imagen vacía o interrumpida a la carpeta .medic_broken: "},
    {"The file is not deleted but moved to .medic_broken next to it (undo brings it back). If a transfer is running, let it finish first.",
     "Le fichier n'est pas supprimé mais déplacé à côté dans .medic_broken (annuler le ramène). Si un transfert est en cours, laissez-le d'abord se terminer.",
     "Il file non viene eliminato ma spostato accanto in .medic_broken (annullare lo riporta). Se è in corso un trasferimento, lascialo prima finire.",
     "El archivo no se borra, sino que se mueve a .medic_broken a su lado (deshacer lo devuelve). Si hay una transferencia en curso, déjala terminar antes."},
    {"Move the leftover of an interrupted job into the folder .medic_broken: ", "Déplacer le reste d'une opération interrompue dans le dossier .medic_broken : ",
     "Spostare il residuo di un'operazione interrotta nella cartella .medic_broken: ", "Mover el resto de una operación interrumpida a la carpeta .medic_broken: "},
    {"The files are not deleted but moved to .medic_broken next to them (undo brings them back). If a transfer or a packing job is running, let it finish first.",
     "Les fichiers ne sont pas supprimés mais déplacés à côté dans .medic_broken (annuler les ramène). Si un transfert ou un empaquetage est en cours, laissez-le d'abord se terminer.",
     "I file non vengono eliminati ma spostati accanto in .medic_broken (annullare li riporta). Se è in corso un trasferimento o un'operazione di impacchettamento, lascialo prima finire.",
     "Los archivos no se borran, sino que se mueven a .medic_broken a su lado (deshacer los devuelve). Si hay una transferencia o un empaquetado en curso, déjalo terminar antes."},
    {"Move the half-finished pack folder into the folder .medic_broken: ", "Déplacer le dossier de paquet inachevé dans le dossier .medic_broken : ",
     "Spostare la cartella di pacchetto incompleta nella cartella .medic_broken: ", "Mover la carpeta de paquete a medias a la carpeta .medic_broken: "},
    {"The folder is not deleted but moved as a whole into .medic_broken in the scan path (undo brings it back). If it is still being written (a transfer or a pack job is running), this stops the job: let it finish first.",
     "Le dossier n'est pas supprimé mais déplacé en entier dans .medic_broken du chemin d'analyse (annuler le ramène). S'il est encore en cours d'écriture (transfert ou empaquetage en cours), cela interrompt l'opération : laissez-la d'abord se terminer.",
     "La cartella non viene eliminata ma spostata per intero in .medic_broken nel percorso di scansione (annullare la riporta). Se è ancora in scrittura (trasferimento o impacchettamento in corso), l'operazione si interrompe: lasciala prima finire.",
     "La carpeta no se borra, sino que se mueve entera a .medic_broken en la ruta de análisis (deshacer la devuelve). Si aún se está escribiendo (hay una transferencia o un empaquetado en curso), esto detiene la operación: déjala terminar antes."},
    {"Only the line AUTOLOAD_ENABLED in the settings file of the Payload Manager is set to 1; undo restores the file.",
     "Seule la ligne AUTOLOAD_ENABLED du fichier de réglages du Payload Manager est mise à 1 ; annuler rétablit le fichier.",
     "Viene impostata a 1 solo la riga AUTOLOAD_ENABLED nel file delle impostazioni del Payload Manager; annullare ripristina il file.",
     "Solo se pone a 1 la línea AUTOLOAD_ENABLED del archivo de ajustes del Payload Manager; deshacer restaura el archivo."},
    {": move duplicate source (keeping ", " : déplacer la source en double (conservée : ", ": spostare la sorgente duplicata (si mantiene ", ": mover la fuente duplicada (se conserva "},
    {"The duplicates go to .duplicates in the scan path (ShadowMount+ skips folders starting with a dot); undo moves them back.",
     "Les doublons vont dans .duplicates du chemin d'analyse (ShadowMount+ ignore les dossiers commençant par un point) ; annuler les remet.",
     "I duplicati vanno in .duplicates nel percorso di scansione (ShadowMount+ salta le cartelle che iniziano con un punto); annullare li rimette a posto.",
     "Los duplicados van a .duplicates en la ruta de análisis (ShadowMount+ omite las carpetas que empiezan por punto); deshacer los devuelve."},
    {"Images are not modified. Game files must sit directly in the image root (sce_sys/param.json at the root, no extra folder): rebuild the image.",
     "Les images ne sont pas modifiées. Les fichiers du jeu doivent se trouver directement à la racine de l'image (sce_sys/param.json à la racine, sans dossier supplémentaire) : reconstruisez l'image.",
     "Le immagini non vengono modificate. I file del gioco devono stare direttamente nella radice dell'immagine (sce_sys/param.json nella radice, senza cartella aggiuntiva): ricostruisci l'immagine.",
     "Las imágenes no se modifican. Los archivos del juego deben estar directamente en la raíz de la imagen (sce_sys/param.json en la raíz, sin carpeta extra): reconstruye la imagen."},

    // ---- Warnungen und Fehler der Engine ----
    {"The permissions changed since the preview (please check again): ", "Les droits ont changé depuis l'aperçu (veuillez vérifier à nouveau) : ",
     "I permessi sono cambiati dopo l'anteprima (controlla di nuovo): ", "Los permisos han cambiado desde la vista previa (comprueba de nuevo): "},
    {"The permissions are not readable (symlink or special file?): ", "Les droits ne sont pas lisibles (lien symbolique ou fichier spécial ?) : ",
     "I permessi non sono leggibili (collegamento simbolico o file speciale?): ", "Los permisos no se pueden leer (¿enlace simbólico o archivo especial?): "},
    {"No program file without execute permission found in the allowed folders", "Aucun fichier programme sans droit d'exécution trouvé dans les dossiers autorisés",
     "Nessun file di programma senza permesso di esecuzione trovato nelle cartelle consentite", "No se encontró ningún archivo de programa sin permiso de ejecución en las carpetas permitidas"},
    {"No leftovers of interrupted jobs found in the allowed folders", "Aucun reste d'opérations interrompues trouvé dans les dossiers autorisés",
     "Nessun residuo di operazioni interrotte trovato nelle cartelle consentite", "No se encontraron restos de operaciones interrumpidas en las carpetas permitidas"},
    {"No half-finished pack folder found in the allowed folders", "Aucun dossier de paquet inachevé trouvé dans les dossiers autorisés",
     "Nessuna cartella di pacchetto incompleta trovata nelle cartelle consentite", "No se encontró ninguna carpeta de paquete a medias en las carpetas permitidas"},
    {"No image name that is too long found in the allowed folders", "Aucun nom d'image trop long trouvé dans les dossiers autorisés",
     "Nessun nome di immagine troppo lungo trovato nelle cartelle consentite", "No se encontró ningún nombre de imagen demasiado largo en las carpetas permitidas"},
    {"Skipped (the name cannot be shortened): ", "Ignoré (le nom ne peut pas être raccourci) : ", "Saltato (il nome non si può accorciare): ", "Omitido (el nombre no se puede acortar): "},
    {"The permissions were changed after the fix: ", "Les droits ont été modifiés après la réparation : ", "I permessi sono stati modificati dopo la riparazione: ",
     "Los permisos se cambiaron después de la reparación: "},
    {"Restoring the permissions failed: ", "Le rétablissement des droits a échoué : ", "Il ripristino dei permessi non è riuscito: ", "No se pudieron restaurar los permisos: "},
    {"Setting the permissions failed: ", "La définition des droits a échoué : ", "L'impostazione dei permessi non è riuscita: ", "No se pudieron establecer los permisos: "},
    {"Skipped (permissions not readable): ", "Ignoré (droits non lisibles) : ", "Saltato (permessi non leggibili): ", "Omitido (permisos no legibles): "},
    {"files: the rest follows after the next check-up", "fichiers : le reste suit après le prochain diagnostic", "file: il resto segue dopo il prossimo check-up",
     "archivos: el resto sigue tras el próximo chequeo"},
    {"Permissions not readable: ", "Droits non lisibles : ", "Permessi non leggibili: ", "Permisos no legibles: "},
    {"Invalid target mode: ", "Mode cible invalide : ", "Modalità di destinazione non valida: ", "Modo de destino no válido: "},
    {"File does not exist: ", "Le fichier n'existe pas : ", "Il file non esiste: ", "El archivo no existe: "},
    {"More than ", "Plus de ", "Più di ", "Más de "},
    {"No autoload list found (path still to be verified on the console)", "Aucune liste d'autoload trouvée (chemin encore à vérifier sur la console)",
     "Nessun elenco di autoload trovato (percorso ancora da verificare sulla console)", "No se encontró ninguna lista de autocarga (ruta aún por verificar en la consola)"},
    {"Skipped (the list with the delays would exceed 4095 bytes): ", "Ignoré (la liste avec les délais dépasserait 4095 octets) : ",
     "Saltato (l'elenco con le attese supererebbe 4095 byte): ", "Omitido (la lista con las esperas superaría los 4095 bytes): "},
    {"The file changed since the preview (please check again): ", "Le fichier a changé depuis l'aperçu (veuillez vérifier à nouveau) : ",
     "Il file è cambiato dopo l'anteprima (controlla di nuovo): ", "El archivo ha cambiado desde la vista previa (comprueba de nuevo): "},
    {" line(s) changed since the check-up: please check again", " ligne(s) ont changé depuis le diagnostic : veuillez vérifier à nouveau",
     " riga/righe sono cambiate dopo il check-up: controlla di nuovo", " línea(s) han cambiado desde el chequeo: comprueba de nuevo"},
    {"Transaction had failed and was already rolled back: ", "La transaction avait échoué et a déjà été annulée : ", "La transazione era fallita ed è già stata annullata: ",
     "La transacción había fallado y ya se revirtió: "},
    {"No active BackPork entry found in the autoload lists", "Aucune entrée BackPork active trouvée dans les listes d'autoload",
     "Nessuna voce BackPork attiva trovata negli elenchi di autoload", "No se encontró ninguna entrada de BackPork activa en las listas de autocarga"},
    {"No flat v1 line with a known v2 equivalent found", "Aucune ligne v1 à plat avec un équivalent v2 connu trouvée",
     "Nessuna riga v1 piatta con un equivalente v2 noto trovata", "No se encontró ninguna línea v1 plana con equivalente v2 conocido"},
    {"Undo incomplete (trying again is possible): ", "Annulation incomplète (un nouvel essai est possible) : ", "Annullamento incompleto (si può riprovare): ",
     "Deshacer incompleto (se puede volver a intentar): "},
    {"smp_set_key: no unambiguous value can be derived from the facts", "smp_set_key : aucune valeur univoque ne peut être déduite des faits",
     "smp_set_key: dai fatti non si può ricavare un valore univoco", "smp_set_key: no se puede deducir un valor inequívoco de los datos"},
    {"No broken ELF file found in the allowed folders", "Aucun fichier ELF défectueux trouvé dans les dossiers autorisés",
     "Nessun file ELF difettoso trovato nelle cartelle consentite", "No se encontró ningún archivo ELF defectuoso en las carpetas permitidas"},
    {"Newly created file could not be set aside: ", "Le fichier nouvellement créé n'a pas pu être mis de côté : ", "Il file appena creato non si è potuto mettere da parte: ",
     "No se pudo apartar el archivo recién creado: "},
    {"No entry found that can be corrected safely", "Aucune entrée trouvée qui puisse être corrigée en toute sécurité", "Nessuna voce trovata che si possa correggere in sicurezza",
     "No se encontró ninguna entrada que se pueda corregir con seguridad"},
    {"smp_disable_conflicting_backpork: invalid pattern", "smp_disable_conflicting_backpork : motif invalide", "smp_disable_conflicting_backpork: modello non valido",
     "smp_disable_conflicting_backpork: patrón no válido"},
    {"No empty image found in the allowed folders", "Aucune image vide trouvée dans les dossiers autorisés", "Nessuna immagine vuota trovata nelle cartelle consentite",
     "No se encontró ninguna imagen vacía en las carpetas permitidas"},
    {"No place found where a delay is missing", "Aucun endroit trouvé où un délai manque", "Nessun punto trovato in cui manca un'attesa", "No se encontró ningún lugar donde falte una espera"},
    {" operations readable) -- nothing is changed: ", " opérations lisibles) -- rien n'est modifié : ", " operazioni leggibili) -- non viene modificato nulla: ",
     " operaciones legibles) -- no se cambia nada: "},
    {"The source does not have the expected spelling: ", "La source n'a pas l'orthographe attendue : ", "La sorgente non ha la grafia prevista: ", "La fuente no tiene la ortografía esperada: "},
    {"Backup path is outside the backup folder: ", "Le chemin de sauvegarde est en dehors du dossier de sauvegarde : ", "Il percorso del backup è fuori dalla cartella dei backup: ",
     "La ruta de la copia de seguridad está fuera de la carpeta de copias: "},
    {"Skipped (outside the allowed folders): ", "Ignoré (en dehors des dossiers autorisés) : ", "Saltato (fuori dalle cartelle consentite): ", "Omitido (fuera de las carpetas permitidas): "},
    {"Something is now at the original location: ", "Il y a maintenant quelque chose à l'emplacement d'origine : ", "Ora c'è qualcosa nella posizione originale: ",
     "Ahora hay algo en la ubicación original: "},
    {"Transaction was already undone: ", "La transaction a déjà été annulée : ", "La transazione è già stata annullata: ", "La transacción ya se deshizo: "},
    {"Checksum after the move does not match: ", "La somme de contrôle après le déplacement ne correspond pas : ", "Il checksum dopo lo spostamento non corrisponde: ",
     "La suma de comprobación tras mover no coincide: "},
    {"This finding has no automatic fix", "Ce résultat n'a pas de réparation automatique", "Questo rilevamento non ha una riparazione automatica", "Este hallazgo no tiene reparación automática"},
    {"No write access to this file system", "Pas d'accès en écriture à ce système de fichiers", "Nessun accesso in scrittura a questo file system", "Sin acceso de escritura a este sistema de archivos"},
    {"Not confirmed: nothing was changed", "Non confirmé : rien n'a été modifié", "Non confermato: non è stato modificato nulla", "No confirmado: no se cambió nada"},
    {"File not readable, no backup possible: ", "Fichier non lisible, aucune sauvegarde possible : ", "File non leggibile, backup impossibile: ",
     "Archivo no legible, copia de seguridad imposible: "},
    {"Skipped (target already exists): ", "Ignoré (la cible existe déjà) : ", "Saltato (la destinazione esiste già): ", "Omitido (el destino ya existe): "},
    {"Skipped (invalid value for v2): ", "Ignoré (valeur invalide pour v2) : ", "Saltato (valore non valido per v2): ", "Omitido (valor no válido para v2): "},
    {" unreadable journal line(s) skipped", " ligne(s) de journal illisible(s) ignorée(s)", " riga/righe del registro illeggibile/i saltata/e", " línea(s) de registro ilegible(s) omitida(s)"},
    {"Journal could not be written", "Le journal n'a pas pu être écrit", "Non è stato possibile scrivere il registro", "No se pudo escribir el registro"},
    {"Internal error while undoing: ", "Erreur interne lors de l'annulation : ", "Errore interno durante l'annullamento: ", "Error interno al deshacer: "},
    {"Skipped (no free target name): ", "Ignoré (aucun nom de cible libre) : ", "Saltato (nessun nome di destinazione libero): ", "Omitido (ningún nombre de destino libre): "},
    {"Backup could not be created: ", "La sauvegarde n'a pas pu être créée : ", "Non è stato possibile creare il backup: ", "No se pudo crear la copia de seguridad: "},
    {"config.ini already has the values", "config.ini contient déjà les valeurs", "config.ini contiene già i valori", "config.ini ya tiene los valores"},
    {"Journal exists but is not readable", "Le journal existe mais n'est pas lisible", "Il registro esiste ma non è leggibile", "El registro existe pero no se puede leer"},
    {"File was changed after the fix: ", "Le fichier a été modifié après la réparation : ", "Il file è stato modificato dopo la riparazione: ", "El archivo se modificó después de la reparación: "},
    {"Backup is missing or was changed: ", "La sauvegarde est manquante ou a été modifiée : ", "Il backup manca o è stato modificato: ", "Falta la copia de seguridad o se modificó: "},
    {"smp_set_key: parameter 'key' is missing", "smp_set_key : le paramètre « key » manque", "smp_set_key: manca il parametro 'key'", "smp_set_key: falta el parámetro 'key'"},
    {"Restoring failed: ", "Le rétablissement a échoué : ", "Il ripristino non è riuscito: ", "La restauración falló: "},
    {"The plan contains no changes", "Le plan ne contient aucune modification", "Il piano non contiene modifiche", "El plan no contiene cambios"},
    {"Skipped (name would be empty): ", "Ignoré (le nom serait vide) : ", "Saltato (il nome sarebbe vuoto): ", "Omitido (el nombre quedaría vacío): "},
    {"Renaming back failed: ", "Le renommage inverse a échoué : ", "Il ritorno al nome precedente non è riuscito: ", "No se pudo volver al nombre anterior: "},
    {"Internal error while applying: ", "Erreur interne lors de l'application : ", "Errore interno durante l'applicazione: ", "Error interno al aplicar: "},
    {"No duplicate line found", "Aucune ligne en double trouvée", "Nessuna riga duplicata trovata", "No se encontró ninguna línea duplicada"},
    {" (changes rolled back)", " (modifications annulées)", " (modifiche annullate)", " (cambios revertidos)"},
    {"Verification failed: ", "La vérification a échoué : ", "La verifica non è riuscita: ", "La comprobación falló: "},
    {"Path not in the whitelist", "Chemin absent de la liste blanche", "Percorso non presente nella lista consentita", "Ruta que no está en la lista permitida"},
    {" | Rollback incomplete: ", " | Annulation incomplète : ", " | Ripristino incompleto: ", " | Reversión incompleta: "},
    {"Renaming failed: ", "Le renommage a échoué : ", "La ridenominazione non è riuscita: ", "No se pudo renombrar: "},
    {" -- restore from the history", " -- à restaurer depuis l'historique", " -- da ripristinare dalla cronologia", " -- restáuralo desde el historial"},
    {"Writing failed: ", "L'écriture a échoué : ", "La scrittura non è riuscita: ", "La escritura falló: "},
    {" already has the value", " contient déjà la valeur", " contiene già il valore", " ya tiene el valor"},
    {"Invalid transaction ID", "ID de transaction invalide", "ID transazione non valido", "ID de transacción no válido"},
    {"File is missing at the target: ", "Le fichier est absent à la cible : ", "Il file manca nella destinazione: ", "Falta el archivo en el destino: "},
    {"Source does not exist: ", "La source n'existe pas : ", "La sorgente non esiste: ", "La fuente no existe: "},
    {"Target already exists: ", "La cible existe déjà : ", "La destinazione esiste già: ", "El destino ya existe: "},
    {"config.ini not readable", "config.ini non lisible", "config.ini non leggibile", "config.ini no legible"},
    {"Path appears more than once in the plan: ", "Le chemin apparaît plusieurs fois dans le plan : ", "Il percorso compare più volte nel piano: ", "La ruta aparece varias veces en el plan: "},
    {"Unknown transaction: ", "Transaction inconnue : ", "Transazione sconosciuta: ", "Transacción desconocida: "},
    {"Journal incomplete (", "Journal incomplet (", "Registro incompleto (", "Registro incompleto ("},
    {"Invalid timestamp", "Horodatage invalide", "Data e ora non valide", "Marca de tiempo no válida"},
    {"Blocked system path", "Chemin système bloqué", "Percorso di sistema bloccato", "Ruta del sistema bloqueada"},
    {"Source not readable: ", "Source non lisible : ", "Sorgente non leggibile: ", "Fuente no legible: "},
    {"File not readable: ", "Fichier non lisible : ", "File non leggibile: ", "Archivo no legible: "},
    {"Unknown fix: ", "Réparation inconnue : ", "Riparazione sconosciuta: ", "Reparación desconocida: "},
    {"Nothing to do: ", "Rien à faire : ", "Niente da fare: ", "Nada que hacer: "},
    {"Invalid path", "Chemin invalide", "Percorso non valido", "Ruta no válida"},
    {"File is missing: ", "Le fichier manque : ", "Il file manca: ", "Falta el archivo: "},
};

const std::vector<const Entry*>& sorted_entries() {
    static const std::vector<const Entry*> v = [] {
        std::vector<const Entry*> r;
        for (const auto& e : kEntries) r.push_back(&e);
        std::stable_sort(r.begin(), r.end(), [](const Entry* a, const Entry* b) { return std::string(a->en).size() > std::string(b->en).size(); });
        return r;
    }();
    return v;
}

}  // namespace

bool core_l10n_has_entry(const std::string& en_fragment) {
    for (const auto& e : kEntries)
        if (en_fragment == e.en) return true;
    return false;
}

std::string core_text_l10n(const std::string& en, const std::string& lang) {
    if (lang != "fr" && lang != "it" && lang != "es") return en;
    // Fragmente der Reihe nach (längste zuerst) ersetzen. Damit eine schon übersetzte Stelle nicht erneut getroffen wird, wird mit Platzhaltern gearbeitet.
    std::string out = en;
    std::vector<std::string> done;
    for (const Entry* e : sorted_entries()) {
        const std::string from = e->en;
        const char* to_c = lang == "fr" ? e->fr : lang == "it" ? e->it : e->es;
        for (std::size_t pos = out.find(from); pos != std::string::npos; pos = out.find(from, pos + 1)) {
            const std::string mark = std::string("\x01") + std::to_string(done.size()) + "\x02";
            done.emplace_back(to_c);
            out.replace(pos, from.size(), mark);
            pos += mark.size() - 1;
        }
    }
    for (std::size_t i = done.size(); i-- > 0;) {
        const std::string mark = std::string("\x01") + std::to_string(i) + "\x02";
        for (std::size_t pos = out.find(mark); pos != std::string::npos; pos = out.find(mark, pos + done[i].size())) out.replace(pos, mark.size(), done[i]);
    }
    return out;
}

}  // namespace medic
