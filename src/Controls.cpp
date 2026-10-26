#include "pch.h"

#include "Controls.h"

#include "Log.h"
#include "Settings.h"

#include <iterator>

// Perche' esiste: per liberare L3 (usato dalle mosse) lo Sneak puo' essere spostato su un altro tasto del pad.
// Il file controlmap.txt non viene applicato dal gioco (vince ControlMap_Custom.txt), quindi la mappa si cambia
// direttamente in memoria. NIENTE viene scritto su disco: riavviando il gioco tutto torna come prima.
//
// Regole di sicurezza (1.1.5):
//  - con SneakTasto = L3 (predefinito) il DLL non tocca la mappa;
//  - prima di cambiare qualsiasi cosa si copia TUTTA la tabella dei tasti del pad; ogni nuova Apply() la rimette
//    com'era (voce per voce) e poi, se serve, applica la scelta.

namespace cdg::controls
{
	namespace
	{
		using clk = std::chrono::steady_clock;

		constexpr std::uint32_t kCodes[kSneakCount] = { 0x0040, 0x0002, 0x0001, 0x0004, 0x0008, 0x0020, 0x0010, 0x0080 };
		constexpr std::uint32_t kPadL3 = 0x0040;
		constexpr std::uint32_t kUnmapped = 0x00FF;

		struct Orig
		{
			std::string   name;
			std::uint32_t key;
			std::uint32_t mod;
		};
		std::vector<Orig>          gOrig;  // la tabella dei tasti del pad (gameplay) prima delle nostre modifiche
		std::vector<std::uint32_t> gMine;  // i tasti che abbiamo scritto noi (stesso ordine)
		bool                       gHave{ false };
		bool                       gFileLogged{ false };
		bool                       gTableLogged{ false };

		bool EqNoCase(const char* a_a, const char* a_b)
		{
			if (!a_a || !a_b) {
				return false;
			}
			while (*a_a && *a_b) {
				if (std::tolower(static_cast<unsigned char>(*a_a)) != std::tolower(static_cast<unsigned char>(*a_b))) {
					return false;
				}
				++a_a;
				++a_b;
			}
			return *a_a == 0 && *a_b == 0;
		}

		// tasto del pad corrispondente al nome salvato nell'INI (0 = nome non valido)
		std::uint32_t TargetCode()
		{
			for (std::size_t i = 0; i < kSneakCount; ++i) {
				if (EqNoCase(S.sneakPad, kSneakNames[i])) {
					return kCodes[i];
				}
			}
			return 0;
		}

		bool IsPadButton(std::uint32_t a_key)
		{
			for (auto c : kCodes) {
				if (c == a_key) {
					return true;
				}
			}
			return false;
		}

		const char* NameOf(const RE::ControlMap::UserEventMapping& a_m)
		{
			const char* n = a_m.eventID.c_str();
			return n ? n : "";
		}

		// Cosa vede il gioco del file controlmap.txt (con MO2 e' la cartella Data "virtuale").
		void LogControlmapFile()
		{
			if (gFileLogged) {
				return;
			}
			gFileLogged = true;

			const char* path = "Data/Interface/Controls/PC/controlmap.txt";
			std::ifstream f(path, std::ios::binary);
			if (!f) {
				Say("controlli: {} non leggibile da qui", path);
				return;
			}
			std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			Say("controlli: {} letto dal gioco, {} byte", path, all.size());

			std::error_code ec;
			Say("controlli: ControlMap_Custom.txt nella cartella del gioco: {}",
				std::filesystem::exists("ControlMap_Custom.txt", ec) ? "presente" : "assente");
		}

		// La tabella completa dei tasti del pad, una sola volta per avvio (serve a capire cosa c'e' su ogni tasto).
		template <class Arr>
		void LogTable(Arr& a_arr)
		{
			if (gTableLogged) {
				return;
			}
			gTableLogged = true;
			for (auto& m : a_arr) {
				if (m.inputKey != kUnmapped) {
					Say("controlli: tabella pad: {} = 0x{:04X} (modificatore 0x{:04X})", NameOf(m), m.inputKey, m.modifier);
				}
			}
		}

		// Rimette ogni voce della tabella com'era PRIMA delle nostre modifiche.
		// Una voce cambiata da qualcun altro dopo di noi (il gioco, un'altra mod) non viene toccata.
		template <class Arr>
		void RestoreAll(Arr& a_arr, const char* a_when)
		{
			if (!gHave) {
				return;
			}
			std::size_t n = 0;
			for (auto& m : a_arr) {
				(void)m;
				++n;
			}
			bool same = (n == gOrig.size() && n == gMine.size());
			if (same) {
				std::size_t i = 0;
				for (auto& m : a_arr) {
					if (!EqNoCase(NameOf(m), gOrig[i].name.c_str())) {
						same = false;
						break;
					}
					++i;
				}
			}
			if (!same) {
				// il gioco ha ricaricato la mappa (o e' cambiata): quello che c'e' ora e' il nuovo "originale"
				Say("controlli [{}]: la mappa del gioco e' stata ricaricata: parto da quella attuale", a_when);
				gOrig.clear();
				gMine.clear();
				gHave = false;
				return;
			}

			std::size_t i = 0;
			int         back = 0;
			for (auto& m : a_arr) {
				if (m.inputKey == gMine[i]) {
					if (m.inputKey != gOrig[i].key) {
						Say("controlli [{}]: rimesso '{}' sul tasto 0x{:04X}", a_when, NameOf(m), gOrig[i].key);
						m.inputKey = gOrig[i].key;
						++back;
					}
				} else if (m.inputKey != gOrig[i].key) {
					Say("controlli [{}]: '{}' e' stato cambiato da altri (0x{:04X}): non lo tocco", a_when, NameOf(m), m.inputKey);
				}
				++i;
			}

			// verifica: ora la tabella deve essere uguale all'originale
			std::size_t diff = 0;
			i = 0;
			for (auto& m : a_arr) {
				if (m.inputKey != gOrig[i].key) {
					++diff;
				}
				++i;
			}
			Say("controlli [{}]: tasti originali rimessi ({} voci), differenze rimaste: {}", a_when, back, diff);
			gOrig.clear();
			gMine.clear();
			gHave = false;
		}

		// --- aiuto allo Sneak ------------------------------------------------------------------------------
		struct Assist
		{
			bool          pending{ false };
			int           stage{ 0 };
			clk::time_point at{};
			bool          before{ false };  // era furtivo quando e' stato premuto il tasto
			std::uint32_t code{ 0 };
		};
		std::mutex  gAsMx;
		Assist      gAs;
		std::mutex  gNoteMx;
		std::string gNote;

		void SetNote(const std::string& a_note)
		{
			std::lock_guard l(gNoteMx);
			gNote = a_note;
		}

		bool Sneaking()
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			auto* as = pc ? pc->AsActorState() : nullptr;
			return as && as->IsSneaking();
		}

		// Condizioni minime perche' sia sensato forzare lo Sneak (le stesse in cui lo accetterebbe il gioco con L3)
		const char* CannotSneakBecause(RE::PlayerCharacter* a_pc)
		{
			if (!a_pc || !a_pc->Is3DLoaded()) {
				return "personaggio non pronto";
			}
			if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
				return "gioco in pausa/menu";
			}
			if (auto* cm = RE::ControlMap::GetSingleton()) {
				if (!cm->AreControlsEnabled(RE::UserEvents::USER_EVENT_FLAG::kSneaking)) {
					return "comandi di Sneak disattivati (dialogo, filmato...)";
				}
			}
			if (a_pc->IsDead() || a_pc->IsInRagdollState() || a_pc->IsInKillMove() || a_pc->IsOnMount() || a_pc->IsStaggering()) {
				return "personaggio a cavallo, a terra, in stagger o in una mossa finale";
			}
			if (auto* as = a_pc->AsActorState(); as && as->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone) {
				return "attacco in corso";
			}
			return nullptr;
		}

		void OnSneakPressed(std::uint32_t a_code)
		{
			// solo se lo Sneak e' stato spostato da noi
			if (!S.sneakAssist || !gHave || a_code == kPadL3) {
				return;
			}
			const bool before = Sneaking();
			std::lock_guard l(gAsMx);
			if (gAs.pending) {
				return;
			}
			gAs.pending = true;
			gAs.stage = 0;
			gAs.at = clk::now() + std::chrono::milliseconds(200);
			gAs.before = before;
			gAs.code = a_code;
		}
	}

	void Apply(const char* a_when)
	{
		try {
			LogControlmapFile();

			auto* cm = RE::ControlMap::GetSingleton();
			if (!cm) {
				Say("controlli [{}]: ControlMap non disponibile", a_when);
				return;
			}
			auto* ctx = cm->controlMap[RE::UserEvents::INPUT_CONTEXT_ID::kGameplay];
			if (!ctx) {
				Say("controlli [{}]: contesto di gioco non disponibile", a_when);
				return;
			}
			auto& arr = ctx->deviceMappings[RE::INPUT_DEVICES::kGamepad];

			// 1) tutto come era
			RestoreAll(arr, a_when);
			LogTable(arr);

			Say("controlli [{}]: Sneak sul pad (GetMappedKey) = 0x{:04X}", a_when,
				cm->GetMappedKey("Sneak", RE::INPUT_DEVICES::kGamepad));

			const std::uint32_t target = TargetCode();
			if (target == 0) {
				Say("controlli [{}]: SneakTasto = '{}' non valido: non cambio niente", a_when, S.sneakPad);
				return;
			}
			if (target == kPadL3) {
				Say("controlli [{}]: Sneak su L3 (originale): non cambio niente", a_when);
				return;
			}

			RE::ControlMap::UserEventMapping* sneak = nullptr;
			for (auto& m : arr) {
				if (EqNoCase(NameOf(m), "Sneak")) {
					sneak = &m;
					break;
				}
			}
			if (!sneak) {
				Say("controlli [{}]: voce Sneak non trovata sul pad: non cambio niente", a_when);
				return;
			}
			if (!IsPadButton(sneak->inputKey)) {
				Say("controlli [{}]: Sneak ha un valore inatteso (0x{:04X}): non cambio niente", a_when, sneak->inputKey);
				return;
			}

			// 2) copia di sicurezza di TUTTA la tabella, poi le modifiche
			gOrig.clear();
			gMine.clear();
			for (auto& m : arr) {
				gOrig.push_back({ NameOf(m), static_cast<std::uint32_t>(m.inputKey), static_cast<std::uint32_t>(m.modifier) });
			}
			gHave = true;

			sneak->inputKey = target;
			const auto plainMod = sneak->modifier;
			for (auto& m : arr) {
				if (&m == sneak || m.inputKey != target || m.modifier != plainMod) {
					continue;
				}
				const char* n = NameOf(m);
				if (!*n) {
					continue;
				}
				Say("controlli [{}]: '{}' tolto da 0x{:04X} (lo Sneak ora e' li')", a_when, n, target);
				m.inputKey = kUnmapped;
			}
			for (auto& m : arr) {
				gMine.push_back(static_cast<std::uint32_t>(m.inputKey));
			}

			Say("controlli [{}]: Sneak sul pad ora = 0x{:04X} ({})", a_when,
				cm->GetMappedKey("Sneak", RE::INPUT_DEVICES::kGamepad), S.sneakPad);
		} catch (const std::exception& e) {
			SKSE::log::error("controlli: errore: {}", e.what());
		} catch (...) {
			SKSE::log::error("controlli: errore sconosciuto");
		}
	}

	namespace
	{
		// registro dei tasti premuti sul pad (solo croce, Start, Back, L3, R3): al massimo 150 righe per avvio.
		// Per ogni pressione scrive il comando che l'evento porta con se' (quello che il gioco ha assegnato a quel tasto)
		// e lo stato del gioco in quel momento.
		class PadLog final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_evn, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				try {
					if (!a_evn) {
						return RE::BSEventNotifyControl::kContinue;
					}
					for (auto* e = *a_evn; e; e = e->next) {
						if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || e->GetDevice() != RE::INPUT_DEVICES::kGamepad) {
							continue;
						}
						auto* b = e->AsButtonEvent();
						if (!b || !b->IsDown()) {
							continue;
						}
						const std::uint32_t code = b->GetIDCode();
						if (!IsPadButton(code)) {
							continue;
						}
						const auto& ue = e->QUserEvent();
						const char* ues = ue.c_str();
						const bool  isSneak = ues && EqNoCase(ues, "Sneak");

						if (S.logEvents && _budget > 0) {
							bool paused = false;
							if (auto* ui = RE::UI::GetSingleton()) {
								paused = ui->GameIsPaused();
							}
							bool sneakOk = true;
							if (auto* cm = RE::ControlMap::GetSingleton()) {
								sneakOk = cm->AreControlsEnabled(RE::UserEvents::USER_EVENT_FLAG::kSneaking);
							}
							Say("pad: premuto 0x{:04X} -> evento '{}'; menu/pausa: {}; comandi Sneak attivi: {}; furtivo: {}", code,
								(ues && *ues) ? ues : "(vuoto)", paused ? "si" : "no", sneakOk ? "si" : "no",
								Sneaking() ? "si" : "no");
							--_budget;
						}
						if (isSneak) {
							OnSneakPressed(code);
						}
					}
				} catch (...) {
				}
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			int _budget{ 150 };
		};
	}

	void InstallPadLog()
	{
		static PadLog sink;
		static bool   done = false;
		if (done) {
			return;
		}
		if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) {
			mgr->PrependEventSink(static_cast<RE::BSTEventSink<RE::InputEvent*>*>(&sink));
			done = true;
			Say("controlli: registro dei tasti del pad attivo (croce, Start, Back, L3, R3)");
		}
	}

	std::string LastNote()
	{
		std::lock_guard l(gNoteMx);
		return gNote;
	}

	void Tick()
	{
		try {
			Assist as;
			{
				std::lock_guard l(gAsMx);
				if (!gAs.pending || clk::now() < gAs.at) {
					return;
				}
				as = gAs;
			}

			auto* pc = RE::PlayerCharacter::GetSingleton();
			auto finish = [&](const std::string& a_msg) {
				Say("sneak: {}", a_msg);
				SetNote(a_msg);
				std::lock_guard l(gAsMx);
				gAs.pending = false;
			};
			auto again = [&](int a_stage, int a_ms) {
				std::lock_guard l(gAsMx);
				gAs.stage = a_stage;
				gAs.at = clk::now() + std::chrono::milliseconds(a_ms);
			};
			const bool now = Sneaking();

			if (as.stage == 0) {
				if (now != as.before) {
					finish(std::format("premuto 0x{:04X}: il gioco ha cambiato lo Sneak da solo ({}), non serve aiuto", as.code,
						now ? "ora furtivo" : "ora in piedi"));
					return;
				}
				if (const char* why = CannotSneakBecause(pc)) {
					finish(std::format("premuto 0x{:04X}: il gioco non ha reagito, ma non intervengo ({})", as.code, why));
					return;
				}
				const char* ev = as.before ? "SneakStop" : "SneakStart";
				const bool  sent = pc->NotifyAnimationGraph(ev);
				Say("sneak: premuto 0x{:04X}: il gioco non ha reagito; invio l'evento '{}' ({})", as.code, ev,
					sent ? "accettato" : "rifiutato");
				again(1, 400);
				return;
			}

			if (as.stage == 1) {
				if (now != as.before) {
					finish(std::format("premuto 0x{:04X}: il gioco non reagiva, l'evento d'animazione ha funzionato ({})", as.code,
						now ? "ora furtivo" : "ora in piedi"));
					return;
				}
				// l'evento d'animazione non basta: provo con il gestore dello Sneak del gioco, come se fosse L3
				auto* ctrl = RE::PlayerControls::GetSingleton();
				auto* ue = RE::UserEvents::GetSingleton();
				if (!ctrl || !ctrl->sneakHandler || !ue) {
					finish(std::format("premuto 0x{:04X}: ne' il gioco ne' l'evento d'animazione cambiano lo Sneak (gestore non disponibile)",
						as.code));
					return;
				}
				// il piccolo blocco di memoria dell'evento non viene liberato di proposito (poche decine di byte per pressione)
				auto* ev = RE::ButtonEvent::Create(RE::INPUT_DEVICES::kGamepad, ue->sneak, 0x40, 1.0f, 0.0f);
				if (!ev) {
					finish(std::format("premuto 0x{:04X}: impossibile creare l'evento per il gestore dello Sneak", as.code));
					return;
				}
				const bool can = ctrl->sneakHandler->CanProcess(ev);
				if (can) {
					ctrl->sneakHandler->ProcessButton(ev, &ctrl->data);
				}
				Say("sneak: premuto 0x{:04X}: provo il gestore dello Sneak del gioco ({})", as.code,
					can ? "ha preso l'evento" : "NON ha preso l'evento");
				again(2, 400);
				return;
			}

			finish(std::format("premuto 0x{:04X}: dopo il gestore dello Sneak lo stato e' {}{}", as.code,
				now ? "furtivo" : "in piedi", now == as.before ? "  (NON e' cambiato: lo Sneak non parte in nessun modo)" : ""));
		} catch (const std::exception& e) {
			SKSE::log::error("sneak: errore: {}", e.what());
			std::lock_guard l(gAsMx);
			gAs.pending = false;
		} catch (...) {
			SKSE::log::error("sneak: errore sconosciuto");
			std::lock_guard l(gAsMx);
			gAs.pending = false;
		}
	}
}
