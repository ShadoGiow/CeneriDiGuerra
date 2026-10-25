#include "pch.h"

#include "Catalog.h"
#include "Log.h"
#include "Settings.h"

namespace cdg::catalog
{
	namespace
	{
		constexpr auto kFile = "Data/SKSE/Plugins/CeneriDiGuerra_catalogo.tsv";

		std::vector<Move> gMoves;
		bool              gLoaded{ false };

		std::string Lower(std::string s)
		{
			for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		// il font del menu non ha le lettere accentate: tengo solo ASCII
		std::string Ascii(std::string s)
		{
			for (auto& c : s) {
				if (static_cast<unsigned char>(c) >= 0x80) c = '?';
			}
			return s;
		}

		std::vector<std::string> SplitTab(const std::string& a_line)
		{
			std::vector<std::string> out;
			std::string              cur;
			for (const char c : a_line) {
				if (c == '\t') {
					out.push_back(cur);
					cur.clear();
				} else if (c != '\r' && c != '\n') {
					cur += c;
				}
			}
			out.push_back(cur);
			return out;
		}

		int ToInt(const std::string& s, int a_def = 0)
		{
			try {
				return std::stoi(s);
			} catch (...) {
				return a_def;
			}
		}
	}

	bool Load()
	{
		gMoves.clear();
		gLoaded = false;

		std::ifstream f(kFile);
		if (!f.good()) {
			Say("catalogo: file non trovato: {}", kFile);
			return false;
		}

		std::map<std::string, std::string> names;
		{
			std::lock_guard l(gBindMx);
			names = gNames;
		}

		std::string line;
		while (std::getline(f, line)) {
			if (line.empty() || line[0] == '#') continue;
			const auto c = SplitTab(line);
			if (c.size() < 10) continue;

			Move m;
			m.id = ToInt(c[0]);
			m.code = Ascii(c[1]);
			m.srcKey = c[2];
			m.srcLabel = Ascii(c[3]);
			m.cat = Ascii(c[4]);
			m.label = Ascii(c[5]);
			m.unnamed = c[6] == "1";
			m.weapon = Ascii(c[8]);
			m.also = Ascii(c[9]);

			// regole di catena "mgef:destinazione;mgef:destinazione"
			std::stringstream ss(c[7]);
			std::string       tok;
			while (std::getline(ss, tok, ';')) {
				const auto p = tok.find(':');
				if (p == std::string::npos) continue;
				Upgrade u;
				u.mgef = static_cast<std::uint32_t>(ToInt(tok.substr(0, p)));
				u.to = ToInt(tok.substr(p + 1));
				if (u.mgef && u.to) m.upgrade.push_back(u);
			}

			// nome scelto dall'utente
			if (const auto it = names.find(Lower(m.code)); it != names.end() && !it->second.empty()) {
				const auto nm = Ascii(it->second);
				m.label = m.code + " " + nm;
				m.cat = nm;
				m.unnamed = false;
			}

			if (m.id > 0) gMoves.push_back(std::move(m));
		}

		gLoaded = !gMoves.empty();
		Say("catalogo: {} mosse caricate", gMoves.size());
		return gLoaded;
	}

	bool Loaded() { return gLoaded; }

	const std::vector<Move>& All() { return gMoves; }

	const Move* ById(int a_id)
	{
		for (const auto& m : gMoves) {
			if (m.id == a_id) return &m;
		}
		return nullptr;
	}

	const Move* ByCode(const std::string& a_code)
	{
		const auto k = Lower(a_code);
		if (k.empty()) return nullptr;
		for (const auto& m : gMoves) {
			if (Lower(m.code) == k) return &m;
		}
		return nullptr;
	}
}
