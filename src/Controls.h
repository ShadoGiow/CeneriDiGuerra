#pragma once

// Tasti del pad letti/cambiati direttamente nel gioco (senza dipendere dal file controlmap.txt).
// Progetto PERSONALE (mashup per il mio gioco), non una release per altre persone.

#include <cstddef>
#include <cstdint>

namespace cdg::controls
{
	// Tasti del pad su cui si puo' mettere lo Sneak. "L3" e' quello originale del gioco.
	// Il valore salvato nell'INI ([Controlli] SneakPad) e' uno dei nomi (senza distinzione maiuscole/minuscole).
	inline constexpr std::size_t kSneakCount = 8;
	inline constexpr const char* kSneakNames[kSneakCount] = { "L3", "giu", "su", "sinistra", "destra", "back", "start", "R3" };
	inline constexpr const char* kSneakLabels[kSneakCount] = {
		"L3 (originale)", "Freccia giu", "Freccia su", "Freccia sinistra", "Freccia destra",
		"Back (toglie Wait)", "Start (toglie il Journal)", "R3 (toglie vista 1a/3a)"
	};

	// Scrive nel log lo stato dei tasti del pad e mette lo Sneak sul tasto scelto in S.sneakPad
	// (liberando quel tasto da altri comandi semplici; quelli liberati prima vengono rimessi).
	// Da chiamare sul thread principale (messaggi SKSE o AddTask).
	void Apply(const char* a_when);

	// Registra (una sola volta) il piccolo registro dei tasti premuti sul pad: nel log compare, per Freccia/Back/Start/L3/R3,
	// quale comando il gioco assegna al tasto premuto. Serve a capire perche' un tasto non fa quello che deve.
	void InstallPadLog();

	// Numero (cdg::InputId) del tasto del pad scelto per lo Sneak in S.sneakPad (0 = nome non valido)
	std::uint16_t TargetInputId();

	// Da chiamare ~30 volte al secondo sul thread di gioco: modo "Sneak diretto" (S.sneakDirect)
	void Tick();
}
