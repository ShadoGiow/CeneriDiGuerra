#include "pch.h"

#include "Controls.h"

#include "Log.h"
#include "Settings.h"

#include <iterator>

// Perche' esiste: il file controlmap.txt della patch "Sneak su Freccia giu'" non veniva applicato dal gioco.
// Qui si legge la mappa dei tasti che il gioco ha davvero in memoria, la si scrive nel log e si sposta
// lo Sneak direttamente in memoria (funziona anche se il gioco o un'altra mod ignora il file).

namespace cdg::controls
{
	namespace
	{
		constexpr std::uint16_t kCodes[kSneakCount] = { 0x0040, 0x0002, 0x0001, 0x0004, 0x0008, 0x0020, 0x0010, 0x0080 };
		constexpr std::uint16_t kPadL3 = 0x0040;
		constexpr std::uint16_t kUnmapped = 0x00FF;

		struct Freed
		{
			std::string   name;
			std::uint16_t key;
		};
		std::vector<Freed> gFreed;  // comandi tolti dal tasto scelto, da rimettere se si cambia tasto
		bool               gFileLogged{ false };

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
		std::uint16_t TargetCode()
		{
			for (std::size_t i = 0; i < kSneakCount; ++i) {
				if (EqNoCase(S.sneakPad, kSneakNames[i])) {
					return kCodes[i];
				}
			}
			return 0;
		}

		bool IsPadButton(std::uint16_t a_key)
		{
			for (auto c : kCodes) {
				if (c == a_key) {
					return true;
				}
			}
			return false;
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

			std::istringstream in(all);
			std::string        line;
			while (std::getline(in, line)) {
				if (line.size() > 5 && (line.rfind("Sneak", 0) == 0 || line.rfind("Hotkey8", 0) == 0)) {
					std::string compact;
					bool        sp = false;
					for (char c : line) {
						if (c == '\r') {
							continue;
						}
						if (c == '\t' || c == ' ') {
							if (!sp) {
								compact.push_back(' ');
							}
							sp = true;
						} else {
							compact.push_back(c);
							sp = false;
						}
					}
					Say("controlli: riga del file: {}", compact);
				}
			}

			std::error_code ec;
			Say("controlli: ControlMap_Custom.txt nella cartella del gioco: {}",
				std::filesystem::exists("ControlMap_Custom.txt", ec) ? "presente" : "assente");
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

			// 1) rimette a posto i comandi tolti in precedenza
			for (const auto& fr : gFreed) {
				for (auto& m : arr) {
					if (EqNoCase(m.eventID.c_str(), fr.name.c_str()) && m.inputKey == kUnmapped) {
						m.inputKey = fr.key;
					}
				}
			}
			gFreed.clear();

			// 2) stato attuale (solo le voci interessanti)
			Say("controlli [{}]: Sneak sul pad (GetMappedKey) = 0x{:04X}", a_when,
				cm->GetMappedKey("Sneak", RE::INPUT_DEVICES::kGamepad));

			const std::uint16_t target = TargetCode();
			if (target == 0) {
				Say("controlli [{}]: SneakPad = '{}' non valido: non cambio niente", a_when, S.sneakPad);
				return;
			}

			RE::ControlMap::UserEventMapping* sneak = nullptr;
			for (auto& m : arr) {
				if (EqNoCase(m.eventID.c_str(), "Sneak")) {
					sneak = &m;
					break;
				}
			}
			if (!sneak) {
				Say("controlli [{}]: voce Sneak non trovata sul pad", a_when);
				return;
			}
			if (!IsPadButton(sneak->inputKey)) {
				Say("controlli [{}]: Sneak ha un valore inatteso (0x{:04X}), non modifico niente", a_when, sneak->inputKey);
				return;
			}

			// 3) Sneak sul tasto scelto; gli altri comandi "semplici" su quel tasto vengono liberati
			bool changed = false;
			if (sneak->inputKey != target) {
				sneak->inputKey = target;
				changed = true;
			}
			const std::uint16_t plainMod = sneak->modifier;
			for (auto& m : arr) {
				if (&m == sneak || m.inputKey != target || m.modifier != plainMod) {
					continue;
				}
				const char* n = m.eventID.c_str();
				if (!n || !*n) {
					continue;
				}
				Say("controlli [{}]: '{}' tolto da 0x{:04X} (lo Sneak ora e' li')", a_when, n, target);
				gFreed.push_back({ n, target });
				m.inputKey = kUnmapped;
				changed = true;
			}

			// 4) riepilogo
			for (auto& m : arr) {
				const char* n = m.eventID.c_str();
				if (!n || !*n) {
					continue;
				}
				if (m.inputKey == target || m.inputKey == kPadL3 || EqNoCase(n, "Sneak")) {
					Say("controlli [{}]:   {} = tasto 0x{:04X}, modificatore 0x{:04X}, rimappabile: {}", a_when, n,
						m.inputKey, m.modifier, m.remappable ? "si" : "no");
				}
			}
			if (changed) {
				Say("controlli [{}]: Sneak sul pad ora = 0x{:04X} ({})", a_when,
					cm->GetMappedKey("Sneak", RE::INPUT_DEVICES::kGamepad), S.sneakPad);
			}
		} catch (const std::exception& e) {
			SKSE::log::error("controlli: errore: {}", e.what());
		} catch (...) {
			SKSE::log::error("controlli: errore sconosciuto");
		}
	}

	namespace
	{
		// registro dei tasti premuti sul pad (solo croce, Start, Back, L3, R3): al massimo 150 righe per avvio.
		// Per ogni pressione scrive il comando che l'evento porta con se' e quello che la mappa del gioco assegna a quel tasto.
		// Se premendo Freccia giu' qui non compare NIENTE, la pressione non arriva al gioco (la ferma qualcosa prima).
		class PadLog final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_evn, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				try {
					if (!a_evn || !S.logEvents || _budget <= 0) {
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
						const auto code = b->GetIDCode();
						if (code > 0xFFFF || !IsPadButton(static_cast<std::uint16_t>(code))) {
							continue;
						}
						const auto& ue = e->QUserEvent();
						const char* ues = ue.c_str();

						std::string mapped = "?";
						if (auto* cm = RE::ControlMap::GetSingleton()) {
							mapped = std::string(cm->GetUserEventName(code, RE::INPUT_DEVICES::kGamepad));
						}
						Say("pad: premuto 0x{:04X} -> evento '{}', mappa del gioco '{}'", code,
							(ues && *ues) ? ues : "(vuoto)", mapped.empty() ? "(nessuno)" : mapped);
						--_budget;
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
}
