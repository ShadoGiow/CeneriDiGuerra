#include "pch.h"

#include <set>

#include "Arts.h"
#include "Catalog.h"
#include "Input.h"
#include "Log.h"
#include "Menu.h"
#include "Settings.h"

// Il menu usa le funzioni cimgui esportate da SKSEMenuFramework.dll, collegate a runtime
// (nessuna dipendenza in fase di compilazione). Testi senza accenti per sicurezza sul font.
// Si usano SOLO le funzioni gia' collegate con successo in BladeFireMode.

namespace cdg::menu
{
	namespace
	{
		struct V2 { float x, y; };
		struct V4 { float x, y, z, w; };

		template <class F>
		struct Fn
		{
			F fn{ nullptr };
			bool Bind(REX::W32::HMODULE a_m, const char* a_name)
			{
				fn = reinterpret_cast<F>(REX::W32::GetProcAddress(a_m, a_name));
				if (!fn) {
					Say("menu: funzione mancante nel framework: {}", a_name);
				}
				return fn != nullptr;
			}
		};

		// varianti "V" (va_list), come usa l'header ufficiale del framework
		Fn<void (*)(const char*, va_list)>                     igTextV, igTextWrappedV, igTextDisabledV, igSetTooltipV;
		Fn<void (*)(V4, const char*, va_list)>                 igTextColoredV;
		Fn<void (*)()>                                         igSeparator, igSpacing, igPopID, igEndDisabled, igNewLine;
		Fn<void (*)(const char*)>                              igSeparatorText, igPushID_Str;
		Fn<void (*)(float, float)>                             igSameLine;
		Fn<bool (*)(const char*, V2)>                          igButton;
		Fn<bool (*)(const char*, bool*)>                       igCheckbox;
		Fn<bool (*)(const char*, float*, float, float, const char*, int)> igSliderFloat;
		Fn<bool (*)(const char*, char*, std::size_t, int, void*, void*)> igInputText;
		Fn<bool (*)(const char*, int)>                         igCollapsingHeader;
		Fn<void (*)(bool)>                                     igBeginDisabled;
		Fn<void (*)(float)>                                    igSetNextItemWidth;
		Fn<bool (*)(int)>                                      igIsItemHovered;
		Fn<void (*)(float, V2, const char*)>                   igProgressBar;

		bool gBound{ false };

		constexpr int kSliderClamp = 1 << 4;
		constexpr int kOpen = 1 << 5;

		constexpr V4 kGreen{ 0.35f, 0.95f, 0.45f, 1.0f };
		constexpr V4 kRed{ 1.0f, 0.4f, 0.3f, 1.0f };
		constexpr V4 kOrange{ 1.0f, 0.65f, 0.2f, 1.0f };
		constexpr V4 kGrey{ 0.7f, 0.7f, 0.7f, 1.0f };

		bool BindAll()
		{
			const auto m = REX::W32::GetModuleHandleA("SKSEMenuFramework.dll");
			if (!m) {
				Say("menu: SKSE Menu Framework non trovato (il plugin funziona lo stesso, con INI)");
				return false;
			}
			bool ok = true;
			ok &= igTextV.Bind(m, "igTextV");
			ok &= igTextWrappedV.Bind(m, "igTextWrappedV");
			ok &= igTextDisabledV.Bind(m, "igTextDisabledV");
			ok &= igTextColoredV.Bind(m, "igTextColoredV");
			ok &= igSeparator.Bind(m, "igSeparator");
			ok &= igSpacing.Bind(m, "igSpacing");
			ok &= igNewLine.Bind(m, "igNewLine");
			ok &= igPopID.Bind(m, "igPopID");
			ok &= igEndDisabled.Bind(m, "igEndDisabled");
			ok &= igSeparatorText.Bind(m, "igSeparatorText");
			ok &= igPushID_Str.Bind(m, "igPushID_Str");
			ok &= igSameLine.Bind(m, "igSameLine");
			ok &= igButton.Bind(m, "igButton");
			ok &= igCheckbox.Bind(m, "igCheckbox");
			ok &= igSliderFloat.Bind(m, "igSliderFloat");
			ok &= igInputText.Bind(m, "igInputText");
			ok &= igCollapsingHeader.Bind(m, "igCollapsingHeader_TreeNodeFlags");
			ok &= igBeginDisabled.Bind(m, "igBeginDisabled");
			ok &= igSetNextItemWidth.Bind(m, "igSetNextItemWidth");
			ok &= igIsItemHovered.Bind(m, "igIsItemHovered");
			ok &= igSetTooltipV.Bind(m, "igSetTooltipV");
			ok &= igProgressBar.Bind(m, "igProgressBar");
			return ok;
		}

		void TextF(const char* a_fmt, ...)
		{
			va_list a;
			va_start(a, a_fmt);
			igTextV.fn(a_fmt, a);
			va_end(a);
		}
		void TextWrappedF(const char* a_fmt, ...)
		{
			va_list a;
			va_start(a, a_fmt);
			igTextWrappedV.fn(a_fmt, a);
			va_end(a);
		}
		void TextDisabledF(const char* a_fmt, ...)
		{
			va_list a;
			va_start(a, a_fmt);
			igTextDisabledV.fn(a_fmt, a);
			va_end(a);
		}
		void TooltipF(const char* a_fmt, ...)
		{
			va_list a;
			va_start(a, a_fmt);
			igSetTooltipV.fn(a_fmt, a);
			va_end(a);
		}
		void TextColoredF(V4 a_col, const char* a_fmt, ...)
		{
			va_list a;
			va_start(a, a_fmt);
			igTextColoredV.fn(a_col, a_fmt, a);
			va_end(a);
		}

		// ----------------------------------------------------------- helper di interfaccia
		void Tip(const char* a_text)
		{
			if (igIsItemHovered.fn(0)) {
				TooltipF("%s", a_text);
			}
		}

		bool Check(const char* a_label, bool& a_v, const char* a_tip = nullptr)
		{
			const bool ch = igCheckbox.fn(a_label, &a_v);
			if (a_tip) Tip(a_tip);
			if (ch) RequestSave();
			return ch;
		}

		bool Slider(const char* a_label, float& a_v, float a_min, float a_max, const char* a_fmt, const char* a_tip = nullptr)
		{
			igSetNextItemWidth.fn(320.0f);
			const bool ch = igSliderFloat.fn(a_label, &a_v, a_min, a_max, a_fmt, kSliderClamp);
			if (a_tip) Tip(a_tip);
			if (ch) RequestSave();
			return ch;
		}

		bool Text(const char* a_label, char* a_buf, std::size_t a_size, const char* a_tip = nullptr, bool a_save = true)
		{
			igSetNextItemWidth.fn(420.0f);
			const bool ch = igInputText.fn(a_label, a_buf, a_size, 0, nullptr, nullptr);
			if (a_tip) Tip(a_tip);
			if (ch && a_save) RequestSave();
			return ch;
		}

		bool Button(const char* a_label) { return igButton.fn(a_label, V2{ 0.0f, 0.0f }); }

		std::string Lower(std::string s)
		{
			for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		// ----------------------------------------------------------- stato del menu
		bool gByName{ false };  // elenco unico per nome invece che per sezioni/origine
		char gFilter[64]{};
		char gTestEvent[96]{ "attackPowerStart_Sprint" };
		bool gPrevCtrlF{ false };
		bool gCtrlF{ false };  // Ctrl+F appena premuto (valido per un solo fotogramma)

		struct Group
		{
			std::string      title;
			std::string      key;  // identificatore stabile (l'intestazione cambia numero, l'id no)
			std::vector<int> idx;  // indici in catalog::All()
		};
		std::vector<Group>         gSecGroups;  // sezioni trasversali: Combo eleganti, Fuoco
		std::vector<Group>         gGroups;     // per origine (senza le mosse gia' messe in una sezione)
		std::vector<int>           gAllByName;
		std::map<std::string, int> gCatCount;

		void BuildIndex()
		{
			gSecGroups.clear();
			gGroups.clear();
			gAllByName.clear();
			gCatCount.clear();
			const auto& all = catalog::All();

			auto less = [&](int a, int b) {
				const auto ca = Lower(all[a].cat);
				const auto cb = Lower(all[b].cat);
				if (ca != cb) return ca < cb;
				return all[a].id < all[b].id;
			};

			// sezioni trasversali ai pacchetti (ordine fisso)
			for (const char* name : { "Combo eleganti", "Fuoco" }) {
				Group g;
				g.key = name;
				g.title = name;
				for (int i = 0; i < static_cast<int>(all.size()); ++i) {
					if (all[i].sect == name) g.idx.push_back(i);
				}
				if (g.idx.empty()) continue;
				std::sort(g.idx.begin(), g.idx.end(), less);
				gSecGroups.push_back(std::move(g));
			}

			// origine: solo le mosse che non stanno in una sezione
			std::vector<std::string> keys;
			for (const auto& m : all) {
				if (std::find(keys.begin(), keys.end(), m.srcKey) == keys.end()) keys.push_back(m.srcKey);
			}
			for (const auto& k : keys) {
				Group g;
				g.key = k;
				for (int i = 0; i < static_cast<int>(all.size()); ++i) {
					if (all[i].srcKey == k && all[i].sect.empty()) {
						if (g.title.empty()) g.title = all[i].srcLabel;
						g.idx.push_back(i);
					}
				}
				if (g.idx.empty()) continue;
				std::sort(g.idx.begin(), g.idx.end(), less);
				gGroups.push_back(std::move(g));
			}
			for (int i = 0; i < static_cast<int>(all.size()); ++i) {
				gAllByName.push_back(i);
				++gCatCount[Lower(all[i].cat)];
			}
			std::sort(gAllByName.begin(), gAllByName.end(), less);
		}

		bool Matches(const catalog::Move& m, const std::string& a_filterLower)
		{
			if (a_filterLower.empty()) return true;
			const auto hay = Lower(m.label + " " + m.cat + " " + m.srcLabel + " " + m.code + " " + m.weapon + " " + m.sect);
			return hay.find(a_filterLower) != std::string::npos;
		}

		// Ctrl+F con il mouse sopra una riga = metti/togli la stella (come in Photo Mode)
		void UpdateHotkeys()
		{
			const bool down = input::IsHeld(0x146) && input::IsHeld(0x111);
			gCtrlF = down && !gPrevCtrlF;
			gPrevCtrlF = down;
		}

		void BindingRow(int a_slot)
		{
			igPushID_Str.fn(std::format("bind{}", a_slot).c_str());

			std::string label;
			float       hold;
			{
				std::lock_guard l(gBindMx);
				label = BindingLabel(S.bind[a_slot]);
				hold = S.bind[a_slot].hold;
			}
			const bool capturing = input::CaptureSlot() == a_slot;
			if (capturing) {
				TextColoredF(kOrange, "In attesa... premi la combinazione (pad o tastiera) e rilasciala. Esc = annulla");
				const auto live = input::CaptureLive();
				TextF("  adesso: %s", live.empty() ? "(niente)" : live.c_str());
				if (Button("Annulla")) input::CancelCapture();
			} else {
				TextF("Tasti: %s", label.c_str());
				if (Button("Registra nuova combinazione")) input::BeginCapture(a_slot);
				igSameLine.fn(0.0f, 8.0f);
				if (Button("Cancella tasti")) {
					std::lock_guard l(gBindMx);
					S.bind[a_slot].keys.clear();
					RequestSave();
				}
			}

			if (Slider("Tempo di pressione (s)##hold", hold, 0.0f, 2.0f, "%.2f",
					"Per quanti secondi vanno tenuti premuti i tasti. 0 = istantaneo (consigliato per le combinazioni con due tasti)")) {
				std::lock_guard l(gBindMx);
				S.bind[a_slot].hold = hold;
			}
			igPopID.fn();
		}

		// Una riga: [*] stella | 1 2 3 = mettila nello slot | Prova | nome
		void MoveRow(const catalog::Move& m, const std::string codes[kSlots], const std::set<std::string>& a_favs, bool a_showOrigin)
		{
			igPushID_Str.fn(std::format("mv{}", m.id).c_str());
			const auto lc = Lower(m.code);
			const bool fav = a_favs.count(lc) != 0;
			bool       hov = false;

			if (Button(fav ? "[*]##fav" : "[ ]##fav")) SetFavorite(m.code, !fav);
			hov |= igIsItemHovered.fn(0);
			Tip(fav ? "Preferito: clicca per togliere la stella (anche Ctrl+F col mouse sopra la riga)"
					: "Metti la stella: la mossa compare anche nella sezione Preferiti (anche Ctrl+F col mouse sopra la riga)");

			std::string marks;
			for (int i = 0; i < kSlots; ++i) {
				igSameLine.fn(0.0f, 4.0f);
				const bool here = !codes[i].empty() && Lower(codes[i]) == lc;
				const auto lab = std::format("{}##s{}", here ? std::format("[{}]", i + 1) : std::format(" {} ", i + 1), i);
				if (Button(lab.c_str())) SetMoveCode(i, m.code);
				hov |= igIsItemHovered.fn(0);
				Tip(std::format("Metti questa mossa nello Slot {}", i + 1).c_str());
				if (here) marks += std::format(" <- Slot {}", i + 1);
			}

			igSameLine.fn(0.0f, 8.0f);
			if (Button("Prova##t")) arts::RequestMove(m.id);
			hov |= igIsItemHovered.fn(0);
			Tip("Avvia la mossa dopo il ritardo di prova (hai il tempo di chiudere il menu)");

			igSameLine.fn(0.0f, 8.0f);
			if (!marks.empty()) {
				TextColoredF(kGreen, "%s%s", m.label.c_str(), marks.c_str());
			} else if (m.unnamed) {
				TextColoredF(kOrange, "%s", m.label.c_str());
			} else {
				TextF("%s", m.label.c_str());
			}
			hov |= igIsItemHovered.fn(0);
			if (a_showOrigin) {
				igSameLine.fn(0.0f, 8.0f);
				TextDisabledF("(%s)", m.srcLabel.c_str());
			}
			std::string tip = m.srcLabel;
			if (!m.weapon.empty()) tip += "  -  " + m.weapon;
			if (!m.also.empty()) tip += "\nIdentica a: " + m.also;
			if (!m.upgrade.empty()) tip += "\nMossa a catena: passa da sola agli stadi successivi se premi di nuovo";
			if (!m.sect.empty()) tip += "\nSezione: " + m.sect;
			if (igIsItemHovered.fn(0)) TooltipF("%s", tip.c_str());

			if (gCtrlF && hov) {
				SetFavorite(m.code, !fav);
				gCtrlF = false;
			}
			igPopID.fn();
		}

		void DrawList(const std::vector<int>& a_idx, const std::string& a_filterLower, const std::string codes[kSlots],
			const std::set<std::string>& a_favs, bool a_showOrigin, bool a_subheads)
		{
			const auto& all = catalog::All();
			std::string prevCat;
			for (const int i : a_idx) {
				const auto& m = all[i];
				if (!Matches(m, a_filterLower)) continue;
				if (a_subheads) {
					const auto lc = Lower(m.cat);
					if (lc != prevCat) {
						prevCat = lc;
						const auto it = gCatCount.find(lc);
						if (it != gCatCount.end() && it->second > 1 && a_filterLower.empty()) {
							TextColoredF(kGrey, "-- %s --", m.cat.c_str());
						}
					}
				}
				MoveRow(m, codes, a_favs, a_showOrigin);
			}
		}

		// intestazione apribile con id stabile (il numero fra parentesi puo' cambiare)
		bool Header(const std::string& a_title, std::size_t a_count, const std::string& a_key, bool a_open)
		{
			const auto lab = std::format("{} ({})###{}", a_title, a_count, a_key);
			return igCollapsingHeader.fn(lab.c_str(), a_open ? kOpen : 0);
		}

		// ----------------------------------------------------------- pagine
		void __stdcall PageSlots()
		{
			UpdateHotkeys();
			const auto s = arts::GetSnapshot();

			igSeparatorText.fn("Stato");
			TextF("Mossa: %s", s.state.c_str());
			if (s.oarRegistered) {
				TextColoredF(kGreen, "Open Animation Replacer: condizione registrata");
			} else {
				TextColoredF(kRed, "Open Animation Replacer: NON disponibile (le mosse non partono)");
			}
			if (s.espOk) {
				TextColoredF(kGreen, "CeneriDiGuerra.esp: caricato");
			} else {
				TextColoredF(kRed, "CeneriDiGuerra.esp: NON trovato tra i plugin attivi");
			}
			if (s.weaponOk) {
				TextColoredF(kGreen, "Arma in mano destra: %s", s.weapon.empty() ? "(qualunque)" : s.weapon.c_str());
			} else {
				TextColoredF(kOrange, "Arma in mano destra: %s  - serve uno spadone a due mani", s.weapon.empty() ? "nessuna" : s.weapon.c_str());
			}
			TextDisabledF(input::PadConnected() ? "Pad rilevato" : "Pad non rilevato (solo tastiera)");
			Check("Mosse attive##en", S.enabled);

			std::string codes[kSlots];
			for (int i = 0; i < kSlots; ++i) codes[i] = GetMoveCode(i);
			std::set<std::string> favs;
			for (const auto& f : Favorites()) favs.insert(Lower(f));

			// ---- i tre slot, in breve
			igSeparatorText.fn("I tuoi 3 slot");
			TextWrappedF("Per mettere una mossa in uno slot: nell'elenco qui sotto premi il numero 1, 2 o 3 accanto alla mossa. Il numero tra parentesi quadre [2] vuol dire che la mossa e' gia' in quello slot.");
			for (int i = 0; i < kSlots; ++i) {
				igPushID_Str.fn(std::format("slot{}", i).c_str());
				const auto* m = codes[i].empty() ? nullptr : catalog::ByCode(codes[i]);
				std::string keys;
				{
					std::lock_guard l(gBindMx);
					keys = BindingLabel(S.bind[i]);
				}
				if (m) {
					TextColoredF(kGreen, "Slot %d: %s", i + 1, m->label.c_str());
				} else if (!codes[i].empty()) {
					TextColoredF(kRed, "Slot %d: mossa '%s' non trovata nel catalogo", i + 1, codes[i].c_str());
				} else {
					TextColoredF(kOrange, "Slot %d: (vuoto)", i + 1);
				}
				TextDisabledF("      tasti: %s", keys.c_str());
				if (m) {
					igSameLine.fn(0.0f, 12.0f);
					if (Button("Prova")) arts::RequestMove(m->id);
					Tip("Avvia la mossa dopo il ritardo di prova, cosi' puoi chiudere il menu");
				}
				if (!codes[i].empty()) {
					igSameLine.fn(0.0f, 8.0f);
					if (Button("Svuota")) SetMoveCode(i, "");
				}
				const auto hdr = std::format("Cambia i tasti dello Slot {}###keys{}", i + 1, i);
				if (igCollapsingHeader.fn(hdr.c_str(), 0)) {
					BindingRow(i);
				}
				igPopID.fn();
			}

			igSpacing.fn();
			TextF("Tasti pronti:");
			igSameLine.fn(0.0f, 8.0f);
			if (Button("L3 (tenuto) / L3+RT / L3+RB##presetL3")) ApplyKeyPreset(1);
			Tip("Usa L3 al posto di LT (LT resta libero per la parata). Serve il patch che sposta lo Sneak su Freccia giu, altrimenti L3 fa anche accovacciare");
			igSameLine.fn(0.0f, 8.0f);
			if (Button("LT (tenuto) / LT+RT / LT+RB##presetLT")) ApplyKeyPreset(0);
			Tip("La versione 1.0: LT tenuto premuto. Occupa il tasto della parata");

			// ---- elenco delle mosse
			igSeparatorText.fn("Scegli le mosse");
			Text("Cerca (nome, numero, origine)##flt", gFilter, sizeof(gFilter), "Scrivi una parte del nome o del numero, es. 208, Slash, Elden, fuoco", false);
			igSameLine.fn(0.0f, 8.0f);
			if (Button("Pulisci")) gFilter[0] = 0;
			igCheckbox.fn("Elenco unico ordinato per nome (senza sezioni)##byname", &gByName);
			TextDisabledF("[*] = preferito (stellina). Ctrl+F con il mouse sopra una mossa = metti/togli la stella. Passa il mouse sul nome per origine e dettagli.");

			if (!catalog::Loaded() || (gGroups.empty() && gSecGroups.empty())) {
				TextColoredF(kRed, "Catalogo mosse non caricato (manca CeneriDiGuerra_catalogo.tsv in SKSE/Plugins)");
				return;
			}

			const auto flt = Lower(gFilter);
			if (!flt.empty() || gByName) {
				DrawList(gAllByName, flt, codes, favs, true, false);
				return;
			}

			// Preferiti: tutte le mosse con la stella, da qualunque pacchetto
			{
				const auto& all = catalog::All();
				std::vector<int> fv;
				for (int i = 0; i < static_cast<int>(all.size()); ++i) {
					if (favs.count(Lower(all[i].code))) fv.push_back(i);
				}
				std::sort(fv.begin(), fv.end(), [&](int a, int b) { return all[a].id < all[b].id; });
				if (Header("Preferiti", fv.size(), "pref", true)) {
					if (fv.empty()) {
						TextDisabledF("Nessuna mossa preferita: premi [ ] accanto a una mossa per metterla qui.");
					} else {
						DrawList(fv, flt, codes, favs, true, false);
					}
				}
			}

			for (const auto& g : gSecGroups) {
				if (Header(g.title, g.idx.size(), "sec_" + g.key, false)) {
					DrawList(g.idx, flt, codes, favs, true, false);
				}
			}
			for (std::size_t g = 0; g < gGroups.size(); ++g) {
				if (Header(gGroups[g].title, gGroups[g].idx.size(), "src_" + gGroups[g].key, false)) {
					DrawList(gGroups[g].idx, flt, codes, favs, false, true);
				}
			}
		}

		void __stdcall PageSettings()
		{
			igSeparatorText.fn("Generale");
			Check("Mosse attive##en2", S.enabled);
			Check("Solo con l'arma estratta##drawn", S.requireDrawn);
			Check("Consenti anche asce e martelli a due mani##axe", S.allow2HAxe,
				"Le clip sostituite sono pensate per lo spadone: con asce/martelli funzionano solo se il tuo set MCO usa gli stessi nomi di clip");
			Check("Consenti qualunque arma##any", S.allowAnyWeapon,
				"Per prova: l'animazione parte solo se il grafo dell'arma usa una delle clip sostituite");
			Check("Messaggi a schermo##hud", S.showHud);

			igSeparatorText.fn("Avvio della mossa");
			Text("Eventi d'animazione da provare (separati da virgola)##events", S.triggerEvents, sizeof(S.triggerEvents),
				"Il primo che fa partire la clip (confermata da OAR) viene messo in testa all'elenco automaticamente");
			Slider("Attesa conferma OAR (s)##evalto", S.evalTimeout, 0.2f, 3.0f, "%.2f",
				"Quanto aspettare la conferma di OAR prima di provare l'evento successivo");
			Slider("Pausa fra stop e attacco (s)##stopdl", S.stopDelay, 0.0f, 0.5f, "%.2f");
			Slider("Durata massima di una mossa (s)##maxsec", S.maxSeconds, 1.0f, 30.0f, "%.1f",
				"Sicurezza: dopo questo tempo la mossa viene chiusa comunque");
			Slider("Ritardo del pulsante Prova (s)##testdl", S.testDelay, 0.0f, 10.0f, "%.1f");
			Check("Catena automatica##chain", S.autoChain,
				"Le mosse a piu' stadi (es. DoppioSlash 2/3/4, Aereo 1/2) avanzano da sole se premi di nuovo lo stesso slot nella finestra giusta");
			Check("Blocca attacco/parata/rinfodero durante la mossa##lock", S.lockInput,
				"Non rinfodera mai l'arma: disabilita solo i gestori di attacco, parata, rinfodero e urlo");

			igSeparatorText.fn("Costi");
			Slider("Stamina per mossa##stam", S.staminaCost, 0.0f, 200.0f, "%.0f", "0 = nessun costo");
			Slider("Attesa fra una mossa e l'altra (s)##cd", S.cooldown, 0.0f, 20.0f, "%.1f", "0 = nessuna attesa");

			igSeparatorText.fn("File");
			TextDisabledF("Impostazioni: Data/SKSE/Plugins/CeneriDiGuerra.ini");
			TextDisabledF("Per dare un nome alle mosse senza nome: sezione [Nomi] dell'INI, es.  119 = MioNome");
			TextDisabledF("Per spostare una mossa tra le sezioni: sezione [Sezioni] dell'INI, es.  A956 = Combo eleganti   oppure   096 = -  (toglie la sezione). Vale al prossimo avvio.");
			TextDisabledF("Le stelline (preferiti) sono salvate nella sezione [Preferiti] dell'INI.");
			if (Button("Salva impostazioni ora")) SaveSettings();
		}

		void __stdcall PageDiag()
		{
			const auto s = arts::GetSnapshot();

			igSeparatorText.fn("Stato");
			TextF("Stato: %s  (da %.1f s)", s.state.c_str(), s.stateTime);
			TextF("Mossa armata: %d %s", s.armedId, s.armedLabel.c_str());
			TextF("Mossa scelta: %s", s.baseLabel.empty() ? "-" : s.baseLabel.c_str());
			TextF("Conferma OAR sulla clip: %s", s.confirmed ? "si" : "no");
			TextF("Ultimo evento inviato: %s", s.lastEvent.empty() ? "-" : s.lastEvent.c_str());
			TextF("Esito: %s", s.lastResult.empty() ? "-" : s.lastResult.c_str());
			TextF("Stato di attacco del personaggio: %d   arma estratta: %s   comandi bloccati: %s", s.attackState,
				s.drawn ? "si" : "no", s.locked ? "si" : "no");
			TextF("Stamina: %.0f", s.stamina);
			TextF("Mosse avviate: %lld   clip confermate da OAR: %lld", s.started, s.confirmedCount);
			if (Button("Interrompi mossa")) arts::RequestAbort();

			igSeparatorText.fn("Prova di un evento d'animazione");
			Text("Evento##testev", gTestEvent, sizeof(gTestEvent), "Invia questo evento al grafo del giocatore (per capire quali sono accettati)", false);
			igSameLine.fn(0.0f, 8.0f);
			if (Button("Invia")) arts::RequestEvent(gTestEvent);
			TextDisabledF("Attenzione: la prova NON arma nessuna mossa, quindi vedrai solo l'animazione normale di Skyrim.");

			igSeparatorText.fn("Cattura degli eventi del gioco");
			TextWrappedF("Se le mosse non partono: premi il pulsante, poi nel gioco corri (sprint) e fai un attacco potente normale. Gli eventi d'attacco vengono scritti nel registro: il nome che compare vicino a 'attackPower...' e' quello da mettere nell'elenco eventi in Impostazioni.");
			if (Button("Registra eventi per 25 secondi")) arts::RequestCapture(25.0f);
			if (s.captureLeft > 0.0f) {
				igSameLine.fn(0.0f, 8.0f);
				TextColoredF(kOrange, "registrazione in corso: %.0f s", s.captureLeft);
			}

			igSeparatorText.fn("Registro");
			Check("Scrivi gli eventi di animazione nel registro##logev", S.logEvents,
				"Mostra quali annotazioni arrivano durante la mossa");
			if (Button("Svuota registro")) LogClear();
			igSameLine.fn(0.0f, 8.0f);
			if (Button("Salva impostazioni ora")) SaveSettings();
			igSpacing.fn();
			for (auto& l : LogSnapshot()) {
				TextWrappedF("%s", l.c_str());
			}
			igSpacing.fn();
			TextDisabledF("File completo: Documents/My Games/Skyrim Special Edition/SKSE/CeneriDiGuerra.log");
		}
	}

	bool Register()
	{
		if (gBound) return true;
		if (!BindAll()) {
			return false;
		}
		const auto add = reinterpret_cast<void (*)(const char*, void (*)())>(
			REX::W32::GetProcAddress(REX::W32::GetModuleHandleA("SKSEMenuFramework.dll"), "AddSectionItem"));
		if (!add) {
			Say("menu: AddSectionItem non trovato");
			return false;
		}
		BuildIndex();
		add("Ceneri di Guerra/Slot e Mosse", PageSlots);
		add("Ceneri di Guerra/Impostazioni", PageSettings);
		add("Ceneri di Guerra/Diagnostica", PageDiag);
		gBound = true;
		Say("menu F1: pagine registrate");
		return true;
	}
}
