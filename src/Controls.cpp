#include "pch.h"

#include "Controls.h"

#include "Log.h"
#include "Settings.h"

#include <iterator>

// Perche' esiste: il file controlmap.txt della patch "Sneak su Freccia giu'" risultava ignorato dal gioco
// (il menu Controlli mostrava ancora L3). Qui si legge la mappa che il gioco ha davvero in memoria,
// la si scrive nel log (cosi' si vede cosa succede) e si sposta lo Sneak direttamente in memoria.

namespace cdg::controls
{
	namespace
	{
		constexpr std::uint16_t kPadL3 = 0x0040;
		constexpr std::uint16_t kPadDown = 0x0002;
		constexpr std::uint16_t kUnmapped = 0x00FF;

		// tasto originale dello Sneak, ricordato per poterlo rimettere se l'opzione viene spenta
		std::optional<std::uint16_t> gOrigSneak;
		bool                         gFileLogged{ false };

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

		bool IsPlainHotkey(const char* a_name)
		{
			if (!a_name) {
				return false;
			}
			for (int i = 1; i <= 8; ++i) {
				const auto n = std::format("Hotkey{}", i);
				if (EqNoCase(a_name, n.c_str())) {
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

			Say("controlli [{}]: Sneak sul pad (GetMappedKey) = 0x{:04X}", a_when,
				cm->GetMappedKey("Sneak", RE::INPUT_DEVICES::kGamepad));

			// stato prima della modifica
			for (auto& m : arr) {
				const char* n = m.eventID.c_str();
				if (!n || !*n) {
					continue;
				}
				if (m.inputKey == kPadL3 || m.inputKey == kPadDown || EqNoCase(n, "Sneak")) {
					Say("controlli [{}]:   {} = tasto 0x{:04X}, modificatore 0x{:04X}, rimappabile: {}", a_when, n,
						m.inputKey, m.modifier, m.remappable ? "si" : "no");
				}
			}

			bool changed = false;
			if (S.sneakOnDpadDown) {
				// il modificatore dello Sneak (di solito "nessuno") serve per riconoscere i tasti semplici
				std::optional<std::uint16_t> sneakMod;
				for (auto& m : arr) {
					if (!EqNoCase(m.eventID.c_str(), "Sneak")) {
						continue;
					}
					sneakMod = m.modifier;
					if (m.inputKey == kPadL3) {
						gOrigSneak = m.inputKey;
						m.inputKey = kPadDown;
						changed = true;
					} else if (m.inputKey != kPadDown) {
						// codifica inattesa: non si tocca niente
						Say("controlli [{}]: Sneak ha un valore inatteso (0x{:04X}), non lo modifico", a_when, m.inputKey);
						sneakMod.reset();
					}
				}
				if (sneakMod) {
					// Hotkey 1-8 semplici sulla Freccia giu' (non quelli con modificatore) vengono liberati
					for (auto& m : arr) {
						if (m.inputKey == kPadDown && m.modifier == *sneakMod && IsPlainHotkey(m.eventID.c_str())) {
							Say("controlli [{}]: {} tolto dalla Freccia giu'", a_when, m.eventID.c_str());
							m.inputKey = kUnmapped;
							changed = true;
						}
					}
				}
			} else if (gOrigSneak && *gOrigSneak == kPadL3) {
				for (auto& m : arr) {
					if (EqNoCase(m.eventID.c_str(), "Sneak") && m.inputKey == kPadDown) {
						m.inputKey = kPadL3;
						changed = true;
					}
				}
			}

			if (changed) {
				Say("controlli [{}]: Sneak sul pad ora = 0x{:04X}", a_when,
					cm->GetMappedKey("Sneak", RE::INPUT_DEVICES::kGamepad));
			}
		} catch (const std::exception& e) {
			SKSE::log::error("controlli: errore: {}", e.what());
		} catch (...) {
			SKSE::log::error("controlli: errore sconosciuto");
		}
	}
}
