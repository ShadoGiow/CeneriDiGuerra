#pragma once

// Mosse speciali: avvio (eventi + condizione OAR), catene, blocco comandi, costi.

#include <string>

namespace cdg::arts
{
	enum class St { Idle, Arming, Playing };

	struct Snapshot
	{
		std::string state{ "pronto" };
		float       stateTime{ 0.0f };
		int         armedId{ 0 };
		std::string armedLabel;
		std::string baseLabel;
		bool        confirmed{ false };
		std::string lastEvent;
		std::string lastResult;
		std::string weapon;
		bool        weaponOk{ false };
		bool        drawn{ false };
		int         attackState{ 0 };
		float       stamina{ 0.0f };
		bool        oarRegistered{ false };
		bool        espOk{ false };
		bool        locked{ false };
		long long   started{ 0 };
		long long   confirmedCount{ 0 };
		float       captureLeft{ 0.0f };
	};

	void Install();
	void OnPreLoad();
	void OnGameLoaded();
	void Tick();  // thread di gioco, ~30 volte al secondo

	// da qualunque thread
	void RequestSlot(int a_slot);                 // come se il tasto dello slot fosse stato premuto
	void RequestMove(int a_id);                   // prova una mossa (dopo il ritardo di prova)
	void RequestAbort();                          // interrompe la mossa in corso
	void RequestEvent(const std::string& a_event);  // prova manuale di un evento d'animazione
	void RequestCapture(float a_seconds);           // registra nel log gli eventi d'attacco del giocatore per N secondi

	// usate dalla condizione OAR (qualunque thread)
	int  ArmedId();
	void NotifyOAREvaluated(int a_id, bool a_hasClip);
	void SetOarRegistered(bool a_ok);

	Snapshot GetSnapshot();
}
