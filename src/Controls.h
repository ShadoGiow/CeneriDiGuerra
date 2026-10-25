#pragma once

// Tasti del pad letti/cambiati direttamente nel gioco (senza dipendere dal file controlmap.txt).
// Progetto PERSONALE (mashup per il mio gioco), non una release per altre persone.

namespace cdg::controls
{
	// Scrive nel log lo stato dei tasti del pad (Sneak, L3, Freccia giu') e, se S.sneakOnDpadDown e' attivo,
	// sposta lo Sneak su Freccia giu' (liberando L3). Se e' spento, rimette lo Sneak dov'era.
	// Da chiamare sul thread principale (messaggi SKSE o AddTask).
	void Apply(const char* a_when);
}
