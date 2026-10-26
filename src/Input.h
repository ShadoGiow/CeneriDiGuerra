#pragma once

// Lettura dei tasti (pad XInput + tastiera/mouse) con un thread proprio.
// Tre slot: ognuno ha una combinazione e un tempo di pressione (0 = istantaneo).
// Le combinazioni piu' grandi hanno la precedenza su quelle contenute (es. LT+RT esclude LT lungo).

#include "Settings.h"

namespace cdg::input
{
	void Start();
	void Stop();

	// bit i = lo slot i e' stato attivato dall'ultima chiamata (consumati dal thread di gioco)
	unsigned TakeSlots();

	// true se almeno un tasto di una combinazione degli slot e' premuto adesso
	bool SlotKeysHeld();
	// true se il tasto dato e' premuto adesso (pad/tastiera)
	bool IsHeld(InputId a_id);

	// registrazione di una nuova combinazione dal menu (a_slot = 0..2)
	void        BeginCapture(int a_slot);
	void        CancelCapture();
	int         CaptureSlot();   // -1 se non sta registrando
	std::string CaptureLive();   // testo dei tasti premuti in questo momento
	std::string PressedNames();  // tasti premuti adesso (per il registro), es. "Pad_LB + Pad_RB"

	bool PadConnected();
}
