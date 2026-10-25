#include "pch.h"

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include "Input.h"
#include "Log.h"

namespace cdg::input
{
	namespace
	{
		struct XState
		{
			std::uint32_t packet;
			std::uint16_t buttons;
			std::uint8_t  lt, rt;
			std::int16_t  lx, ly, rx, ry;
		};

		using XInputGetStateFn = std::uint32_t(__stdcall*)(std::uint32_t, XState*);

		XInputGetStateFn gXGet{ nullptr };

		std::atomic<bool> gRun{ false };
		std::atomic<unsigned> gMask{ 0 };
		std::atomic<bool> gPad{ false };
		std::atomic<bool> gHeldAny{ false };

		std::atomic<int>     gCapSlot{ -1 };
		std::mutex           gCapMx;
		std::vector<InputId> gCapMax;   // massima combinazione vista
		std::vector<InputId> gCapLive;  // premuti ora
		bool                 gCapAny{ false };
		float                gCapWait{ 0.0f };

		std::mutex           gPressedMx;
		std::vector<InputId> gPressedNow;

		struct Track
		{
			float heldFor{ 0.0f };
			bool  fired{ false };
			bool  blocked{ false };  // bloccato da una combinazione piu' grande, fino al rilascio
		};

		void Bind()
		{
			for (auto name : { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" }) {
				if (auto h = ::LoadLibraryA(name)) {
					gXGet = reinterpret_cast<XInputGetStateFn>(::GetProcAddress(h, "XInputGetState"));
					if (gXGet) {
						Say("lettura pad: {}", name);
						return;
					}
				}
			}
			Say("lettura pad: XInput non trovato (resta la tastiera)");
		}

		bool GameFocused()
		{
			const auto h = ::GetForegroundWindow();
			if (!h) return false;
			DWORD pid = 0;
			::GetWindowThreadProcessId(h, &pid);
			return pid == ::GetCurrentProcessId();
		}

		// tutti gli ingressi premuti ora (ordinati)
		std::vector<InputId> Pressed(bool a_focused)
		{
			std::vector<InputId> out;
			if (!a_focused) return out;

			if (gXGet) {
				std::uint16_t b = 0;
				std::uint8_t  lt = 0, rt = 0;
				bool          any = false;
				for (std::uint32_t i = 0; i < 4; ++i) {
					XState st{};
					if (gXGet(i, &st) == 0) {
						any = true;
						b |= st.buttons;
						lt = std::max(lt, st.lt);
						rt = std::max(rt, st.rt);
					}
				}
				gPad = any;
				struct M { std::uint16_t mask; InputId id; };
				static constexpr M map[] = {
					{ 0x0001, 1 }, { 0x0002, 2 }, { 0x0004, 3 }, { 0x0008, 4 }, { 0x0010, 5 }, { 0x0020, 6 },
					{ 0x0040, 7 }, { 0x0080, 8 }, { 0x0100, 9 }, { 0x0200, 10 }, { 0x1000, 11 }, { 0x2000, 12 },
					{ 0x4000, 13 }, { 0x8000, 14 },
				};
				for (auto& m : map) {
					if (b & m.mask) out.push_back(m.id);
				}
				if (lt > 90) out.push_back(kPadLT);
				if (rt > 90) out.push_back(kPadRT);
			}

			// tastiera + mouse: VK 1..0xFE (salto i tasti "generici" che raddoppiano gli specifici)
			for (int vk = 1; vk < 0xFF; ++vk) {
				if (vk == 0x10 || vk == 0x11 || vk == 0x12) continue;  // Shift/Ctrl/Alt generici: uso L/R
				if (::GetAsyncKeyState(vk) & 0x8000) {
					out.push_back(static_cast<InputId>(0x100 + vk));
				}
			}
			if ((::GetAsyncKeyState(0xA0) | ::GetAsyncKeyState(0xA1)) & 0x8000) out.push_back(0x110);
			if ((::GetAsyncKeyState(0xA2) | ::GetAsyncKeyState(0xA3)) & 0x8000) out.push_back(0x111);
			if ((::GetAsyncKeyState(0xA4) | ::GetAsyncKeyState(0xA5)) & 0x8000) out.push_back(0x112);

			// tolgo i duplicati L/R
			out.erase(std::remove_if(out.begin(), out.end(), [](InputId i) {
						  const auto vk = static_cast<int>(i) - 0x100;
						  return i >= 0x100 && (vk >= 0xA0 && vk <= 0xA5);
					  }),
				out.end());

			std::sort(out.begin(), out.end());
			out.erase(std::unique(out.begin(), out.end()), out.end());
			return out;
		}

		bool Contains(const std::vector<InputId>& a_v, InputId a_k)
		{
			return std::find(a_v.begin(), a_v.end(), a_k) != a_v.end();
		}

		bool ChordDown(const Binding& a_b, const std::vector<InputId>& a_pressed)
		{
			if (a_b.empty()) return false;
			for (auto k : a_b.keys) {
				if (!Contains(a_pressed, k)) return false;
			}
			return true;
		}

		bool IsSubset(const std::vector<InputId>& a_small, const std::vector<InputId>& a_big)
		{
			for (auto k : a_small) {
				if (!Contains(a_big, k)) return false;
			}
			return true;
		}

		void CaptureStep(int a_slot, const std::vector<InputId>& a_pressed, float a_dt)
		{
			std::lock_guard l(gCapMx);
			gCapLive = a_pressed;

			// Esc annulla
			if (Contains(a_pressed, 0x11B)) {
				gCapSlot = -1;
				gCapMax.clear();
				gCapAny = false;
				return;
			}

			if (!a_pressed.empty()) {
				gCapAny = true;
				gCapWait = 0.0f;
				// tengo la combinazione piu' ampia vista (max 5)
				if (a_pressed.size() >= gCapMax.size() && a_pressed.size() <= 5) {
					gCapMax = a_pressed;
				}
				return;
			}

			if (gCapAny) {
				gCapWait += a_dt;
				if (gCapWait > 0.15f) {
					Binding b;
					b.keys = gCapMax;
					{
						std::lock_guard bl(gBindMx);
						// conservo il tempo di pressione scelto in precedenza
						b.hold = S.bind[a_slot].hold;
						S.bind[a_slot] = b;
					}
					RequestSave();
					Say("slot {}: nuova combinazione registrata: {}", a_slot + 1, BindingLabel(b));
					gCapSlot = -1;
					gCapMax.clear();
					gCapAny = false;
				}
			}
		}

		void Loop()
		{
			using clock = std::chrono::steady_clock;
			auto  last = clock::now();
			Track tr[kSlots];
			bool  wasCapturing = false;

			while (gRun) {
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
				const auto  now = clock::now();
				const float dt = std::min(0.1f, std::chrono::duration<float>(now - last).count());
				last = now;

				const auto focused = GameFocused();
				const auto pressed = Pressed(focused);
				{
					std::lock_guard l(gPressedMx);
					gPressedNow = pressed;
				}

				if (const int cs = gCapSlot.load(); cs >= 0) {
					wasCapturing = true;
					CaptureStep(cs, pressed, dt);
					for (auto& t : tr) t = {};
					gHeldAny = false;
					continue;
				}
				if (wasCapturing) {
					// dopo la registrazione aspetto che tutto sia rilasciato
					if (!pressed.empty()) continue;
					wasCapturing = false;
				}

				Binding b[kSlots];
				{
					std::lock_guard l(gBindMx);
					for (int i = 0; i < kSlots; ++i) b[i] = S.bind[i];
				}

				bool down[kSlots]{};
				bool any = false;
				for (int i = 0; i < kSlots; ++i) {
					down[i] = ChordDown(b[i], pressed);
					for (auto k : b[i].keys) {
						if (Contains(pressed, k)) any = true;
					}
				}
				gHeldAny = any;

				for (int s = 0; s < kSlots; ++s) {
					if (b[s].empty() || !down[s]) {
						tr[s] = {};
						continue;
					}
					// una combinazione piu' grande che contiene questa e' premuta: questa non scatta
					bool suppressed = false;
					for (int t = 0; t < kSlots; ++t) {
						if (t == s || b[t].empty() || !down[t]) continue;
						if (b[t].keys.size() > b[s].keys.size() && IsSubset(b[s].keys, b[t].keys)) {
							suppressed = true;
							break;
						}
					}
					if (suppressed) {
						tr[s].blocked = true;
						tr[s].heldFor = 0.0f;
						continue;
					}
					if (tr[s].blocked) continue;  // resta bloccata fino al rilascio

					tr[s].heldFor += dt;
					if (!tr[s].fired && tr[s].heldFor >= b[s].hold) {
						tr[s].fired = true;
						gMask |= (1u << s);
					}
				}
			}
		}
	}

	void Start()
	{
		if (gRun.exchange(true)) return;
		Bind();
		std::thread(Loop).detach();  // mai "join": all'uscita del processo il thread viene chiuso dal sistema
	}

	void Stop()
	{
		gRun = false;
	}

	unsigned TakeSlots() { return gMask.exchange(0); }

	bool SlotKeysHeld() { return gHeldAny.load(); }

	bool IsHeld(InputId a_id)
	{
		std::lock_guard l(gPressedMx);
		return Contains(gPressedNow, a_id);
	}

	void BeginCapture(int a_slot)
	{
		if (a_slot < 0 || a_slot >= kSlots) return;
		std::lock_guard l(gCapMx);
		gCapMax.clear();
		gCapLive.clear();
		gCapAny = false;
		gCapWait = 0.0f;
		gCapSlot = a_slot;
	}

	void CancelCapture()
	{
		std::lock_guard l(gCapMx);
		gCapSlot = -1;
		gCapMax.clear();
		gCapAny = false;
	}

	int CaptureSlot() { return gCapSlot.load(); }

	std::string CaptureLive()
	{
		std::lock_guard l(gCapMx);
		std::string     s;
		for (auto k : gCapLive) {
			if (!s.empty()) s += " + ";
			s += InputName(k);
		}
		return s;
	}

	bool PadConnected() { return gPad; }
}
