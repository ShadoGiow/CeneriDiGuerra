#pragma once

// Impostazioni di Ceneri di Guerra: tutte modificabili dal menu F1 e salvate nel file INI.
// Progetto PERSONALE (mashup per il mio gioco), non una release per altre persone.

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace cdg
{
	// Un tasto/pulsante e' un numero: 1..21 = pulsanti del pad (20 = LT, 21 = RT), 0x100+VK = tastiera/mouse.
	using InputId = std::uint16_t;

	constexpr int kSlots = 3;

	constexpr InputId kPadLB = 9;
	constexpr InputId kPadRB = 10;
	constexpr InputId kPadLT = 20;
	constexpr InputId kPadRT = 21;

	struct Binding
	{
		std::vector<InputId> keys;          // combinazione: tutti questi tasti devono essere premuti insieme
		float                hold{ 0.0f };  // secondi di pressione richiesti (0 = istantaneo)

		[[nodiscard]] bool empty() const noexcept { return keys.empty(); }
	};

	struct Settings
	{
		float iniVersion{ 1.0f };

		// --- generale
		bool enabled{ true };           // interruttore generale
		bool requireDrawn{ true };      // le mosse partono solo con l'arma estratta
		bool allow2HAxe{ false };       // consente anche asce/martelli a due mani
		bool allowAnyWeapon{ false };   // consente qualunque arma (le clip sostituite sono pensate per lo spadone)
		bool showHud{ true };           // messaggi a schermo

		// --- avvio della mossa
		char  triggerEvents[256]{ "attackPowerStart_Sprint,attackPowerStartSprint" };  // eventi provati nell'ordine
		float evalTimeout{ 0.60f };     // attesa massima che OAR confermi la clip prima di provare l'evento successivo
		float stopDelay{ 0.07f };       // pausa fra i "stop" iniziali e l'evento d'attacco
		float maxSeconds{ 8.0f };       // durata massima di sicurezza di una mossa
		float testDelay{ 2.0f };        // secondi di attesa dopo "Prova" dal menu (per chiudere il menu)
		bool  autoChain{ true };        // le mosse a piu' stadi avanzano da sole se premi di nuovo nella finestra giusta
		bool  lockInput{ true };        // durante la mossa blocca attacco/parata/rinfodero (non rinfodera mai l'arma)

		// --- costi
		float staminaCost{ 0.0f };      // stamina consumata a ogni mossa (0 = niente)
		float cooldown{ 0.0f };         // secondi di attesa fra una mossa e la successiva (0 = nessuna)

		// --- slot (tasti + mossa assegnata per codice, es. "056", "S001", "A902")
		Binding bind[kSlots];
		char    moveCode[kSlots][32]{};

		// --- diagnostica
		bool logEvents{ true };
	};

	extern Settings   S;
	extern std::mutex gBindMx;  // protegge bind[], moveCode[] e gNames

	// nomi scelti dall'utente per le mosse senza nome: codice (minuscolo) -> nome
	extern std::map<std::string, std::string> gNames;

	void LoadSettings();
	void SaveSettings();
	void RequestSave();   // salva a breve (dal thread principale)
	bool SaveIfDirty();   // chiamata periodica

	std::string GetMoveCode(int a_slot);
	void        SetMoveCode(int a_slot, const std::string& a_code);

	std::string BindingToString(const Binding& a_b);
	bool        BindingFromString(const std::string& a_s, Binding& a_out);
	std::string BindingLabel(const Binding& a_b);  // testo leggibile per il menu

	std::string InputName(InputId a_id);
	bool        InputFromName(const std::string& a_name, InputId& a_out);

	std::vector<std::string> SplitList(const std::string& a_s);  // separa per virgola/punto e virgola
}
