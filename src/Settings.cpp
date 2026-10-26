#include "pch.h"

#include "Log.h"
#include "Settings.h"

namespace cdg
{
	Settings                           S;
	std::mutex                         gBindMx;
	std::map<std::string, std::string> gNames;
	std::map<std::string, std::string> gDescs;
	std::map<std::string, std::string> gSections;

	namespace
	{
		std::vector<std::string> gFavs;  // protetto da gBindMx

		constexpr auto kIni = "Data/SKSE/Plugins/CeneriDiGuerra.ini";

		enum class T { Bool, Float, Str };

		struct Item
		{
			const char* section;
			const char* key;
			T           type;
			void*       ptr;
			std::size_t cap;  // per Str
		};

		std::atomic<bool> gDirty{ false };

		std::vector<Item> Table()
		{
			return {
				{ "Generale", "Versione", T::Float, &S.iniVersion, 0 },
				{ "Generale", "Attivo", T::Bool, &S.enabled, 0 },
				{ "Generale", "SoloArmaEstratta", T::Bool, &S.requireDrawn, 0 },
				{ "Generale", "AncheAsciMartelli2H", T::Bool, &S.allow2HAxe, 0 },
				{ "Generale", "QualunqueArma", T::Bool, &S.allowAnyWeapon, 0 },
				{ "Generale", "MessaggiASchermo", T::Bool, &S.showHud, 0 },

				{ "Avvio", "Eventi", T::Str, S.triggerEvents, sizeof(S.triggerEvents) },
				{ "Avvio", "AttesaConferma", T::Float, &S.evalTimeout, 0 },
				{ "Avvio", "PausaStop", T::Float, &S.stopDelay, 0 },
				{ "Avvio", "Tentativi", T::Float, &S.retries, 0 },
				{ "Avvio", "AttesaFineAttacco", T::Float, &S.clearWait, 0 },
				{ "Avvio", "DurataMassima", T::Float, &S.maxSeconds, 0 },
				{ "Avvio", "RitardoProva", T::Float, &S.testDelay, 0 },
				{ "Avvio", "CatenaAutomatica", T::Bool, &S.autoChain, 0 },
				{ "Avvio", "BloccaComandi", T::Bool, &S.lockInput, 0 },

				{ "Controlli", "SneakTasto", T::Str, S.sneakPad, sizeof(S.sneakPad) },
				{ "Controlli", "SneakAiuto", T::Bool, &S.sneakAssist, 0 },

				{ "Costi", "Stamina", T::Float, &S.staminaCost, 0 },
				{ "Costi", "Attesa", T::Float, &S.cooldown, 0 },

				{ "Diagnostica", "Log", T::Bool, &S.logEvents, 0 },
			};
		}

		std::string Trim(std::string s)
		{
			auto issp = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
			while (!s.empty() && issp(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
			while (!s.empty() && issp(static_cast<unsigned char>(s.back()))) s.pop_back();
			return s;
		}

		std::string Lower(std::string s)
		{
			for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		struct NamedKey { InputId id; const char* name; };

		// pad: 1..14 + 20/21 (grilletti)
		constexpr NamedKey kPad[] = {
			{ 1, "Pad_DpadUp" }, { 2, "Pad_DpadDown" }, { 3, "Pad_DpadLeft" }, { 4, "Pad_DpadRight" },
			{ 5, "Pad_Start" }, { 6, "Pad_Back" }, { 7, "Pad_L3" }, { 8, "Pad_R3" },
			{ 9, "Pad_LB" }, { 10, "Pad_RB" }, { 11, "Pad_A" }, { 12, "Pad_B" },
			{ 13, "Pad_X" }, { 14, "Pad_Y" }, { 20, "Pad_LT" }, { 21, "Pad_RT" },
		};

		// tastiera/mouse: 0x100 + VK
		constexpr NamedKey kKb[] = {
			{ 0x101, "Mouse1" }, { 0x102, "Mouse2" }, { 0x104, "Mouse3" }, { 0x105, "Mouse4" }, { 0x106, "Mouse5" },
			{ 0x108, "Backspace" }, { 0x109, "Tab" }, { 0x10D, "Enter" }, { 0x110, "Shift" }, { 0x111, "Ctrl" },
			{ 0x112, "Alt" }, { 0x113, "Pause" }, { 0x114, "CapsLock" }, { 0x11B, "Esc" }, { 0x120, "Space" },
			{ 0x121, "PageUp" }, { 0x122, "PageDown" }, { 0x123, "End" }, { 0x124, "Home" },
			{ 0x125, "Left" }, { 0x126, "Up" }, { 0x127, "Right" }, { 0x128, "Down" },
			{ 0x12D, "Insert" }, { 0x12E, "Delete" },
			{ 0x160, "Num0" }, { 0x161, "Num1" }, { 0x162, "Num2" }, { 0x163, "Num3" }, { 0x164, "Num4" },
			{ 0x165, "Num5" }, { 0x166, "Num6" }, { 0x167, "Num7" }, { 0x168, "Num8" }, { 0x169, "Num9" },
			{ 0x16A, "Num*" }, { 0x16B, "Num+" }, { 0x16D, "Num-" }, { 0x16E, "Num." }, { 0x16F, "Num/" },
			{ 0x1BA, ";" }, { 0x1BB, "=" }, { 0x1BC, "," }, { 0x1BD, "-" }, { 0x1BE, "." }, { 0x1BF, "/" },
			{ 0x1C0, "`" }, { 0x1DB, "[" }, { 0x1DC, "\\" }, { 0x1DD, "]" }, { 0x1DE, "'" },
		};

		constexpr InputId kPadL3 = 7;

		// a_base = tasto "chiave" dei tre slot (LT, L3 oppure LB)
		void SetPreset(InputId a_base)
		{
			// Slot 1: tasto tenuto (lungo); Slot 2: +RT istantaneo; Slot 3: +RB istantaneo
			S.bind[0].keys = { a_base };
			S.bind[0].hold = 0.45f;
			S.bind[1].keys = { a_base, kPadRT };
			S.bind[1].hold = 0.0f;
			S.bind[2].keys = { a_base, kPadRB };
			S.bind[2].hold = 0.0f;
		}

		void Defaults()
		{
			S = Settings{};
			std::lock_guard l(gBindMx);
			gFavs.clear();
			// predefinito: slot su L3 o LB (LT resta libero per la parata) e Sneak spostato dal DLL su Freccia giu (1.2.1)
			SetPreset(kPadL3);
		}
	}

	std::string InputName(InputId a_id)
	{
		for (auto& k : kPad) {
			if (k.id == a_id) return k.name;
		}
		for (auto& k : kKb) {
			if (k.id == a_id) return k.name;
		}
		if (a_id >= 0x100) {
			const auto vk = a_id - 0x100;
			if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
				return std::string(1, static_cast<char>(vk));
			}
			if (vk >= 0x70 && vk <= 0x87) {
				return std::format("F{}", vk - 0x70 + 1);
			}
			return std::format("VK{:02X}", vk);
		}
		return std::format("Pad{}", a_id);
	}

	bool InputFromName(const std::string& a_name, InputId& a_out)
	{
		const auto n = Lower(Trim(a_name));
		if (n.empty()) return false;
		for (auto& k : kPad) {
			if (Lower(k.name) == n) { a_out = k.id; return true; }
		}
		for (auto& k : kKb) {
			if (Lower(k.name) == n) { a_out = k.id; return true; }
		}
		if (n.size() == 1 && ((n[0] >= '0' && n[0] <= '9') || (n[0] >= 'a' && n[0] <= 'z'))) {
			a_out = static_cast<InputId>(0x100 + std::toupper(static_cast<unsigned char>(n[0])));
			return true;
		}
		if (n[0] == 'f' && n.size() <= 3) {
			int  v = 0;
			bool ok = n.size() > 1;
			for (std::size_t i = 1; i < n.size(); ++i) {
				if (n[i] < '0' || n[i] > '9') { ok = false; break; }
				v = v * 10 + (n[i] - '0');
			}
			if (ok && v >= 1 && v <= 24) { a_out = static_cast<InputId>(0x100 + 0x70 + v - 1); return true; }
		}
		if (n.rfind("vk", 0) == 0 && n.size() > 2) {
			char*      e = nullptr;
			const auto v = std::strtoul(n.c_str() + 2, &e, 16);
			if (e && *e == 0 && v > 0 && v < 0x100) { a_out = static_cast<InputId>(0x100 + v); return true; }
		}
		return false;
	}

	// formato: "Pad_LT+Pad_RT|0.30"  (dopo | i secondi di pressione)
	std::string BindingToString(const Binding& a_b)
	{
		std::string s;
		for (std::size_t i = 0; i < a_b.keys.size(); ++i) {
			if (i) s += '+';
			s += InputName(a_b.keys[i]);
		}
		if (!s.empty()) {
			s += std::format("|{:.2f}", a_b.hold);
		}
		return s;
	}

	bool BindingFromString(const std::string& a_s, Binding& a_out)
	{
		Binding b;
		auto    s = Trim(a_s);
		if (s.empty()) {
			a_out = b;
			return true;
		}
		std::string keys = s;
		if (const auto bar = s.find('|'); bar != std::string::npos) {
			keys = s.substr(0, bar);
			try {
				b.hold = std::clamp(std::stof(s.substr(bar + 1)), 0.0f, 5.0f);
			} catch (...) {
				b.hold = 0.0f;
			}
		}
		std::stringstream ss(keys);
		std::string       tok;
		while (std::getline(ss, tok, '+')) {
			InputId id{};
			if (!InputFromName(tok, id)) {
				return false;
			}
			if (std::find(b.keys.begin(), b.keys.end(), id) == b.keys.end()) {
				b.keys.push_back(id);
			}
		}
		if (b.keys.size() > 5) {
			b.keys.resize(5);
		}
		a_out = std::move(b);
		return true;
	}

	std::string BindingLabel(const Binding& a_b)
	{
		if (a_b.empty()) return "(nessuno)";
		std::string s;
		for (std::size_t i = 0; i < a_b.keys.size(); ++i) {
			if (i) s += " + ";
			s += InputName(a_b.keys[i]);
		}
		if (a_b.hold > 0.001f) {
			s += std::format("  (tieni premuto {:.2f} s)", a_b.hold);
		} else {
			s += "  (istantaneo)";
		}
		return s;
	}

	std::vector<std::string> SplitList(const std::string& a_s)
	{
		std::vector<std::string> out;
		std::string              cur;
		for (const char c : a_s) {
			if (c == ',' || c == ';') {
				cur = Trim(cur);
				if (!cur.empty()) out.push_back(cur);
				cur.clear();
			} else {
				cur += c;
			}
		}
		cur = Trim(cur);
		if (!cur.empty()) out.push_back(cur);
		return out;
	}

	void ApplyKeyPreset(int a_preset)
	{
		{
			std::lock_guard l(gBindMx);
			SetPreset(a_preset == 0 ? kPadLT : (a_preset == 2 ? kPadLB : kPadL3));
		}
		RequestSave();
	}

	std::vector<std::string> Favorites()
	{
		std::lock_guard l(gBindMx);
		return gFavs;
	}

	bool IsFavorite(const std::string& a_code)
	{
		const auto k = Lower(a_code);
		std::lock_guard l(gBindMx);
		return std::any_of(gFavs.begin(), gFavs.end(), [&](const std::string& f) { return Lower(f) == k; });
	}

	void SetFavorite(const std::string& a_code, bool a_on)
	{
		const auto k = Lower(a_code);
		{
			std::lock_guard l(gBindMx);
			const auto      it = std::find_if(gFavs.begin(), gFavs.end(), [&](const std::string& f) { return Lower(f) == k; });
			if (a_on && it == gFavs.end()) {
				gFavs.push_back(a_code);
			} else if (!a_on && it != gFavs.end()) {
				gFavs.erase(it);
			}
		}
		RequestSave();
	}

	std::string GetMoveCode(int a_slot)
	{
		if (a_slot < 0 || a_slot >= kSlots) return {};
		std::lock_guard l(gBindMx);
		return S.moveCode[a_slot];
	}

	void SetMoveCode(int a_slot, const std::string& a_code)
	{
		if (a_slot < 0 || a_slot >= kSlots) return;
		{
			std::lock_guard l(gBindMx);
			const auto      n = std::min(a_code.size(), sizeof(S.moveCode[a_slot]) - 1);
			std::memcpy(S.moveCode[a_slot], a_code.data(), n);
			S.moveCode[a_slot][n] = 0;
		}
		RequestSave();
	}

	void LoadSettings()
	{
		Defaults();

		std::ifstream f(kIni);
		if (!f.good()) {
			SaveSettings();
			return;
		}

		std::unordered_map<std::string, std::string> kv;
		std::string                                  line, sec;
		while (std::getline(f, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#') continue;
			if (line.front() == '[' && line.back() == ']') {
				sec = Lower(line.substr(1, line.size() - 2));
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) continue;
			kv[sec + "." + Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
		}

		for (auto& it : Table()) {
			const auto k = Lower(it.section) + "." + Lower(it.key);
			const auto p = kv.find(k);
			if (p == kv.end()) continue;
			const auto& v = p->second;
			switch (it.type) {
			case T::Bool:
				*static_cast<bool*>(it.ptr) = (v == "1" || Lower(v) == "true" || Lower(v) == "si" || Lower(v) == "yes");
				break;
			case T::Float:
				try {
					*static_cast<float*>(it.ptr) = std::stof(v);
				} catch (...) {}
				break;
			case T::Str: {
				const auto n = std::min(v.size(), it.cap - 1);
				std::memcpy(it.ptr, v.data(), n);
				static_cast<char*>(it.ptr)[n] = 0;
				break;
			}
			}
		}

		{
			std::lock_guard l(gBindMx);
			for (int i = 0; i < kSlots; ++i) {
				const auto sec2 = std::format("slot{}", i + 1);
				if (const auto p = kv.find(sec2 + ".tasti"); p != kv.end()) {
					Binding b;
					if (BindingFromString(p->second, b)) {
						S.bind[i] = b;
					}
				}
				if (const auto p = kv.find(sec2 + ".mossa"); p != kv.end()) {
					const auto n = std::min(p->second.size(), sizeof(S.moveCode[i]) - 1);
					std::memcpy(S.moveCode[i], p->second.data(), n);
					S.moveCode[i][n] = 0;
				}
			}
			gNames.clear();
			for (const auto& [k, v] : kv) {
				if (k.rfind("nomi.", 0) == 0 && !v.empty()) {
					gNames[k.substr(5)] = v;
				}
			}
		}

		// limiti di sicurezza
		S.iniVersion = 1.1f;
		S.evalTimeout = std::clamp(S.evalTimeout, 0.2f, 3.0f);
		S.stopDelay = std::clamp(S.stopDelay, 0.0f, 0.5f);
		S.retries = std::clamp(S.retries, 0.0f, 5.0f);
		S.clearWait = std::clamp(S.clearWait, 0.0f, 2.0f);
		S.maxSeconds = std::clamp(S.maxSeconds, 1.0f, 30.0f);
		S.testDelay = std::clamp(S.testDelay, 0.0f, 10.0f);
		S.staminaCost = std::clamp(S.staminaCost, 0.0f, 1000.0f);
		S.cooldown = std::clamp(S.cooldown, 0.0f, 60.0f);
		if (S.triggerEvents[0] == 0) {
			std::strncpy(S.triggerEvents, "attackPowerStart_Sprint", sizeof(S.triggerEvents) - 1);
		}
		gDirty = true;
	}

	void SaveSettings()
	{
		std::filesystem::create_directories("Data/SKSE/Plugins");
		std::ofstream f(kIni, std::ios::trunc);
		if (!f.good()) {
			SKSE::log::warn("impossibile scrivere {}", kIni);
			return;
		}
		f << "; Impostazioni di Ceneri di Guerra (progetto personale). Si modificano meglio dal menu F1.\n";
		f << "; I tasti si scrivono cosi': Pad_LT+Pad_RT|0.00  (dopo | i secondi di pressione, 0 = istantaneo)\n";
		f << "; Pad: Pad_A/B/X/Y, Pad_LB/RB, Pad_LT/RT, Pad_L3/R3, Pad_Start/Back, Pad_DpadUp/Down/Left/Right\n";
		f << "; Mossa = codice della mossa come nel menu (es. 056, 208-1, S001, W019, A902)\n";
		f << "; [Nomi]: per dare un nome alle mosse senza nome scrivi  codice = nome   (es. 119 = MioNome)\n";
		f << "; [Sezioni]: per spostare una mossa in Fuoco / Combo eleganti (o toglierla con -)\n";
		f << "; [Descrizioni]: per scrivere o cambiare la descrizione di una mossa (appare passando il mouse): codice = testo   (es. 096 = Salto e affondo)\n";
		f << "; [Controlli] SneakTasto: tasto del pad per lo Sneak = giu (predefinito) | L3 (originale: il DLL non tocca i tasti) | su | sinistra | destra | back | start | R3\n";
		f << "; [Controlli] SneakAiuto = 1: se lo Sneak e' stato spostato e il gioco non reagisce, lo fa partire il DLL\n";

		std::string last;
		for (auto& it : Table()) {
			if (last != it.section) {
				f << "\n[" << it.section << "]\n";
				last = it.section;
			}
			f << it.key << " = ";
			switch (it.type) {
			case T::Bool:
				f << (*static_cast<bool*>(it.ptr) ? "1" : "0");
				break;
			case T::Float:
				f << std::format("{:.3f}", *static_cast<float*>(it.ptr));
				break;
			case T::Str:
				f << static_cast<char*>(it.ptr);
				break;
			}
			f << "\n";
		}

		{
			std::lock_guard l(gBindMx);
			for (int i = 0; i < kSlots; ++i) {
				f << "\n[Slot" << (i + 1) << "]\n";
				f << "Tasti = " << BindingToString(S.bind[i]) << "\n";
				f << "Mossa = " << S.moveCode[i] << "\n";
			}
			f << "\n[Preferiti]\n";
			f << "; codici delle mosse con la stellina, separati da virgola (si gestiscono dal menu F1)\n";
			f << "Codici = ";
			for (std::size_t i = 0; i < gFavs.size(); ++i) {
				if (i) f << ",";
				f << gFavs[i];
			}
			f << "\n";
			f << "\n[Sezioni]\n";
			f << "; per spostare una mossa:  codice = Fuoco   oppure   codice = Combo eleganti   oppure   codice = -  (nessuna sezione)\n";
			for (const auto& [k, v] : gSections) {
				f << k << " = " << v << "\n";
			}
			f << "\n[Nomi]\n";
			for (const auto& [k, v] : gNames) {
				f << k << " = " << v << "\n";
			}
			f << "\n[Descrizioni]\n";
			f << "; codice = testo breve (senza accenti, una riga). Sostituisce la descrizione del catalogo.\n";
			for (const auto& [k, v] : gDescs) {
				f << k << " = " << v << "\n";
			}
		}
		gDirty = false;
	}

	void RequestSave() { gDirty = true; }

	bool SaveIfDirty()
	{
		if (gDirty.exchange(false)) {
			SaveSettings();
			return true;
		}
		return false;
	}
}
