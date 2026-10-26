#include "pch.h"

#include "Arts.h"
#include "Log.h"
#include "OAR.h"
#include "OAR_API/OpenAnimationReplacerAPI-Conditions.h"

OAR_API::Conditions::IConditionsInterface* g_oarConditionsInterface = nullptr;

namespace OAR_API::Conditions
{
	IConditionsInterface* GetAPI(const InterfaceVersion a_version)
	{
		if (g_oarConditionsInterface) {
			return g_oarConditionsInterface;
		}
		const auto module = ::GetModuleHandleA("OpenAnimationReplacer.dll");
		if (!module) {
			return nullptr;
		}
		const auto request = reinterpret_cast<_RequestPluginAPI_Conditions>(
			::GetProcAddress(module, "RequestPluginAPI_Conditions"));
		if (!request) {
			return nullptr;
		}
		const auto plugin = SKSE::PluginDeclaration::GetSingleton();
		g_oarConditionsInterface = request(a_version, plugin->GetName().data(), plugin->GetVersion());
		return g_oarConditionsInterface;
	}
}

namespace
{
	// Vera solo per il giocatore, mentre il plugin ha "armato" proprio questa mossa.
	// Nel config.json di ogni cartella: { "condition": "CeneriTrigger", "Move ID": { "value": N } }
	class CeneriTriggerCondition final : public Conditions::CustomCondition
	{
	public:
		inline static constexpr std::string_view CONDITION_NAME = "CeneriTrigger"sv;

		CeneriTriggerCondition()
		{
			_moveId = static_cast<Conditions::INumericConditionComponent*>(
				AddBaseComponent(Conditions::ConditionComponentType::kNumeric, "Move ID", "Numero della mossa di Ceneri di Guerra"));
		}

		RE::BSString GetName() const override { return CONDITION_NAME.data(); }
		RE::BSString GetDescription() const override
		{
			return "True only for the player while Ceneri di Guerra has armed this Move ID.";
		}
		constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		bool EvaluateImpl(RE::TESObjectREFR* a_refr, RE::hkbClipGenerator* a_clipGenerator, void*) const override
		{
			if (!a_refr || !a_refr->IsPlayerRef() || !_moveId) {
				return false;
			}
			const int armed = cdg::arts::ArmedId();
			if (armed <= 0) {
				return false;
			}
			const int mine = static_cast<int>(std::lround(_moveId->GetNumericValue(a_refr)));
			if (mine != armed) {
				return false;
			}
			cdg::arts::NotifyOAREvaluated(armed, a_clipGenerator != nullptr);
			return true;
		}

	private:
		Conditions::INumericConditionComponent* _moveId{ nullptr };
	};
}

namespace cdg::oar
{
	bool Register()
	{
		const auto result = OAR_API::Conditions::AddCustomCondition<CeneriTriggerCondition>();
		using enum OAR_API::Conditions::APIResult;
		switch (result) {
		case OK:
			Say("OAR: condizione CeneriTrigger registrata");
			arts::SetOarRegistered(true);
			return true;
		case AlreadyRegistered:
			Say("OAR: condizione CeneriTrigger gia' registrata");
			arts::SetOarRegistered(true);
			return true;
		case Invalid:
			Say("OAR: condizione CeneriTrigger non valida");
			return false;
		case Failed:
			Say("OAR: API non disponibile; le mosse non possono partire");
			return false;
		}
		return false;
	}
}
