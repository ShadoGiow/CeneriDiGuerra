#pragma once

namespace cdg::oar
{
	// Registra la condizione OAR "CeneriTrigger" durante SKSE kPostLoad.
	// Restituisce false quando OAR non e' installato o l'API non e' disponibile.
	bool Register();
}
