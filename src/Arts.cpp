#include "pch.h"

#include "Arts.h"
#include "Catalog.h"
#include "Input.h"
#include "Log.h"
#include "Settings.h"

namespace cdg::arts
{
	namespace
	{
		using clk = std::chrono::steady_clock;

		constexpr auto kEsp = "CeneriDiGuerra.esp";

		// ---------------------------------------------------------------- stato
		std::atomic<int> gSt{ static_cast<int>(St::Idle) };
		float            gStateTime{ 0.0f };

		// orologio di gioco: fermo in pausa (solo thread di gioco)
		double       gClock{ 0.0 };
		std::int64_t gLastTickNs{ 0 };

		std::atomic<int>  gArmed{ 0 };         // mossa armata per la condizione OAR
		std::atomic<bool> gConfirmed{ false }; // OAR ha valutato la clip del giocatore con l'id armato
		std::atomic<bool> gOarOk{ false };
		std::atomic<int>  gLogBudget{ 0 };
		std::atomic<std::int64_t> gCaptureUntilNs{ 0 };  // registrazione eventi richiesta dal menu
		std::atomic<int>          gCaptureBudget{ 0 };

		int    gCur{ 0 };       // mossa (stadio) in corso
		int    gBase{ 0 };      // mossa scelta dal giocatore (prima degli stadi)
		double gMoveStart{ 0.0 };
		double gPlayStart{ 0.0 };
		bool   gSeenAttack{ false };
		double gNoneSince{ -1.0 };

		enum class Ph { None, Stops, Waiting };
		Ph                       gPh{ Ph::None };
		double                   gPhAt{ 0.0 };
		double                   gSentAt{ 0.0 };
		std::size_t              gEvIdx{ 0 };
		std::vector<std::string> gEvList;

		bool   gLocked{ false };
		double gUnlockSince{ 0.0 };
		bool   gUnlockPending{ false };
		double gCooldownUntil{ 0.0 };

		std::atomic<unsigned> gReqSlots{ 0 };
		std::atomic<int>      gReqMove{ 0 };
		std::atomic<bool>     gReqMoveFresh{ false };
		std::atomic<bool>     gReqAbort{ false };
		double                gReqMoveAt{ 0.0 };
		std::mutex            gEvMx;
		std::string           gReqEvent;

		float gSaveTimer{ 0.0f };
		float gSnapTimer{ 0.0f };
		bool  gSinkAttached{ false };

		std::mutex gSnapMx;
		Snapshot   gSnap;
		std::string gLastEvent;
		std::string gLastResult;
		long long   gStarted{ 0 };
		std::atomic<long long> gConfirmedCount{ 0 };

		// ---------------------------------------------------------------- utilita'
		St St_() { return static_cast<St>(gSt.load()); }

		void Enter(St a_s)
		{
			gSt = static_cast<int>(a_s);
			gStateTime = 0.0f;
		}

		const char* Name(St a_s)
		{
			switch (a_s) {
			case St::Idle: return "pronto";
			case St::Arming: return "avvio animazione";
			case St::Playing: return "mossa in corso";
			}
			return "?";
		}

		void Hud(const std::string& a_msg)
		{
			Say("{}", a_msg);
			if (S.showHud) {
				RE::SendHUDMessage::ShowHUDMessage(a_msg.c_str());
			}
		}

		void SetResult(const std::string& a_s)
		{
			std::lock_guard l(gSnapMx);
			gLastResult = a_s;
		}

		float Stamina(RE::PlayerCharacter* a_pc)
		{
			return a_pc->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina);
		}

		void DrainStamina(RE::PlayerCharacter* a_pc, float a_x)
		{
			if (a_x <= 0.0f) return;
			a_pc->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, -a_x);
		}

		bool IsDrawn(RE::PlayerCharacter* a_pc)
		{
			auto* st = a_pc ? a_pc->AsActorState() : nullptr;
			return st && st->IsWeaponDrawn();
		}

		RE::TESObjectWEAP* RightWeapon(RE::PlayerCharacter* a_pc)
		{
			if (!a_pc) return nullptr;
			auto* o = a_pc->GetEquippedObject(false);
			return o ? o->As<RE::TESObjectWEAP>() : nullptr;
		}

		bool WeaponOk(RE::PlayerCharacter* a_pc, std::string* a_why = nullptr)
		{
			if (S.allowAnyWeapon) return true;
			auto* w = RightWeapon(a_pc);
			if (w) {
				const auto t = w->GetWeaponType();
				if (t == RE::WEAPON_TYPE::kTwoHandSword) return true;
				if (S.allow2HAxe && t == RE::WEAPON_TYPE::kTwoHandAxe) return true;
			}
			if (a_why) *a_why = "Serve uno spadone a due mani in mano";
			return false;
		}

		bool HasMgef(RE::PlayerCharacter* a_pc, std::uint32_t a_local)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh || !a_pc) return false;
			auto* eff = dh->LookupForm<RE::EffectSetting>(a_local, kEsp);
			if (!eff) return false;
			auto* mt = a_pc->AsMagicTarget();
			return mt && mt->HasMagicEffect(eff);
		}

		// Replica le condizioni HasMagicEffect dei replacer originali: sceglie lo stadio della catena.
		const catalog::Move* ResolveStage(RE::PlayerCharacter* a_pc, const catalog::Move* a_m)
		{
			if (!a_m || !S.autoChain) return a_m;
			for (int hop = 0; hop < 4; ++hop) {
				const catalog::Move* next = nullptr;
				for (const auto& u : a_m->upgrade) {
					if (HasMgef(a_pc, u.mgef)) {
						next = catalog::ById(u.to);
						break;
					}
				}
				if (!next || next == a_m) break;
				a_m = next;
			}
			return a_m;
		}

		std::vector<std::string> EventList()
		{
			auto v = SplitList(S.triggerEvents);
			if (v.empty()) v.push_back("attackPowerStart_Sprint");
			return v;
		}

		// ---------------------------------------------------------------- comandi
		// NON si toccano mai i flag di ControlMap (kFighting fa rinfoderare l'arma):
		// si disabilitano solo gli handler di attacco/parata, rinfodero e urlo.
		void Lock(bool a_on)
		{
			auto* pcCtrl = RE::PlayerControls::GetSingleton();
			if (!pcCtrl) return;
			if (a_on && !gLocked) {
				if (pcCtrl->attackBlockHandler) pcCtrl->attackBlockHandler->inputEventHandlingEnabled = false;
				if (pcCtrl->readyWeaponHandler) pcCtrl->readyWeaponHandler->inputEventHandlingEnabled = false;
				if (pcCtrl->shoutHandler) pcCtrl->shoutHandler->inputEventHandlingEnabled = false;
				gLocked = true;
				if (S.logEvents) Say("comandi di attacco/parata bloccati durante la mossa");
			} else if (!a_on && gLocked) {
				if (pcCtrl->attackBlockHandler) pcCtrl->attackBlockHandler->inputEventHandlingEnabled = true;
				if (pcCtrl->readyWeaponHandler) pcCtrl->readyWeaponHandler->inputEventHandlingEnabled = true;
				if (pcCtrl->shoutHandler) pcCtrl->shoutHandler->inputEventHandlingEnabled = true;
				gLocked = false;
				if (S.logEvents) Say("comandi ripristinati");
			}
		}

		// ---------------------------------------------------------------- eventi d'animazione
		class AnimSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_ev, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
			{
				if (!a_ev || !a_ev->holder || !a_ev->holder->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
				const auto nowNs = clk::now().time_since_epoch().count();
				const bool capturing = nowNs < gCaptureUntilNs.load() && gCaptureBudget.load() > 0;
				if (!capturing) {
					if (St_() == St::Idle || !S.logEvents) return RE::BSEventNotifyControl::kContinue;
					if (gLogBudget.load() <= 0) return RE::BSEventNotifyControl::kContinue;
					--gLogBudget;
				}
				const std::string tag = a_ev->tag.c_str() ? a_ev->tag.c_str() : "";
				const std::string payload = a_ev->payload.c_str() ? a_ev->payload.c_str() : "";
				if (capturing) {
					// solo gli eventi che interessano (attacco, corsa, parata...), per non riempire il registro
					std::string lt = tag;
					for (auto& c : lt) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					if (lt.find("attack") == std::string::npos && lt.find("sprint") == std::string::npos &&
						lt.find("power") == std::string::npos && lt.find("bash") == std::string::npos &&
						lt.find("block") == std::string::npos && lt.find("swing") == std::string::npos &&
						lt.find("hitframe") == std::string::npos && lt.find("mco") == std::string::npos) {
						return RE::BSEventNotifyControl::kContinue;
					}
					--gCaptureBudget;
					Say("CATTURA evento: tag='{}' payload='{}'", tag, payload);
				} else {
					Say("evento anim: tag='{}' payload='{}'", tag, payload);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		AnimSink gAnimSink;

		void AttachSink(RE::PlayerCharacter* a_pc)
		{
			if (!a_pc || gSinkAttached) return;
			a_pc->AddAnimationGraphEventSink(&gAnimSink);
			gSinkAttached = true;
		}

		// ---------------------------------------------------------------- avvio e fine mossa
		void SendStops(RE::PlayerCharacter* a_pc)
		{
			for (const char* ev : { "blockHitStop", "blockStop", "IdleStop", "attackStop" }) {
				a_pc->NotifyAnimationGraph(ev);
			}
		}

		void SendTrigger(RE::PlayerCharacter* a_pc, double a_now)
		{
			if (gEvIdx >= gEvList.size()) return;
			const auto& ev = gEvList[gEvIdx];
			const bool  ok = a_pc->NotifyAnimationGraph(ev);
			Say("evento '{}' -> {}", ev, ok ? "accettato dal grafo" : "RIFIUTATO dal grafo");
			{
				std::lock_guard l(gSnapMx);
				gLastEvent = ev;
				gLastResult = ok ? "accettato, attendo la conferma di OAR" : "rifiutato dal grafo";
			}
			gSentAt = a_now;
			if (!ok) {
				gSentAt = a_now - static_cast<double>(S.evalTimeout);  // passa subito al successivo
			}
			gPh = Ph::Waiting;
		}

		void End(const std::string& a_why, bool a_ok)
		{
			Say("mossa terminata: {}", a_why);
			SetResult(a_why);
			gArmed = 0;
			gCur = 0;
			gBase = 0;
			gPh = Ph::None;
			gConfirmed = false;
			gSeenAttack = false;
			gNoneSince = -1.0;
			Enter(St::Idle);
			gCooldownUntil = gClock + static_cast<double>(S.cooldown);
			gUnlockSince = gClock;
			gUnlockPending = gLocked;
			(void)a_ok;
		}

		void BeginMove(RE::PlayerCharacter* a_pc, const catalog::Move* a_m, int a_base, bool a_chain)
		{
			if (!a_m) return;
			AttachSink(a_pc);

			gCur = a_m->id;
			gBase = a_base;
			gArmed = a_m->id;
			gConfirmed = false;
			gEvList = EventList();
			gEvIdx = 0;
			gSeenAttack = false;
			gNoneSince = -1.0;
			gMoveStart = gClock;
			gLogBudget = 160;
			gUnlockPending = false;

			if (!a_chain) {
				DrainStamina(a_pc, S.staminaCost);
				++gStarted;
			}
			if (S.lockInput) Lock(true);

			SendStops(a_pc);
			gPh = Ph::Stops;
			gPhAt = gClock + static_cast<double>(S.stopDelay);
			Enter(St::Arming);
			Say("{}: {} (id {}), eventi da provare: {}", a_chain ? "catena" : "avvio", a_m->label, a_m->id, S.triggerEvents);
			SetResult(a_chain ? "catena: stadio successivo" : "avvio in corso");
		}

		bool Gate(RE::PlayerCharacter* a_pc, std::string& a_why)
		{
			a_why.clear();
			if (!S.enabled) { a_why = "Ceneri di Guerra e' disattivato (menu F1)"; return false; }
			if (!a_pc || !a_pc->Is3DLoaded() || a_pc->IsDead()) return false;

			auto* ui = RE::UI::GetSingleton();
			if (ui && ui->GameIsPaused()) return false;
			if (auto* cm = RE::ControlMap::GetSingleton()) {
				if (!cm->AreControlsEnabled(RE::UserEvents::USER_EVENT_FLAG::kFighting)) return false;  // dialoghi, filmati, menu
			}
			if (!gOarOk.load()) { a_why = "Open Animation Replacer non trovato: le mosse non possono partire"; return false; }
			if (!catalog::Loaded()) { a_why = "Catalogo mosse non trovato (CeneriDiGuerra_catalogo.tsv)"; return false; }

			if (a_pc->IsOnMount() || a_pc->IsInKillMove() || a_pc->IsInRagdollState() || a_pc->IsStaggering()) {
				a_why = "Stato del personaggio non adatto";
				return false;
			}
			if (a_pc->IsInMidair()) { a_why = "Serve stare a terra"; return false; }
			if (S.requireDrawn && !IsDrawn(a_pc)) { a_why = "Estrai l'arma"; return false; }
			if (!WeaponOk(a_pc, &a_why)) return false;
			return true;
		}

		void UseMove(RE::PlayerCharacter* a_pc, const catalog::Move* a_m)
		{
			if (!a_m) return;
			const auto st = St_();
			if (st == St::Arming) return;  // sto ancora avviando

			if (st == St::Playing) {
				// catena: stesso slot premuto di nuovo mentre la mossa e' in corso
				if (!S.autoChain || a_m->id != gBase) return;
				if (gClock - gPlayStart < 0.15) return;
				const auto* cur = catalog::ById(gCur);
				const auto* nxt = ResolveStage(a_pc, cur);
				if (nxt && cur && nxt != cur) {
					BeginMove(a_pc, nxt, gBase, true);
				}
				return;
			}

			std::string why;
			if (!Gate(a_pc, why)) {
				if (!why.empty()) Hud(why);
				return;
			}
			if (gClock < gCooldownUntil) return;
			if (S.staminaCost > 0.0f && Stamina(a_pc) < S.staminaCost) {
				Hud("Stamina insufficiente");
				return;
			}
			const auto* stage = ResolveStage(a_pc, a_m);
			BeginMove(a_pc, stage, a_m->id, false);
		}

		void OnSlot(RE::PlayerCharacter* a_pc, int a_slot)
		{
			const auto code = GetMoveCode(a_slot);
			if (code.empty()) {
				Hud(std::format("Slot {} vuoto: scegli una mossa dal menu F1", a_slot + 1));
				return;
			}
			const auto* m = catalog::ByCode(code);
			if (!m) {
				Hud(std::format("Slot {}: mossa '{}' non trovata nel catalogo", a_slot + 1, code));
				return;
			}
			UseMove(a_pc, m);
		}

		void PromoteEvent(std::size_t a_idx)
		{
			if (a_idx == 0 || a_idx >= gEvList.size()) return;
			std::string out = gEvList[a_idx];
			for (std::size_t i = 0; i < gEvList.size(); ++i) {
				if (i != a_idx) out += "," + gEvList[i];
			}
			std::strncpy(S.triggerEvents, out.c_str(), sizeof(S.triggerEvents) - 1);
			S.triggerEvents[sizeof(S.triggerEvents) - 1] = 0;
			RequestSave();
			Say("evento funzionante '{}' messo per primo nell'elenco", gEvList[a_idx]);
		}

		bool CheckInterrupt(RE::PlayerCharacter* a_pc)
		{
			if (!a_pc) return false;
			if (a_pc->IsDead() || a_pc->IsInRagdollState() || a_pc->IsInKillMove() || a_pc->IsOnMount()) {
				End("interrotta (morte, ragdoll, killmove o cavalcatura)", false);
				return true;
			}
			return false;
		}

		void HandleRequests(RE::PlayerCharacter* a_pc)
		{
			unsigned mask = input::TakeSlots() | gReqSlots.exchange(0);
			if (input::CaptureSlot() >= 0) mask = 0;
			if (mask) {
				for (int s = 0; s < kSlots; ++s) {
					if (mask & (1u << s)) {
						OnSlot(a_pc, s);
						break;
					}
				}
			}

			if (gReqAbort.exchange(false) && St_() != St::Idle) {
				a_pc->NotifyAnimationGraph("attackStop");
				End("annullata dal menu", false);
			}

			if (gReqMoveFresh.exchange(false)) {
				gReqMoveAt = gClock + static_cast<double>(S.testDelay);
			}
			if (const int id = gReqMove.load(); id > 0 && gClock >= gReqMoveAt) {
				gReqMove = 0;
				if (const auto* m = catalog::ById(id)) {
					UseMove(a_pc, m);
				}
			}

			if (gCaptureUntilNs.load() > 0) AttachSink(a_pc);

			std::string ev;
			{
				std::lock_guard l(gEvMx);
				ev.swap(gReqEvent);
			}
			if (!ev.empty()) {
				AttachSink(a_pc);
				gLogBudget = 40;
				const bool ok = a_pc->NotifyAnimationGraph(ev);
				Say("prova evento '{}' -> {}", ev, ok ? "accettato" : "rifiutato");
				std::lock_guard l(gSnapMx);
				gLastEvent = ev;
				gLastResult = ok ? "prova evento: accettato" : "prova evento: rifiutato";
			}
		}

		void UpdateSnapshot(RE::PlayerCharacter* a_pc)
		{
			Snapshot s;
			s.state = Name(St_());
			s.stateTime = gStateTime;
			s.armedId = gArmed.load();
			if (const auto* m = catalog::ById(s.armedId)) s.armedLabel = m->label;
			if (const auto* m = catalog::ById(gBase)) s.baseLabel = m->label;
			s.confirmed = gConfirmed.load();
			s.oarRegistered = gOarOk.load();
			s.locked = gLocked;
			s.started = gStarted;
			s.confirmedCount = gConfirmedCount.load();
			{
				const auto nowNs = clk::now().time_since_epoch().count();
				const auto left = gCaptureUntilNs.load() - nowNs;
				s.captureLeft = left > 0 ? static_cast<float>(static_cast<double>(left) * 1e-9) : 0.0f;
			}
			if (a_pc) {
				if (auto* w = RightWeapon(a_pc)) {
					s.weapon = w->GetName() ? w->GetName() : "";
				}
				s.weaponOk = WeaponOk(a_pc);
				s.drawn = IsDrawn(a_pc);
				if (auto* as = a_pc->AsActorState()) {
					s.attackState = static_cast<int>(as->GetAttackState());
				}
				s.stamina = Stamina(a_pc);
			}
			if (auto* dh = RE::TESDataHandler::GetSingleton()) {
				s.espOk = dh->LookupModByName(kEsp) != nullptr;
			}
			std::lock_guard l(gSnapMx);
			s.lastEvent = gLastEvent;
			s.lastResult = gLastResult;
			gSnap = std::move(s);
		}
	}

	// ====================================================================== API pubblica
	void Install()
	{
		Say("mosse: pronte");
	}

	void OnPreLoad()
	{
		gArmed = 0;
		gConfirmed = false;
		gPh = Ph::None;
		gCur = 0;
		gBase = 0;
		gSinkAttached = false;
		Enter(St::Idle);
		Lock(false);
	}

	void OnGameLoaded()
	{
		gArmed = 0;
		gConfirmed = false;
		gPh = Ph::None;
		gCur = 0;
		gBase = 0;
		Enter(St::Idle);
		Lock(false);
		gSinkAttached = false;
		if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
			AttachSink(pc);
		}
		if (auto* dh = RE::TESDataHandler::GetSingleton()) {
			if (!dh->LookupModByName(kEsp)) {
				Say("ATTENZIONE: {} non trovato tra i plugin attivi", kEsp);
			}
		}
	}

	void RequestSlot(int a_slot)
	{
		if (a_slot >= 0 && a_slot < kSlots) gReqSlots |= (1u << a_slot);
	}

	void RequestMove(int a_id)
	{
		gReqMove = a_id;
		gReqMoveFresh = true;
	}

	void RequestAbort() { gReqAbort = true; }

	void RequestEvent(const std::string& a_event)
	{
		std::lock_guard l(gEvMx);
		gReqEvent = a_event;
	}

	void RequestCapture(float a_seconds)
	{
		const auto nowNs = clk::now().time_since_epoch().count();
		gCaptureBudget = 400;
		gCaptureUntilNs = nowNs + static_cast<std::int64_t>(std::clamp(a_seconds, 1.0f, 120.0f) * 1e9f);
		Say("cattura eventi del giocatore per {:.0f} s: fai uno sprint e un attacco potente normale", a_seconds);
	}

	int ArmedId() { return gArmed.load(); }

	void SetOarRegistered(bool a_ok) { gOarOk = a_ok; }

	void NotifyOAREvaluated(int a_id, bool a_hasClip)
	{
		if (!a_hasClip || a_id <= 0 || a_id != gArmed.load()) return;
		if (!gConfirmed.exchange(true)) {
			++gConfirmedCount;
			Say("OAR: clip della mossa {} confermata", a_id);
		}
	}

	Snapshot GetSnapshot()
	{
		std::lock_guard l(gSnapMx);
		return gSnap;
	}

	void Tick()
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) return;

		// ----- orologio
		const auto nowNs = clk::now().time_since_epoch().count();
		float      dt = gLastTickNs > 0 ? static_cast<float>(static_cast<double>(nowNs - gLastTickNs) * 1e-9) : 0.0f;
		dt = std::clamp(dt, 0.0f, 0.25f);
		auto*      ui = RE::UI::GetSingleton();
		const bool paused = ui && ui->GameIsPaused();
		if (paused) dt = 0.0f;
		gLastTickNs = nowNs;
		gClock += static_cast<double>(dt);
		gStateTime += dt;
		const double now = gClock;

		gSaveTimer += dt;
		if (gSaveTimer > 1.0f) {
			gSaveTimer = 0.0f;
			SaveIfDirty();
		}

		if (!pc->Is3DLoaded()) return;

		HandleRequests(pc);

		switch (St_()) {
		case St::Idle:
			if (gLocked) {
				// sblocco quando i tasti sono rilasciati (o dopo 3 s al massimo)
				const double w = now - gUnlockSince;
				if (w > 0.25 && (!input::SlotKeysHeld() || w > 3.0)) {
					Lock(false);
					if (!input::IsHeld(kPadLT)) {
						pc->NotifyAnimationGraph("blockStop");  // evita una parata "rimasta" attiva
					}
					gUnlockPending = false;
				}
			}
			break;

		case St::Arming: {
			if (CheckInterrupt(pc)) break;
			if (now - gMoveStart > static_cast<double>(S.maxSeconds)) {
				End("tempo massimo scaduto prima dell'avvio", false);
				break;
			}
			if (gPh == Ph::Stops) {
				if (now >= gPhAt) SendTrigger(pc, now);
				break;
			}
			if (gPh == Ph::Waiting) {
				if (gConfirmed.load()) {
					gPh = Ph::None;
					gPlayStart = now;
					gSeenAttack = false;
					gNoneSince = -1.0;
					PromoteEvent(gEvIdx);
					Enter(St::Playing);
					SetResult("mossa in corso");
					Say("clip partita dopo {:.2f} s (evento '{}')", now - gMoveStart, gEvIdx < gEvList.size() ? gEvList[gEvIdx] : "?");
					break;
				}
				if (now - gSentAt >= static_cast<double>(S.evalTimeout)) {
					if (gEvIdx + 1 < gEvList.size()) {
						++gEvIdx;
						pc->NotifyAnimationGraph("attackStop");
						gPh = Ph::Stops;
						gPhAt = now + static_cast<double>(S.stopDelay);
						Say("nessuna conferma di OAR: provo l'evento successivo");
					} else {
						Hud("Mossa non partita: vedi Diagnostica nel menu F1");
						End("nessuna clip confermata da OAR con gli eventi provati", false);
					}
				}
			}
			break;
		}

		case St::Playing: {
			if (CheckInterrupt(pc)) break;

			const double elapsed = now - gPlayStart;
			if (now - gMoveStart > static_cast<double>(S.maxSeconds)) {
				pc->NotifyAnimationGraph("attackStop");
				End("tempo massimo raggiunto", true);
				break;
			}

			auto* as = pc->AsActorState();
			const bool attacking = as && as->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone;
			if (attacking) {
				gSeenAttack = true;
				gNoneSince = -1.0;
			} else if (gSeenAttack || elapsed > 1.5) {
				if (gNoneSince < 0.0) {
					gNoneSince = now;
				} else if (now - gNoneSince > 0.5) {
					End("animazione conclusa", true);
					break;
				}
			}
			break;
		}
		}

		gSnapTimer += dt;
		if (gSnapTimer > 0.15f || paused) {
			gSnapTimer = 0.0f;
			UpdateSnapshot(pc);
		}
	}
}
