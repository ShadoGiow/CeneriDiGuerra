#pragma once

// Catalogo delle mosse (generato insieme alle cartelle OAR): Data/SKSE/Plugins/CeneriDiGuerra_catalogo.tsv

#include <cstdint>
#include <string>
#include <vector>

namespace cdg::catalog
{
	// regola di catena: se il giocatore ha l'effetto "mgef" (id locale in CeneriDiGuerra.esp) la mossa passa a "to"
	struct Upgrade
	{
		std::uint32_t mgef{ 0 };
		int           to{ 0 };
	};

	struct Move
	{
		int         id{ 0 };       // numero usato dalla condizione OAR "Move ID"
		std::string code;          // es. "056", "208-1", "S001", "W019", "A902"
		std::string srcKey;        // Pack01 / Pack02 / Pack03 / WA327 / Special / AoW
		std::string srcLabel;      // testo leggibile dell'origine
		std::string cat;           // gruppo (nome senza numero progressivo)
		std::string label;         // "056 Genichiro"
		std::string weapon;        // dettagli facoltativi (arma di origine per Ashes of War)
		std::string also;          // altre cartelle identiche unite a questa
		bool        unnamed{ false };
		std::vector<Upgrade> upgrade;
	};

	bool                     Load();  // legge il file; applica i nomi scelti in [Nomi] dell'INI
	bool                     Loaded();
	const std::vector<Move>& All();
	const Move*              ById(int a_id);
	const Move*              ByCode(const std::string& a_code);  // maiuscole/minuscole indifferenti
}
