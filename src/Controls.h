#pragma once

// Tasto dello Sneak sul pad. Dalla 1.2.1 il predefinito e' Freccia giu (il DLL sposta lo Sneak in memoria e libera L3 per le mosse);
// con SneakTasto = L3 il DLL non tocca la mappa dei tasti.
// Progetto PERSONALE (mashup per il mio gioco), non una release per altre persone.

#include <cstddef>
#include <cstdint>
#include <string>

namespace cdg::controls
{
	// Tasti del pad su cui si puo' mettere lo Sneak. "L3" e' quello originale del gioco.
	// Il valore salvato nell'INI ([Controlli] SneakTasto) e' uno dei nomi (senza distinzione maiuscole/minuscole).
	inline constexpr std::size_t kSneakCount = 8;
	inline constexpr const char* kSneakNames[kSneakCount] = { "L3", "giu", "su", "sinistra", "destra", "back", "start", "R3" };
	inline constexpr const char* kSneakLabels[kSneakCount] = {
		"L3 (originale: non tocco niente)", "Freccia giu", "Freccia su", "Freccia sinistra", "Freccia destra",
		"Back (toglie Wait)", "Start (toglie il Journal)", "R3 (toglie vista 1a/3a)"
	};

	// 1) rimette TUTTA la mappa dei tasti del pad (gioco) come era prima di ogni nostra modifica;
	// 2) se S.sneakPad non e' L3: mette lo Sneak su quel tasto e libera il tasto da altri comandi semplici.
	// Con L3 non cambia niente. Da chiamare sul thread principale (messaggi SKSE o AddTask).
	void Apply(const char* a_when);

	// Registra (una sola volta) il piccolo registro dei tasti premuti sul pad: nel log compare, per Freccia/Back/Start/L3/R3,
	// il comando che il gioco assegna al tasto premuto. Se lo Sneak e' stato spostato da noi, controlla anche
	// che il personaggio si accovacci davvero e, se il gioco non reagisce, lo fa partire il DLL (S.sneakAssist).
	void InstallPadLog();

	// Da chiamare ~30 volte al secondo sul thread di gioco
	void Tick();

	// Ultimo esito dell'aiuto allo Sneak, per il menu (vuoto se non e' mai servito)
	std::string LastNote();
}
