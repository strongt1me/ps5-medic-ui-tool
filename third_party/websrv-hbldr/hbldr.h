/* Copyright (C) 2024 John Törnblom

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#pragma once

#include <unistd.h>


/* PS5 Medic (12. Nov. 2026): optionale Pruefung direkt vor dem Schliessen der laufenden Big App. Liefert sie 0, wird die App nicht
   geschlossen und hbldr_launch scheitert mit errno EBUSY (die App kann sich zwischen der Pruefung des Aufrufers und dem Kill geaendert haben). */
extern int (*hbldr_kill_guard)(int app_id);

/* PS5 Medic: != 0 laesst den Syscall sys_set_budget(0) (0x23b) beim Start der Big App weg (siehe docs/AUDIT.md). */
extern int hbldr_skip_budget;

/* PS5 Medic: Testhebel fuer elfldr_raise_privileges (nur Diagnose der PS-Taste): Bit 0 = uid nicht auf 0 setzen, Bit 1 = Faehigkeiten nicht setzen. */
extern int hbldr_raise_skip;

pid_t hbldr_launch(const char* cwd, const char* path, int stdio, char** argv,
		   char** envp);

