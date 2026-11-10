/*
 * Odin as a libretro core
 *
 * The board (machine/boards/hikaru) runs a frame in each retro_run, the
 * renderer (host/renderer/hikaru) draws it with OpenGL in the context the
 * frontend gives the core, and its picture is copied into the frontend's
 * framebuffer. The sound of the frame goes to the frontend as it is, and the
 * controls of the cabinet come from the frontend's input, by the roles the
 * game list gives them (roles.hpp). None of the desktop program is in the
 * core: no window, no menu, no SDL.
 *
 * Content: a game's MAME set (airtrix.zip, a .7z or a folder). The shared
 * sets hikaru and mie, and the optional ones, are looked for beside it and in
 * system/odin. What the board keeps without power goes to saves/odin/<game>.
 *
 * Released under the GNU GPL, version 3 or later (see LICENSE).
 */

#include <GL/glew.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <functional>
#include <cstring>
#include <span>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "libretro.h"
/* The options, as the desktop's [display] has them (OPTIONS); their
 * translations in libretro_core_options_intl.h come from Crowdin */
#include "libretro_core_options.h"

/* Multi-channel sound (RetroArch's libretro.h, experimental; newer than the
 * copy here) */
#ifndef RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_MULTI
#define RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_MULTI (94 | RETRO_ENVIRONMENT_EXPERIMENTAL)
typedef size_t(RETRO_CALLCONV *retro_audio_sample_batch_multi_int16_t)(const int16_t *data, size_t frames,
                                                                        unsigned channels, unsigned layout);
typedef size_t(RETRO_CALLCONV *retro_audio_sample_batch_multi_float_t)(const float *data, size_t frames,
                                                                        unsigned channels, unsigned layout);
struct retro_audio_sample_multi_callback {
	retro_audio_sample_batch_multi_int16_t batch_int16;
	retro_audio_sample_batch_multi_float_t batch_float;
};
#define RETRO_AUDIO_SPEAKER_FRONT_LEFT 0x001
#define RETRO_AUDIO_SPEAKER_FRONT_RIGHT 0x002
#define RETRO_AUDIO_SPEAKER_BACK_LEFT 0x010
#define RETRO_AUDIO_SPEAKER_BACK_RIGHT 0x020
#define RETRO_AUDIO_LAYOUT_STEREO (RETRO_AUDIO_SPEAKER_FRONT_LEFT | RETRO_AUDIO_SPEAKER_FRONT_RIGHT)
#define RETRO_AUDIO_LAYOUT_QUAD (RETRO_AUDIO_LAYOUT_STEREO | RETRO_AUDIO_SPEAKER_BACK_LEFT | RETRO_AUDIO_SPEAKER_BACK_RIGHT)
#endif
#include <GL/osmesa.h>
#include "odin/host/gfx/vk_common.hpp"
#include "libretro_vulkan.h"

#include "odin/host/app/games.hpp"
#include "odin/host/app/rom_files.hpp"
#include "odin/host/app/user_files.hpp"
#include "odin/host/gfx/host_graphics.hpp"
#include "odin/host/input/roles.hpp"
#include "odin/host/renderer/hikaru/renderer.hpp"
#include "odin/machine/boards/hikaru/hikaru.hpp"
#include "odin/machine/boards/hikaru/hikaru_host.hpp"
#include "odin/machine/core/state_file.hpp"
#include "odin/machine/cabinet/drive_board.hpp"
#include "odin/host/audio/audio_out.hpp"
#include "odin/host/renderer/hikaru/renderer_programs.hpp"
#include "gaelco_run.hpp"

extern "C" const char odin_games_json[];
extern "C" const char odin_shaders_airtrix[], odin_shaders_braveff[], odin_shaders_pharrier[], odin_shaders_sgnascar[],
	odin_shaders_swracer[], odin_shaders_von4[], odin_shaders_atvtrack[], odin_shaders_gfootbal[],
	odin_shaders_smashdrv[];

namespace fs = std::filesystem;
using namespace odin;

namespace {

retro_environment_t environ_cb;
retro_video_refresh_t video_cb;
retro_audio_sample_batch_t audio_batch_cb;
retro_input_poll_t input_poll_cb;
retro_input_state_t input_state_cb;
retro_log_printf_t log_cb;
retro_hw_render_callback hw_render;

/* What the core draws with: OpenGL, or Vulkan on the frontend's device
 * (Renderer, a core option taken when the content starts) */
enum class Api { OPENGL, VULKAN };
Api api = Api::OPENGL;
/* Vulkan: Odin's card on the device the core made through the frontend
 * (negotiation interface, vk_create_device2), the frontend's interface, and
 * an image for each of the frontend's sync indices that the
 * picture of a frame is copied into for it */
std::unique_ptr<vk::Context> vk_card;
const retro_hw_render_interface_vulkan *vk_iface;
struct VkOutput {
	vk::Image image;
	/* What set_image is given: the frontend keeps the pointer, and reads it
	 * again after retro_run (a screenshot, a frame shown again) */
	retro_vulkan_image handed{};
};
/* A fixed array: the frontend keeps a pointer into it */
std::array<VkOutput, 32> vk_outputs;

void
log(retro_log_level level, const char *fmt, ...)
{
	char text[2048];
	va_list ap;

	va_start(ap, fmt);
	std::vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);
	if (log_cb)
		log_cb(level, "[Odin] %s", text);
	else
		std::fprintf(stderr, "[Odin] %s", text);
}

void
message(const std::string &text, unsigned frames = 300)
{
	retro_message msg{ text.c_str(), frames };

	if (environ_cb)
		environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
}

/* ─── The card the renderer draws with ─────────────────────────────────── */

/* The renderer asks its HostGraphics only for its drawing (make_drawing);
 * the rest of HostGraphics is the desktop's window, bezels and menu, which
 * the frontend has in place of them. */
class LibretroGraphics : public HostGraphics {
public:
	const std::string &name() const override { return m_name; }
	bool vulkan() const override { return ::api == Api::VULKAN; }
	const char *api() const override { return ::api == Api::VULKAN ? "Vulkan" : "OpenGL"; }
	void take_thread() override {}
	void leave_thread() override {}
	void clear_window(unsigned, unsigned) override {}
	void show_window() override {}
	bool set_waits(bool) override { return false; }
	void read(HostPicture, unsigned, unsigned, uint8_t *) override {}
	HostImage make_image(const SDL_Surface &, bool) override { return {}; }
	void forget_image(HostImage) override {}
	void draw_bezel(HostImage, unsigned, unsigned) override {}
	void draw_blurred_sides(HostPicture, PictureRect, unsigned, unsigned) override {}
	void draw_white_border(PictureRect, unsigned, unsigned, unsigned) override {}
	void blur_background(HostPicture) override {}
	void blur_window(unsigned, unsigned) override {}
	void draw_background(unsigned, unsigned, unsigned, unsigned, float) override {}
	void start_ui() override {}
	void draw_ui(ImDrawData *, unsigned, unsigned, const char *) override {}
	void stop_ui() override {}
	uint64_t ui_image(HostImage) const override { return 0; }
	void ui_blocks(ImDrawList *, bool) const override {}
	void ui_levels(ImDrawList *, bool) const override {}
	std::unique_ptr<Drawing> make_drawing() override
	{
		return ::api == Api::VULKAN ? Drawing::make_vk(*vk_card) : Drawing::make_gl();
	}

private:
	std::string m_name = "libretro";
};

/* ─── The screen of the cabinet ────────────────────────────────────────── */

/* What the board tells its screen (hikaru_host.hpp), as the desktop's
 * Display does it, but on the thread that runs the board: the frontend's
 * context is current there, and a frame is drawn right after it is run. */
class LibretroScreen : public HikaruScreen {
public:
	std::unique_ptr<Renderer> renderer;

	/* With the board on a thread of its own (Threaded Board), the frame
	 * before is drawn while the board runs the next one, as on the
	 * desktop: the board waits for that drawing before it changes what the
	 * drawing reads (Display::finish) */
	void set_drawing(bool on)
	{
		{
			std::lock_guard lock(m_mutex);
			m_drawing = on;
		}
		if (!on)
			m_done.notify_all();
	}
	void wait_drawing()
	{
		std::unique_lock lock(m_mutex);
		m_done.wait(lock, [this] { return !m_drawing; });
	}

	void list_launched() override
	{
		if (renderer)
			renderer->launch();
	}
	void before_texture_write(unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h) override
	{
		wait_drawing();
		if (renderer)
			renderer->invalidate_textures(bank, x, y, w, h);
	}
	void before_memory_changes() override { wait_drawing(); }
	void memory_changed() override
	{
		wait_drawing();
		if (renderer)
			renderer->invalidate_textures(-1);
	}
	bool save_pictures(StateFile &state) override
	{
		std::vector<uint8_t> pictures(Renderer::PICTURES * Renderer::PICTURE_SIZE);
		Renderer::Counters counters = {};

		if (renderer)
			renderer->save(pictures, counters);
		for (unsigned i = 0; i < Renderer::PICTURES; i++)
			if (!state.put(&pictures[i * Renderer::PICTURE_SIZE], static_cast<uint32_t>(Renderer::PICTURE_SIZE)))
				return false;
		return state.put(counters.num_lists) && state.put(counters.shown) && state.put(counters.next) &&
		       state.put(counters.launched);
	}
	bool load_pictures(StateFile &state) override
	{
		std::vector<uint8_t> pictures(Renderer::PICTURES * Renderer::PICTURE_SIZE);
		Renderer::Counters counters = {};

		for (unsigned i = 0; i < Renderer::PICTURES; i++)
			if (!state.get(&pictures[i * Renderer::PICTURE_SIZE], static_cast<uint32_t>(Renderer::PICTURE_SIZE)))
				return false;
		if (!state.get(counters.num_lists) || !state.get(counters.shown) || !state.get(counters.next) ||
		    !state.get(counters.launched))
			return false;
		if (renderer)
			renderer->load(pictures, counters);
		return true;
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_done;
	bool m_drawing = false;
};

/* ─── The board's own thread (Threaded Board) ──────────────────────────── */

class BoardThread {
public:
	~BoardThread() { stop(); }

	void start(Hikaru &board)
	{
		start([&board] { board.run_frame(); });
	}
	/* Any frame's work: a Gaelco board's, with its controls (GaelcoRun) */
	void start(std::function<void()> step)
	{
		m_step = std::move(step);
		m_quit = false;
		m_thread = std::thread([this] { loop(); });
	}
	void stop()
	{
		if (!m_thread.joinable())
			return;
		wait();
		{
			std::lock_guard lock(m_mutex);
			m_quit = true;
		}
		m_cv.notify_all();
		m_thread.join();
	}
	bool running() const { return m_thread.joinable(); }
	/* The next frame run on the thread */
	void run_frame()
	{
		{
			std::lock_guard lock(m_mutex);
			m_go = m_busy = true;
		}
		m_cv.notify_all();
	}
	/* Until the frame it runs is done */
	void wait()
	{
		std::unique_lock lock(m_mutex);
		m_cv.wait(lock, [this] { return !m_busy; });
	}

private:
	void loop()
	{
		std::unique_lock lock(m_mutex);
		for (;;) {
			m_cv.wait(lock, [this] { return m_go || m_quit; });
			if (m_quit)
				return;
			m_go = false;
			lock.unlock();
			m_step();
			lock.lock();
			m_busy = false;
			m_cv.notify_all();
		}
	}

	std::function<void()> m_step;
	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	bool m_go = false, m_busy = false, m_quit = false;
};

/* ─── The control panel ────────────────────────────────────────────────── */

/* The roles of the game list (roles.hpp) on the RetroPad, the analog sticks
 * and triggers and the light gun, as the desktop gives them to a gamepad:
 *
 *  START Start, SERVICE L3, COIN1/COIN2 Select of player 1/2, TEST R3 of
 *  player 1; UP/DOWN/LEFT/RIGHT the D-pad; BUTTON1 to BUTTON8 B, A, Y, X,
 *  L, R, L2, R2; STICK the left stick (and the D-pad), STICK2 the right one;
 *  GEAR_UP/GEAR_DOWN and a gear lever R and L; STEER the left stick across,
 *  ACCEL R2, BRAKE L2; GUN_X/GUN_Y the left stick, as the desktop gives a
 *  sight to a gamepad, or the light gun on a port whose device is Light Gun
 *  (GUN_OFFSCREEN its reload). A control the list gives inputs of the
 *  gamepad ("pad": "righttrigger"...) takes those in place of its role's.
 */
/* The device of each port (retro_set_controller_port_device): RetroPad or
 * Light Gun */
unsigned port_device[2] = { RETRO_DEVICE_JOYPAD, RETRO_DEVICE_JOYPAD };

bool
light_gun(unsigned port)
{
	return port < 2 && port_device[port] == RETRO_DEVICE_LIGHTGUN;
}

/* An input of the gamepad as SDL names it, as the list's "pad" does, on the
 * RetroPad (SDL's a is the bottom button, the RetroPad's B); -1 for an axis
 * or a name it has no button for */
int
retropad_of(const std::string &name)
{
	static const std::pair<const char *, int> NAMES[] = {
		{ "a", RETRO_DEVICE_ID_JOYPAD_B },
		{ "b", RETRO_DEVICE_ID_JOYPAD_A },
		{ "x", RETRO_DEVICE_ID_JOYPAD_Y },
		{ "y", RETRO_DEVICE_ID_JOYPAD_X },
		{ "leftshoulder", RETRO_DEVICE_ID_JOYPAD_L },
		{ "rightshoulder", RETRO_DEVICE_ID_JOYPAD_R },
		{ "lefttrigger", RETRO_DEVICE_ID_JOYPAD_L2 },
		{ "righttrigger", RETRO_DEVICE_ID_JOYPAD_R2 },
		{ "leftstick", RETRO_DEVICE_ID_JOYPAD_L3 },
		{ "rightstick", RETRO_DEVICE_ID_JOYPAD_R3 },
		{ "start", RETRO_DEVICE_ID_JOYPAD_START },
		{ "back", RETRO_DEVICE_ID_JOYPAD_SELECT },
		{ "dpup", RETRO_DEVICE_ID_JOYPAD_UP },
		{ "dpdown", RETRO_DEVICE_ID_JOYPAD_DOWN },
		{ "dpleft", RETRO_DEVICE_ID_JOYPAD_LEFT },
		{ "dpright", RETRO_DEVICE_ID_JOYPAD_RIGHT },
	};
	for (const auto &[n, id] : NAMES)
		if (name == n)
			return id;
	return -1;
}

class LibretroPanel : public HikaruPanel {
public:
	void set_game(const Game &game)
	{
		m_controls = game.controls.empty() ? standard_panel() : game.controls;
		m_controls.push_back(button("1P SERVICE", 0, 0x4000, Role::SERVICE));
		m_controls.push_back(button("2P SERVICE", 1, 0x4000, Role::SERVICE));
		m_grounded = game.grounded;
		m_positions.assign(m_controls.size(), 0);
		m_levers_were.assign(m_controls.size(), { false, false });
		m_coins_were[0] = m_coins_were[1] = false;
		m_coins[0] = m_coins[1] = false;
	}

	/* What RetroArch's Controls menu shows for each input: the name each
	 * control has in the game's INPUT TEST (the game list), on the input of
	 * its role */
	void describe() const
	{
		struct Input {
			unsigned port, device, index, id;
			std::string name;
		};
		std::vector<Input> inputs;
		const auto add = [&](unsigned port, unsigned device, unsigned index, unsigned id, const std::string &name) {
			for (const Input &in : inputs)
				if (in.port == port && in.device == device && in.index == index && in.id == id)
					return;
			inputs.push_back({ port, device, index, id, name });
		};
		const unsigned J = RETRO_DEVICE_JOYPAD, A = RETRO_DEVICE_ANALOG;
		const unsigned L = RETRO_DEVICE_INDEX_ANALOG_LEFT, R = RETRO_DEVICE_INDEX_ANALOG_RIGHT;

		for (const GameControl &control : m_controls) {
			const unsigned port = control.player & 1;
			const std::string &name = control.name;

			if (control.type == GameControl::Type::POSITIONS) {
				add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_L, name + " down");
				add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_R, name + " up");
				continue;
			}
			if (control.type == GameControl::Type::BUTTON && has_pad(control)) {
				for (const std::string &input : control.pad)
					if (retropad_of(input) >= 0)
						add(port, J, 0, static_cast<unsigned>(retropad_of(input)), name);
				continue;
			}
			switch (control.role) {
			case Role::START: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_START, name); break;
			case Role::SERVICE: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_L3, name); break;
			case Role::UP: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_UP, name); break;
			case Role::DOWN: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, name); break;
			case Role::LEFT: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, name); break;
			case Role::RIGHT: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, name); break;
			case Role::BUTTON1: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_B, name); break;
			case Role::BUTTON2: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_A, name); break;
			case Role::BUTTON3: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_Y, name); break;
			case Role::BUTTON4: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_X, name); break;
			case Role::BUTTON5: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_L, name); break;
			case Role::BUTTON6: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_R, name); break;
			case Role::BUTTON7: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_L2, name); break;
			case Role::BUTTON8: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_R2, name); break;
			case Role::GEAR_UP: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_R, name); break;
			case Role::GEAR_DOWN: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_L, name); break;
			case Role::ACCEL: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_R2, name); break;
			case Role::BRAKE: add(port, J, 0, RETRO_DEVICE_ID_JOYPAD_L2, name); break;
			case Role::STICK_UP: case Role::STICK_DOWN: add(port, A, L, RETRO_DEVICE_ID_ANALOG_Y, name); break;
			case Role::STICK_LEFT: case Role::STICK_RIGHT: add(port, A, L, RETRO_DEVICE_ID_ANALOG_X, name); break;
			case Role::STICK2_UP: case Role::STICK2_DOWN: add(port, A, R, RETRO_DEVICE_ID_ANALOG_Y, name); break;
			case Role::STICK2_LEFT: case Role::STICK2_RIGHT: add(port, A, R, RETRO_DEVICE_ID_ANALOG_X, name); break;
			case Role::STEER: case Role::STICK_X: add(port, A, L, RETRO_DEVICE_ID_ANALOG_X, name); break;
			case Role::STICK_Y: add(port, A, L, RETRO_DEVICE_ID_ANALOG_Y, name); break;
			case Role::STICK2_X: add(port, A, R, RETRO_DEVICE_ID_ANALOG_X, name); break;
			case Role::STICK2_Y: add(port, A, R, RETRO_DEVICE_ID_ANALOG_Y, name); break;
			case Role::GUN_X:
				if (!light_gun(port))
					add(port, A, L, RETRO_DEVICE_ID_ANALOG_X, name);
				break;
			case Role::GUN_Y:
				if (!light_gun(port))
					add(port, A, L, RETRO_DEVICE_ID_ANALOG_Y, name);
				break;
			default: break;
			}
		}
		add(0, J, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Coin 1");
		add(1, J, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Coin 2");
		add(0, J, 0, RETRO_DEVICE_ID_JOYPAD_R3, "Test");

		static std::vector<std::string> names;
		static std::vector<retro_input_descriptor> list;
		names.clear();
		list.clear();
		names.reserve(inputs.size());
		for (const Input &in : inputs)
			names.push_back(in.name);
		for (size_t i = 0; i < inputs.size(); i++)
			list.push_back({ inputs[i].port, inputs[i].device, inputs[i].index, inputs[i].id, names[i].c_str() });
		list.push_back({ 0, 0, 0, 0, nullptr });
		environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, list.data());
	}

	/* The channel the steering wheel is read on, as last read; 8000 with
	 * none */
	uint16_t steer_channel() const
	{
		for (const GameControl &control : m_controls)
			if (control.role == Role::STEER && control.type == GameControl::Type::ANALOG)
				return m_inputs.jvs_analog[control.analog & 7];
		return 0x8000;
	}

	/* Read once a retro_run, after input_poll */
	void poll()
	{
		CabinetInputs inputs = {};

		for (uint16_t &channel : inputs.jvs_analog)
			channel = 0x8000;
		inputs.mainboard_test = 0;
		inputs.mainboard_service = 0;
		inputs.jvs_system = pad(0, RETRO_DEVICE_ID_JOYPAD_R3) ? 0x80 : 0;

		for (size_t c = 0; c < m_controls.size(); c++) {
			const GameControl &control = m_controls[c];
			const unsigned player = control.player & 1;

			switch (control.type) {
			case GameControl::Type::BUTTON:
				if (has_pad(control) ? pad_pressed(control, player) : switch_on(control.role, player))
					inputs.jvs_player[control.button.player & 1] |= control.button.mask;
				break;
			case GameControl::Type::POSITIONS: {
				const bool down = pad(player, RETRO_DEVICE_ID_JOYPAD_L);
				const bool up = pad(player, RETRO_DEVICE_ID_JOYPAD_R);

				if (down && !m_levers_were[c][0] && m_positions[c] > 0)
					m_positions[c]--;
				if (up && !m_levers_were[c][1] && m_positions[c] + 1 < control.positions.size())
					m_positions[c]++;
				m_levers_were[c] = { down, up };
				for (const JvsSwitch &sw : control.positions[m_positions[c]].switches)
					inputs.jvs_player[sw.player & 1] |= sw.mask;
				break;
			}
			case GameControl::Type::ANALOG:
				inputs.jvs_analog[control.analog & 7] = analog(control, player);
				break;
			}
		}
		for (const JvsSwitch &sw : m_grounded)
			inputs.jvs_player[sw.player & 1] |= sw.mask;
		for (unsigned slot = 0; slot < 2; slot++) {
			const bool now = pad(slot, RETRO_DEVICE_ID_JOYPAD_SELECT);

			m_coins[slot] = m_coins[slot] || (now && !m_coins_were[slot]);
			m_coins_were[slot] = now;
		}
		m_inputs = inputs;
	}

	void read_inputs(CabinetInputs &inputs) override
	{
		inputs = m_inputs;
		for (unsigned slot = 0; slot < 2; slot++) {
			inputs.jvs_coin[slot] = m_coins[slot];
			m_coins[slot] = false;
		}
	}

private:
	static GameControl button(const char *name, uint8_t player, uint16_t mask, Role role)
	{
		GameControl control{};

		control.name = name;
		control.type = GameControl::Type::BUTTON;
		control.button = JvsSwitch{ player, mask };
		control.role = role;
		control.player = player;
		return control;
	}

	/* START, the directions and PUSH1 to PUSH6 of both players, for a game
	 * whose list names no controls (control_panel.cpp) */
	static std::vector<GameControl> standard_panel()
	{
		static const uint16_t MASKS[11] = { 0x8000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200,
		                                    0x0100, 0x0080, 0x0040, 0x0020, 0x0010 };
		static const Role ROLES[11] = { Role::START,   Role::UP,      Role::DOWN,    Role::LEFT,
		                                Role::RIGHT,   Role::BUTTON1, Role::BUTTON2, Role::BUTTON3,
		                                Role::BUTTON4, Role::BUTTON5, Role::BUTTON6 };
		std::vector<GameControl> panel;

		for (uint8_t player = 0; player < 2; player++)
			for (unsigned i = 0; i < 11; i++)
				panel.push_back(button("", player, MASKS[i], ROLES[i]));
		return panel;
	}

	/* The list gives the control inputs of the gamepad that the RetroPad has */
	static bool has_pad(const GameControl &control)
	{
		for (const std::string &input : control.pad)
			if (retropad_of(input) >= 0)
				return true;
		return false;
	}
	static bool pad_pressed(const GameControl &control, unsigned player)
	{
		for (const std::string &input : control.pad)
			if (retropad_of(input) >= 0 ? pad(player, static_cast<unsigned>(retropad_of(input))) :
			                              half_axis(input, player))
				return true;
		return false;
	}
	/* Half of a stick as the list names it, "-lefty" (up) or "+rightx":
	 * pushed past half its way */
	static bool half_axis(const std::string &input, unsigned player)
	{
		if (input.size() < 6 || (input[0] != '-' && input[0] != '+'))
			return false;
		const std::string axis_name = input.substr(1);
		const bool left = axis_name.rfind("left", 0) == 0, right = axis_name.rfind("right", 0) == 0;

		if (!left && !right)
			return false;
		const char last = axis_name.back();
		if (last != 'x' && last != 'y')
			return false;
		return stick(player, left ? RETRO_DEVICE_INDEX_ANALOG_LEFT : RETRO_DEVICE_INDEX_ANALOG_RIGHT, last == 'y',
		             input[0] == '+');
	}
	static bool pad(unsigned player, unsigned id)
	{
		return input_state_cb && input_state_cb(player, RETRO_DEVICE_JOYPAD, 0, id) != 0;
	}
	static int axis(unsigned player, unsigned index, unsigned id)
	{
		return input_state_cb ? input_state_cb(player, RETRO_DEVICE_ANALOG, index, id) : 0;
	}
	/* A trigger, 0 to 0x7FFF: its analog value, else all or nothing */
	static int trigger(unsigned player, unsigned id)
	{
		const int value = axis(player, RETRO_DEVICE_INDEX_ANALOG_BUTTON, id);

		return value ? value : (pad(player, id) ? 0x7FFF : 0);
	}
	static bool stick(unsigned player, unsigned index, bool y, bool positive)
	{
		const int value =
			axis(player, index, y ? RETRO_DEVICE_ID_ANALOG_Y : RETRO_DEVICE_ID_ANALOG_X);

		return positive ? value > 0x4000 : value < -0x4000;
	}

	static bool switch_on(Role role, unsigned player)
	{
		const unsigned L = RETRO_DEVICE_INDEX_ANALOG_LEFT, R = RETRO_DEVICE_INDEX_ANALOG_RIGHT;

		switch (role) {
		case Role::START: return pad(player, RETRO_DEVICE_ID_JOYPAD_START);
		case Role::SERVICE: return pad(player, RETRO_DEVICE_ID_JOYPAD_L3);
		case Role::UP: return pad(player, RETRO_DEVICE_ID_JOYPAD_UP);
		case Role::DOWN: return pad(player, RETRO_DEVICE_ID_JOYPAD_DOWN);
		case Role::LEFT: return pad(player, RETRO_DEVICE_ID_JOYPAD_LEFT);
		case Role::RIGHT: return pad(player, RETRO_DEVICE_ID_JOYPAD_RIGHT);
		case Role::BUTTON1: return pad(player, RETRO_DEVICE_ID_JOYPAD_B);
		case Role::BUTTON2: return pad(player, RETRO_DEVICE_ID_JOYPAD_A);
		case Role::BUTTON3: return pad(player, RETRO_DEVICE_ID_JOYPAD_Y);
		case Role::BUTTON4: return pad(player, RETRO_DEVICE_ID_JOYPAD_X);
		case Role::BUTTON5: return pad(player, RETRO_DEVICE_ID_JOYPAD_L);
		case Role::BUTTON6: return pad(player, RETRO_DEVICE_ID_JOYPAD_R);
		case Role::BUTTON7: return pad(player, RETRO_DEVICE_ID_JOYPAD_L2);
		case Role::BUTTON8: return pad(player, RETRO_DEVICE_ID_JOYPAD_R2);
		case Role::STICK_UP: return pad(player, RETRO_DEVICE_ID_JOYPAD_UP) || stick(player, L, true, false);
		case Role::STICK_DOWN: return pad(player, RETRO_DEVICE_ID_JOYPAD_DOWN) || stick(player, L, true, true);
		case Role::STICK_LEFT: return pad(player, RETRO_DEVICE_ID_JOYPAD_LEFT) || stick(player, L, false, false);
		case Role::STICK_RIGHT: return pad(player, RETRO_DEVICE_ID_JOYPAD_RIGHT) || stick(player, L, false, true);
		case Role::STICK2_UP: return stick(player, R, true, false);
		case Role::STICK2_DOWN: return stick(player, R, true, true);
		case Role::STICK2_LEFT: return stick(player, R, false, false);
		case Role::STICK2_RIGHT: return stick(player, R, false, true);
		case Role::GEAR_UP: return pad(player, RETRO_DEVICE_ID_JOYPAD_R);
		case Role::GEAR_DOWN: return pad(player, RETRO_DEVICE_ID_JOYPAD_L);
		case Role::GUN_OFFSCREEN:
			return light_gun(player) && input_state_cb &&
			       (input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_RELOAD) ||
			        input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN));
		default: return false;
		}
	}

	/* A channel as control_panel.cpp makes it from a gamepad: 8000 the
	 * middle of a stick, a trigger from its rest to its end; turned over for
	 * a control whose low values are right, down or pressed */
	static uint16_t channel(GameControl::Low low, int value, bool trigger_axis)
	{
		const bool over = GameControl::turned_over(low);

		if (trigger_axis) {
			const int word = std::max(value, 0) * 0xFFFF / 0x7FFF;

			return static_cast<uint16_t>(over ? 0xFFFF - word : word);
		}
		return static_cast<uint16_t>(std::clamp(over ? 0x8000 - value : 0x8000 + value, 0, 0xFFFF));
	}

	/* The left stick across (or down), else the D-pad all the way, as the
	 * desktop gives a wheel or a handlebar keys: a keyboard has no stick */
	static int stick_or_dpad(unsigned player, bool y)
	{
		const int value = axis(player, RETRO_DEVICE_INDEX_ANALOG_LEFT, y ? RETRO_DEVICE_ID_ANALOG_Y : RETRO_DEVICE_ID_ANALOG_X);

		if (value)
			return value;
		const bool less = pad(player, y ? RETRO_DEVICE_ID_JOYPAD_UP : RETRO_DEVICE_ID_JOYPAD_LEFT);
		const bool more = pad(player, y ? RETRO_DEVICE_ID_JOYPAD_DOWN : RETRO_DEVICE_ID_JOYPAD_RIGHT);

		return less == more ? 0 : (less ? -0x7FFF : 0x7FFF);
	}

	static uint16_t analog(const GameControl &control, unsigned player)
	{
		const unsigned L = RETRO_DEVICE_INDEX_ANALOG_LEFT, R = RETRO_DEVICE_INDEX_ANALOG_RIGHT;

		switch (control.role) {
		case Role::STEER:
		case Role::STICK_X: return channel(control.low, stick_or_dpad(player, false), false);
		case Role::STICK_Y: return channel(control.low, stick_or_dpad(player, true), false);
		case Role::STICK2_X: return channel(control.low, axis(player, R, RETRO_DEVICE_ID_ANALOG_X), false);
		case Role::STICK2_Y: return channel(control.low, axis(player, R, RETRO_DEVICE_ID_ANALOG_Y), false);
		case Role::ACCEL: return channel(control.low, trigger(player, RETRO_DEVICE_ID_JOYPAD_R2), true);
		case Role::BRAKE: return channel(control.low, trigger(player, RETRO_DEVICE_ID_JOYPAD_L2), true);
		case Role::GUN_X:
		case Role::GUN_Y: {
			/* A RetroPad: the left stick, the sight as far across the
			 * picture as the stick is pushed, as the desktop's "leftx" */
			if (!light_gun(player))
				return channel(control.low,
				               axis(player, L, control.role == Role::GUN_X ? RETRO_DEVICE_ID_ANALOG_X :
				                                                            RETRO_DEVICE_ID_ANALOG_Y),
				               false);
			/* The light gun's place on the picture, -0x7FFF to 0x7FFF on
			 * each side, as the desktop's mouse at 0 to 1 */
			const int at = input_state_cb ? input_state_cb(player, RETRO_DEVICE_LIGHTGUN, 0,
			                                               control.role == Role::GUN_X ?
			                                                       RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X :
			                                                       RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y) :
			                                0;
			const double where = (at + 0x7FFF) / 65534.0;
			const long value = std::lround(std::clamp(where, 0.0, 1.0) * 65535.0) - 32768;

			return channel(control.low, static_cast<int>(value), false);
		}
		default:
			return control.low == GameControl::Low::RELEASED ? 0x0000 :
			       control.low == GameControl::Low::PRESSED  ? 0xFFFF :
			                                                   0x8000;
		}
	}

	std::vector<GameControl> m_controls;
	std::vector<JvsSwitch> m_grounded;
	std::vector<size_t> m_positions;
	std::vector<std::array<bool, 2>> m_levers_were;
	CabinetInputs m_inputs = {};
	bool m_coins[2] = {}, m_coins_were[2] = {};
};

/* ─── The core ─────────────────────────────────────────────────────────── */

struct Core {
	GameList games;
	std::optional<Game> game;
	std::unique_ptr<Hikaru> board;
	/* A Gaelco board in place of the Hikaru (gaelco_run.hpp), and what
	 * draws its Neon 250's scenes, as the desktop's Display::attach_scenes */
	std::unique_ptr<GaelcoRun> gaelco;
	std::unique_ptr<Drawing> own;
	unsigned own_scale = 1;
	PmxScene scene;
	bool scene_fresh = false, blanked = false;
	/* The controls and look the board's next frame runs with (taken on the
	 * frontend's thread, run on the board's) */
	CabinetInputs gaelco_in{};
	unsigned gaelco_fine = 1, gaelco_flat = 0;
	bool gaelco_sorts = false;
	/* Its sound: the woofer's lows (AudioOut::woofer_pairs) */
	LowPass woofer_low;
	std::vector<int16_t> woofer_pairs;
	double woofer_hz = 0;
	unsigned start_volume = 3;	/* of START_VOLUMES: 75 */
	LibretroScreen screen;
	LibretroPanel panel;
	LibretroGraphics graphics;
	std::string system_dir, save_dir;
	bool context_ready = false;
	std::vector<int16_t> mixed;
	/* The frame's sound mixed into mixed, not yet handed to the frontend
	 * (hand_frame_sound), with its channel count */
	bool sound_mixed = false;
	unsigned mixed_channels = 2;
	/* Sound Mode and Optional Board Level of the game, for the three with an
	 * optional sound board (sound_game) */
	std::string sound_mode = "main";
	double option_level = 0;
	/* Audio Thread: the frames' sound buffered for the frontend's audio
	 * thread (audio_thread_callback) */
	bool audio_thread = false;
	/* Rumble (core option): the frontend's interface, and what each pad
	 * was last told; the desktop's shakes (Controls::show) */
	retro_set_rumble_state_t set_rumble = nullptr;
	bool rumble = true;
	/* Rumble "Large Motor Only": the weak motor left still */
	bool rumble_weak = true;
	uint16_t felt_level = 0, drive_level = 0, wheel_was = 0x8000;
	int force_was = 0;
	std::array<double, 8> wheel_moves{};
	unsigned wheel_move = 0;
	uint16_t rumbling[2] = {};
	BoardThread board_thread;
	/* Threaded Board: a frame of the board is done and not drawn yet */
	bool threaded = true, frame_pending = false;
	/* Core options (read_options) */
	unsigned scale = 1, anisotropy = 1;
	DisplayUpscaleMode upscale_mode = DisplayUpscaleMode::SNAP;
	unsigned shown_w = 0, shown_h = 0;
	/* The speed, in the log every SPEED_FRAMES frames */
	std::chrono::steady_clock::time_point speed_since;
	unsigned speed_frames = 0;
};

constexpr unsigned MOST_SCALE = 6;
constexpr unsigned SPEED_FRAMES = 600;


/* The games with an optional sound board, whose Sound Mode and Optional
 * Board Level the options have; NASCAR Arcade (original) takes those of
 * NASCAR Arcade */
const char *const SOUND_GAMES[] = { "pharrier", "swracer", "sgnascar", "atvtrack", "gfootbal", "smashdrv" };

/* Smashing Drive's volumes at power-on, as the desktop's (options.hpp) */
const char *const START_VOLUMES[] = { "0", "25", "50", "75", "96" };

/* The Gaelco boards' games: a woofer in place of an optional board */
bool
gaelco_game(const char *g)
{
	return g && (!std::strcmp(g, "atvtrack") || !std::strcmp(g, "gfootbal") || !std::strcmp(g, "smashdrv"));
}

const char *
sound_game(const std::string &name)
{
	for (const char *g : SOUND_GAMES)
		if (name.rfind(g, 0) == 0)
			return g;
	return nullptr;
}

/* The options of the game's sound shown, those of the others hidden (none
 * shown without a game) */
void
show_sound_options(const char *shown)
{
	for (const char *g : SOUND_GAMES)
		for (const char *what : { "_sound_mode", "_option_level", "_woofer_cutoff", "_start_volume" }) {
			if (!std::strcmp(what, "_woofer_cutoff") && !gaelco_game(g))
				continue;
			if (!std::strcmp(what, "_start_volume") && std::strcmp(g, "smashdrv"))
				continue;
			const std::string key = std::string("odin_") + g + what;
			retro_core_option_display display{ key.c_str(), shown && !std::strcmp(shown, g) };

			if (environ_cb)
				environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &display);
		}
}

const char *
option_value(const char *key)
{
	retro_variable var{ key, nullptr };

	return environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value ? var.value : nullptr;
}

void
read_options(Core &c)
{
	if (const char *v = option_value("odin_scale"))
		c.scale = std::clamp<unsigned>(static_cast<unsigned>(std::atoi(v)), 1, MOST_SCALE);
	if (const char *v = option_value("odin_anisotropy"))
		c.anisotropy = std::clamp<unsigned>(static_cast<unsigned>(std::atoi(v)), 1, 16);
	c.sound_mode = "main";
	c.option_level = 0;
	if (const char *g = c.game ? sound_game(c.game->name) : nullptr) {
		const std::string prefix = std::string("odin_") + g;

		if (const char *v = option_value((prefix + "_sound_mode").c_str()))
			c.sound_mode = v;
		if (const char *v = option_value((prefix + "_option_level").c_str()))
			c.option_level = std::atof(v);
		if (gaelco_game(g)) {
			const char *v = option_value((prefix + "_woofer_cutoff").c_str());
			const double hz = v && std::strcmp(v, "off") ? std::atof(v) : (v ? 0.0 : 120.0);

			if (hz != c.woofer_hz) {
				c.woofer_hz = hz;
				c.woofer_low = LowPass(hz, static_cast<int>(AtvTrack::DAC_RATE));
			}
		}
		if (!std::strcmp(g, "smashdrv"))
			if (const char *v = option_value("odin_smashdrv_start_volume"))
				for (unsigned i = 0; i < std::size(START_VOLUMES); i++)
					if (!std::strcmp(v, START_VOLUMES[i]))
						c.start_volume = i;
	}
	if (const char *v = option_value("odin_rumble")) {
		c.rumble = std::strcmp(v, "disabled") != 0;
		c.rumble_weak = std::strcmp(v, "large") != 0;
		/* Sent again with the motors as now chosen */
		c.rumbling[0] = c.rumbling[1] = 0xFFFF;
	}
	if (const char *v = option_value("odin_upscale_mode"))
		c.upscale_mode = !std::strcmp(v, "native") ? DisplayUpscaleMode::NATIVE :
		                 !std::strcmp(v, "raw")    ? DisplayUpscaleMode::RAW :
		                                             DisplayUpscaleMode::SNAP;
}

std::unique_ptr<Core> core;
/* The size of a save state the frontend is told (retro_serialize_size) */
size_t state_size;

/* The game list, written to system/odin/odin-games.json the first time and
 * whenever this build's differs, then read from there: the desktop's code
 * reads it from a file. */
bool
load_game_list(Core &c)
{
	const fs::path dir = fs::path(c.system_dir) / "odin";
	const fs::path path = dir / "odin-games.json";
	const std::string text(odin_games_json);
	std::error_code ec;

	fs::create_directories(dir, ec);
	{
		std::ifstream in(path, std::ios::binary);
		const std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

		if (old != text) {
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			out << text;
		}
	}
	return c.games.load(path.string());
}

/* A file written with that text, unless it holds it already */
void
write_if_changed(const fs::path &path, const std::string &text)
{
	std::error_code ec;

	fs::create_directories(path.parent_path(), ec);
	std::ifstream in(path, std::ios::binary);
	const std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();
	if (old != text) {
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out << text;
	}
}

/* The mesh programs of the game, as the desktop's find_program_lists lays
 * them out: the list Odin comes with (system/odin/shaders/<game>.txt), the
 * one kept for the player, which the renderer adds to as the game draws
 * (system/odin/cache/shaders/<game>.txt), and the folder of the programs
 * the driver made (system/odin/cache/programs) */
void
program_lists(const std::string &game, Renderer::Options &looks)
{
	static const struct {
		const char *game;
		const char *text;
	} seeds[] = { { "airtrix", odin_shaders_airtrix },   { "braveff", odin_shaders_braveff },
		      { "pharrier", odin_shaders_pharrier }, { "sgnascar", odin_shaders_sgnascar },
		      { "swracer", odin_shaders_swracer },   { "von4", odin_shaders_von4 },
		      { "atvtrack", odin_shaders_atvtrack }, { "gfootbal", odin_shaders_gfootbal },
		      { "smashdrv", odin_shaders_smashdrv } };
	const fs::path odin = fs::path(core->system_dir) / "odin";
	std::error_code ec;
	/* A version of a game (airtrixo) draws as its parent does */
	const std::string parent = core->game->parent.empty() ? game : core->game->parent;

	for (const auto &seed : seeds)
		if (parent == seed.game || game == seed.game) {
			const fs::path path = odin / "shaders" / (std::string(seed.game) + ".txt");
			write_if_changed(path, seed.text);
			looks.programs_seed = path.string();
		}
	fs::create_directories(odin / "cache" / "shaders", ec);
	fs::create_directories(odin / "cache" / "programs", ec);
	looks.programs_kept = (odin / "cache" / "shaders" / (game + ".txt")).string();
	looks.programs_made = (odin / "cache" / "programs").string();
}

/* The Neon 250's drawing of a Gaelco game, as the desktop's
 * Display::attach_scenes makes it: the programs of the lists of the game
 * (the one Odin comes with and the one kept for the player) made before its
 * first scene, those it makes from then on added to the kept list */
void
create_drawing()
{
	if (!core || !core->gaelco || core->own)
		return;

	Renderer::Options looks{};
	std::vector<uint64_t> keys;

	program_lists(core->game->name, looks);
	for (const std::string *list : { &looks.programs_seed, &looks.programs_kept })
		if (!list->empty() && fs::exists(*list))
			for (const uint64_t key : read_pmx_list(*list))
				if (std::find(keys.begin(), keys.end(), key) == keys.end())
					keys.push_back(key);
	core->own = core->graphics.make_drawing();
	core->own->keep_programs_in(looks.programs_made);
	core->own_scale = 1;
	unsigned prepared = 0;
	core->own->prepare_pmx(keys, [&](unsigned done, unsigned total) {
		if (done == 0)
			log(RETRO_LOG_INFO, "programs: making %u ready\n", total);
		prepared = total;
	});
	if (prepared)
		log(RETRO_LOG_INFO, "programs: %u ready\n", prepared);
	core->own->list_pmx_in(looks.programs_kept);
	core->scene_fresh = false;
}

void
create_renderer()
{
	if (core && core->gaelco) {
		create_drawing();
		return;
	}
	if (!core || !core->board || core->screen.renderer)
		return;

	Renderer::Memory memory;
	/* As the desktop's Display::attach sets them with none of its
	 * environment variables: no logs, fingerprints or debug keys */
	Renderer::Options looks{};

	looks.log = false;
	looks.log_frame = 0;
	looks.list_fp = false;
	looks.list_dump = -1;
	looks.lists_test = false;
	looks.times = false;
	looks.debug_keys = false;
	looks.key_down = nullptr;
	looks.ctx = nullptr;

	memory.fb = core->board->framebuffer_ram();
	memory.fb_writes = &core->board->framebuffer_writes();
	memory.texram[0] = core->board->texture_ram(0);
	memory.texram[1] = core->board->texture_ram(1);
	program_lists(core->game->name, looks);
	core->screen.renderer = std::make_unique<Renderer>(core->graphics, memory, looks);
	core->screen.renderer->set_gpu(&core->board->gpu());
	/* The programs of the lists made ready before the first frame, as the
	 * desktop does behind its loading screen, so that the game does not
	 * stutter when it first draws with one */
	unsigned prepared = 0;
	core->screen.renderer->prepare_programs([&](unsigned done, unsigned total) {
		if (done == 0)
			log(RETRO_LOG_INFO, "programs: making %u ready\n", total);
		prepared = total;
	});
	if (prepared)
		log(RETRO_LOG_INFO, "programs: %u ready\n", prepared);
}

void vk_destroy_outputs();
void vk_destroy_device();

void
context_reset_vk()
{
	if (!environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, &vk_iface) || !vk_iface ||
	    vk_iface->interface_type != RETRO_HW_RENDER_INTERFACE_VULKAN) {
		log(RETRO_LOG_ERROR, "no Vulkan interface from the frontend\n");
		vk_iface = nullptr;
		return;
	}
	if (!vk_card) {
		log(RETRO_LOG_ERROR, "the frontend did not let the core make its Vulkan device (negotiation interface 2)\n");
		return;
	}
	vk_card->set_queue_lock([] { vk_iface->lock_queue(vk_iface->handle); },
	                        [] { vk_iface->unlock_queue(vk_iface->handle); });
	log(RETRO_LOG_INFO, "Vulkan on %s\n", vk_card->name().c_str());
	if (!core)
		return;
	core->context_ready = true;
	create_renderer();
}

void
context_reset()
{
	if (api == Api::VULKAN) {
		context_reset_vk();
		return;
	}
	glewExperimental = GL_TRUE;
	const GLenum err = glewInit();

	if (err != GLEW_OK && !GLEW_VERSION_3_2) {
		log(RETRO_LOG_ERROR, "GLEW: %s\n", reinterpret_cast<const char *>(glewGetErrorString(err)));
		return;
	}
	log(RETRO_LOG_INFO, "OpenGL %s, %s\n", reinterpret_cast<const char *>(glGetString(GL_VERSION)),
	    reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
	if (!core)
		return;
	core->context_ready = true;
	create_renderer();
}

void
context_destroy()
{
	log(RETRO_LOG_INFO, "context destroyed\n");
	/* The frontend destroys its device right after (it did not call
	 * destroy_device): Odin's card goes now, with everything it made on
	 * that device; the next context makes a new one (vk_create_device) */
	if (!core) {
		if (api == Api::VULKAN)
			vk_destroy_device();
		return;
	}
	/* The board's thread may be running a frame, which tells the screen
	 * (and so the renderer) what it writes: done first, and the frame it
	 * ran is not drawn on the renderer of the next context */
	if (core->board_thread.running())
		core->board_thread.wait();
	core->frame_pending = false;
	core->sound_mixed = false;
	core->screen.renderer.reset();
	core->own.reset();
	core->context_ready = false;
	if (api == Api::VULKAN)
		vk_destroy_device();
}

/* ─── Vulkan ───────────────────────────────────────────────────────────── */

/* The images the pictures are copied into, gone with the context */
void
vk_destroy_outputs()
{
	if (!vk_card)
		return;
	vk_card->send_and_wait();
	/* The frontend's frames still read these images on the card: they
	 * go once the whole device is idle */
	vk_card->fn().vkDeviceWaitIdle(vk_card->device());
	for (VkOutput &out : vk_outputs) {
		if (out.image.image)
			vk_card->destroy_image(out.image);
		out = {};
	}
}

/* Negotiation interface 2: the instance at Vulkan 1.3 and a device with
 * what Odin's renderer needs, both made through the frontend's wrappers,
 * which add what the frontend needs */
const VkApplicationInfo *
vk_application_info()
{
	static const VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Odin", 1, "Odin", 1,
		                            VK_API_VERSION_1_3 };
	return &app;
}

VkInstance
vk_create_instance(PFN_vkGetInstanceProcAddr, const VkApplicationInfo *app,
                   retro_vulkan_create_instance_wrapper_t wrapper, void *opaque)
{
	VkInstanceCreateInfo info{};

	info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	info.pApplicationInfo = app;
	return wrapper(opaque, &info);
}

bool
vk_create_device2(retro_vulkan_context *context, VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
                  PFN_vkGetInstanceProcAddr gipa, retro_vulkan_create_device_wrapper_t wrapper, void *opaque)
{
	std::string why;

	vk_card = vk::Context::make_libretro(
		gipa, instance, gpu, surface,
		[&](VkPhysicalDevice chosen, const VkDeviceCreateInfo &info) { return wrapper(chosen, opaque, &info); }, why);
	if (!vk_card) {
		log(RETRO_LOG_ERROR, "Vulkan: %s\n", why.c_str());
		return false;
	}
	context->gpu = vk_card->gpu();
	context->device = vk_card->device();
	context->queue = vk_card->queue();
	context->queue_family_index = vk_card->queue_family();
	context->presentation_queue = context->queue;
	context->presentation_queue_family_index = context->queue_family_index;
	return true;
}

/* Negotiation interface 1, which RetroArch uses where it does not take
 * create_device2: the core makes the device itself, with the extensions,
 * layers and features the frontend names added to Odin's */
bool
vk_create_device(retro_vulkan_context *context, VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface,
                 PFN_vkGetInstanceProcAddr gipa, const char **required_extensions, unsigned num_extensions,
                 const char **required_layers, unsigned num_layers, const VkPhysicalDeviceFeatures *required_features)
{
	const auto create = [&](VkPhysicalDevice chosen, const VkDeviceCreateInfo &info) -> VkDevice {
		std::vector<const char *> extensions(info.ppEnabledExtensionNames,
		                                     info.ppEnabledExtensionNames + info.enabledExtensionCount);
		for (unsigned i = 0; i < num_extensions; i++)
			if (std::none_of(extensions.begin(), extensions.end(),
			                 [&](const char *e) { return !std::strcmp(e, required_extensions[i]); }))
				extensions.push_back(required_extensions[i]);
		/* Odin's features are in a VkPhysicalDeviceFeatures2 at the head of
		 * the chain (vk_common.cpp, make_device): the frontend's join them */
		if (required_features && info.pNext) {
			auto *features2 = static_cast<VkPhysicalDeviceFeatures2 *>(const_cast<void *>(info.pNext));
			if (features2->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2) {
				auto *mine = reinterpret_cast<VkBool32 *>(&features2->features);
				const auto *theirs = reinterpret_cast<const VkBool32 *>(required_features);
				for (size_t i = 0; i < sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32); i++)
					mine[i] = mine[i] || theirs[i];
			}
		}
		VkDeviceCreateInfo copy = info;
		copy.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		copy.ppEnabledExtensionNames = extensions.data();
		copy.enabledLayerCount = num_layers;
		copy.ppEnabledLayerNames = required_layers;
		const auto create_device = reinterpret_cast<PFN_vkCreateDevice>(gipa(instance, "vkCreateDevice"));
		VkDevice device = VK_NULL_HANDLE;
		if (!create_device || create_device(chosen, &copy, nullptr, &device) != VK_SUCCESS)
			return VK_NULL_HANDLE;
		return device;
	};
	std::string why;

	vk_card = vk::Context::make_libretro(gipa, instance, gpu, surface, create, why);
	if (!vk_card) {
		log(RETRO_LOG_ERROR, "Vulkan: %s\n", why.c_str());
		return false;
	}
	context->gpu = vk_card->gpu();
	context->device = vk_card->device();
	context->queue = vk_card->queue();
	context->queue_family_index = vk_card->queue_family();
	context->presentation_queue = context->queue;
	context->presentation_queue_family_index = context->queue_family_index;
	return true;
}

/* Odin's card goes, before the frontend destroys the device (context_destroy) */
void
vk_destroy_device()
{
	log(RETRO_LOG_INFO, "Vulkan: the device goes\n");
	if (core)
		core->screen.renderer.reset();
	vk_destroy_outputs();
	vk_card.reset();
	vk_iface = nullptr;
}

/* The negotiation interface, on the heap and never freed: RetroArch keeps
 * the pointer and reads it again when it tears its Vulkan context down,
 * after it has unloaded the core, whose own memory is gone by then. No
 * destroy_device either, which it would call there: Odin's card goes in
 * context_destroy, before the frontend destroys the device. */
retro_hw_render_context_negotiation_interface_vulkan *
vk_negotiation()
{
	static retro_hw_render_context_negotiation_interface_vulkan *const iface = [] {
		auto *made = static_cast<retro_hw_render_context_negotiation_interface_vulkan *>(
			std::calloc(1, sizeof(retro_hw_render_context_negotiation_interface_vulkan)));
		made->interface_type = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN;
		made->interface_version = RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION;
		made->get_application_info = vk_application_info;
		made->create_device = vk_create_device;
		made->destroy_device = nullptr;
		made->create_instance = vk_create_instance;
		made->create_device2 = vk_create_device2;
		return made;
	}();
	return iface;
}

/* The picture of the frame (the renderer's image, vk::Context::picture)
 * copied into the image of the frontend's sync index, which the frontend
 * samples after the copy (wait_sync_index: the frontend done with what that
 * image held before) */
constexpr VkComponentMapping OPAQUE = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
	                                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_ONE };

void
present_vk(HostPicture picture)
{
	vk::Image *const from = vk_card && vk_iface ? vk_card->picture(picture.id) : nullptr;

	if (!from || !picture.w || !picture.h) {
		video_cb(nullptr, picture.w ? picture.w : Renderer::WIDTH, picture.h ? picture.h : Renderer::HEIGHT, 0);
		return;
	}
	vk_iface->wait_sync_index(vk_iface->handle);
	const uint32_t index = vk_iface->get_sync_index(vk_iface->handle);
	if (index >= vk_outputs.size()) {
		video_cb(nullptr, picture.w, picture.h, 0);
		return;
	}
	VkOutput &out = vk_outputs[index];
	if (out.image.w != picture.w || out.image.h != picture.h) {
		vk_card->destroy_image(out.image);
		/* The picture's alpha is 0, and RetroArch blends the frame by it:
		 * read as opaque through the view */
		out.image = vk_card->make_image(VK_FORMAT_R8G8B8A8_UNORM, picture.w, picture.h, 1,
		                                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, OPAQUE);
	}

	const VkCommandBuffer cb = vk_card->commands();
	/* Its lines turned over: the picture has the bottom one first, as
	 * OpenGL counts them (VkDrawing::present does the same for the window) */
	const int32_t w = static_cast<int32_t>(picture.w), h = static_cast<int32_t>(picture.h);
	const VkImageBlit region{ { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { { 0, 0, 0 }, { w, h, 1 } },
		                  { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { { 0, h, 0 }, { w, 0, 1 } } };

	vk_card->barrier(cb, *from, vk::COPIED_FROM);
	vk_card->barrier(cb, out.image, vk::COPIED_TO);
	vk_card->fn().vkCmdBlitImage(cb, from->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, out.image.image,
	                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
	vk_card->barrier(cb, out.image, vk::SAMPLED);
	/* Sent before the frontend's frame on the same queue, which the
	 * barrier above orders the frontend's read of the image after, as the
	 * vita3k core does: no semaphore, which RetroArch need not wait for */
	vk_card->end_frame({});
	retro_vulkan_image &image = out.handed;
	image = {};
	image.image_view = out.image.view;
	image.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	image.create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image.create_info.image = out.image.image;
	image.create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	image.create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image.create_info.components = OPAQUE;
	image.create_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	vk_iface->set_image(vk_iface->handle, &image, 0, nullptr, VK_QUEUE_FAMILY_IGNORED);
	video_cb(RETRO_HW_FRAME_BUFFER_VALID, picture.w, picture.h, 0);
}

/* The picture of the frame into the frontend's framebuffer, as large as it
 * was drawn */
void
present(HostPicture picture)
{
	/* The size it is handed over at follows Internal Resolution */
	if (picture.w && picture.h && (picture.w != core->shown_w || picture.h != core->shown_h)) {
		retro_game_geometry geometry{ picture.w, picture.h, Renderer::WIDTH * MOST_SCALE,
		                              Renderer::HEIGHT * MOST_SCALE, 4.0f / 3.0f };

		environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
		core->shown_w = picture.w;
		core->shown_h = picture.h;
	}
	if (api == Api::VULKAN) {
		present_vk(picture);
		return;
	}
	if (!picture.id || !picture.w || !picture.h) {
		video_cb(nullptr, Renderer::WIDTH, Renderer::HEIGHT, 0);
		return;
	}
	/* The picture is the renderer's framebuffer (DrawingGl::frame_picture),
	 * read from as the desktop's GlGraphics::read does */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, picture.id);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(hw_render.get_current_framebuffer()));
	glDisable(GL_SCISSOR_TEST);
	glBlitFramebuffer(0, 0, static_cast<GLint>(picture.w), static_cast<GLint>(picture.h), 0, 0,
	                  static_cast<GLint>(picture.w), static_cast<GLint>(picture.h), GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(hw_render.get_current_framebuffer()));
	if (const GLenum error = glGetError(); error != GL_NO_ERROR)
		log(RETRO_LOG_WARN, "OpenGL error 0x%04X handing the picture over (%ux%u)\n", error, picture.w, picture.h);
	video_cb(RETRO_HW_FRAME_BUFFER_VALID, picture.w, picture.h, 0);
}

/* How hard the optional board's sound would shake a vibration device, 0 to
 * 0xFFFF, and a level smoothed over the frames (AudioOut::shake_of,
 * AudioOut::smoothed, which come with the desktop's sound) */
uint16_t
shake_of(std::span<const int16_t> stereo)
{
	const size_t frames = stereo.size() / 2;
	double sum = 0;

	for (size_t i = 0; i < frames; i++) {
		const double both = (stereo[2 * i] + stereo[2 * i + 1]) / 2.0;
		sum += both * both;
	}
	const double level = frames ? std::sqrt(sum / static_cast<double>(frames)) / 32768.0 : 0;

	return level < 1.0 / 32 ? 0 : static_cast<uint16_t>(std::lround(std::min(1.0, level * 2) * 0xFFFF));
}

uint16_t
smoothed(uint16_t was, uint16_t now)
{
	return static_cast<uint16_t>((3 * was + now) / 4);
}

/* The pads shaken by the frame's outputs, as Controls::show shakes the
 * desktop's gamepads: a rumble output of a player at 0xC000, and player 1's
 * pad also as the vibration device the optional board's sound drives and
 * the jolts of the drive board's wheel */
void
shake_pads()
{
	constexpr uint16_t RUMBLE = 0xC000, FELT_STEP = 0x1000;
	Core &c = *core;

	if (!c.set_rumble)
		return;
	uint16_t strength[2] = {};
	if (c.rumble) {
		/* A Gaelco board: its outputs with how hard each drives, and the
		 * torque of its wheel motor in place of a drive board's */
		const std::span<const uint8_t> outputs = c.gaelco ? c.gaelco->io_outputs() : c.board->io_outputs();
		const std::span<const uint8_t> levels = c.gaelco ? c.gaelco->output_levels() : std::span<const uint8_t>{};
		const Game &game = *c.game;
		const bool still = any_on(game.outputs, GameOutput::Role::MUTE, outputs);

		c.felt_level = !c.gaelco && game.option_vibration && !still ?
		                       smoothed(c.felt_level, shake_of(c.board->sound(true))) :
		                       0;
		const int force = c.gaelco                ? c.gaelco->drive_torque().value_or(0) :
		                  game.drive_first_output ? DriveBoard::force_of(DriveBoard::command(outputs, game.drive_first_output)) :
		                                            0;
		const uint16_t wheel = c.panel.steer_channel();
		c.wheel_moves[c.wheel_move++ % c.wheel_moves.size()] = std::abs(wheel - c.wheel_was) / 65535.0;
		c.wheel_was = wheel;
		const double moved = *std::max_element(c.wheel_moves.begin(), c.wheel_moves.end());
		const uint16_t jolt = DriveBoard::jolt_of(force - c.force_was, moved);
		c.force_was = force;
		c.drive_level = std::max(jolt, smoothed(c.drive_level, jolt));
		const uint16_t felt = static_cast<uint16_t>(std::max(c.felt_level, c.drive_level) / FELT_STEP * FELT_STEP);

		/* As hard as the hardest rumble output of the player that is on
		 * drives (all the way without levels), as Controls::show */
		for (unsigned player = 0; player < 2; player++) {
			unsigned level = 0;

			for (const GameOutput &o : game.outputs)
				if (o.role == GameOutput::Role::RUMBLE && o.player == player && o.on(outputs))
					level = std::max(level, o.output <= levels.size() ? levels[o.output - 1] : 255u);
			const uint16_t driven = static_cast<uint16_t>(RUMBLE * level / 255 / FELT_STEP * FELT_STEP);

			strength[player] = std::max<uint16_t>(driven, player ? 0 : felt);
		}
	}
	for (unsigned player = 0; player < 2; player++) {
		if (strength[player] == c.rumbling[player])
			continue;
		c.set_rumble(player, RETRO_RUMBLE_STRONG, strength[player]);
		c.set_rumble(player, RETRO_RUMBLE_WEAK, c.rumble_weak ? strength[player] : 0);
		c.rumbling[player] = strength[player];
	}
}

/* The sound buffered for the frontend's audio thread (Audio Thread), at
 * most half a second of it */
std::mutex audio_mutex;
std::vector<int16_t> audio_buffer;
constexpr size_t AUDIO_MOST = 44100;
/* The frontend's multi-channel entry points (GET_AUDIO_SAMPLE_BATCH_MULTI),
 * for the games with an optional board: then all their sound goes through
 * them, two or four channels; and the channels of what audio_buffer holds */
retro_audio_sample_multi_callback audio_multi{};
unsigned audio_channels = 2;

/* Frames to the frontend: through the multi-channel call when the game has it */
size_t
send_frames(const int16_t *data, size_t frames, unsigned channels)
{
	if (audio_multi.batch_int16)
		return audio_multi.batch_int16(data, frames, channels,
		                               channels == 4 ? RETRO_AUDIO_LAYOUT_QUAD : RETRO_AUDIO_LAYOUT_STEREO);
	return audio_batch_cb ? audio_batch_cb(data, frames) : 0;
}

/* The sound of the frame, as the desktop's AudioOut::interleave makes it:
 * the main board, the optional one, or both mixed with the optional board at
 * its level (the game's Sound Mode and Optional Board Level; the main board
 * in a game without those); 44 100 pairs a second */
void hand_sound(std::vector<int16_t> &out, unsigned channels);

/* A Gaelco board's frame of sound, as the desktop's AudioOut plays it with
 * a woofer: its speakers' pair, and the woofer's channel on both sides,
 * its lows in Both (AudioOut::woofer_pairs), at its level; 32 227 pairs a
 * second (AtvTrack::DAC_RATE) */
void
play_gaelco_sound()
{
	const std::string &mode = core->sound_mode;
	const bool quad = mode == "quad" && audio_multi.batch_int16;
	const AudioOut::Mode m = mode == "main"   ? AudioOut::Mode::MAIN :
	                         mode == "option" ? AudioOut::Mode::OPTION :
	                         quad             ? AudioOut::Mode::QUAD :
	                                            AudioOut::Mode::MIX;

	/* Woofer Only: what the woofer plays, its channel's lows below Woofer
	 * Cutoff, as Both mixes them in. The channel as it comes sounded the
	 * same as Both (NNshi): it carries more than the woofer plays. */
	AudioOut::woofer_pairs(m == AudioOut::Mode::OPTION ? AudioOut::Mode::MIX : m, core->gaelco->woofer(),
	                       core->woofer_low, core->woofer_pairs);
	/* The woofer at Woofer Level in every mode that plays it: interleave
	 * applies a level in Both only, and 4.0 and Woofer Only took the woofer
	 * as it came (NNshi) */
	const int32_t gain = AudioOut::gain_of(core->option_level);
	for (int16_t &w : core->woofer_pairs)
		w = static_cast<int16_t>(std::clamp<int64_t>((static_cast<int64_t>(w) * gain) >> 15, -0x8000, 0x7FFF));
	AudioOut::interleave(m, core->gaelco->sound(), core->woofer_pairs, AudioOut::UNITY, core->mixed);
	core->mixed_channels = quad ? 4 : 2;
	core->sound_mixed = true;
}

void
play_sound()
{
	if (core->gaelco) {
		play_gaelco_sound();
		return;
	}
	const std::vector<int16_t> &main = core->board->sound(false);
	const std::vector<int16_t> &option = core->board->sound(true);
	std::vector<int16_t> &out = core->mixed;
	const std::string &mode = core->sound_mode;
	const double level = core->option_level;
	const int32_t gain = static_cast<int32_t>(std::lround(32768 * std::pow(10.0, level / 20.0)));
	const auto at = [](const std::vector<int16_t> &v, size_t i) { return i < v.size() ? v[i] : int16_t{ 0 }; };

	/* 4.0: four channels where the frontend takes them, the optional board at
	 * its level too; else Both */
	const auto scaled = [gain](int16_t o) {
		return static_cast<int16_t>(std::clamp<int64_t>((static_cast<int64_t>(o) * gain) >> 15, -0x8000, 0x7FFF));
	};
	const bool quad = mode == "quad" && audio_multi.batch_int16;
	const unsigned channels = quad ? 4 : 2;

	out.clear();
	if (quad) {
		for (size_t i = 0; i + 1 < main.size(); i += 2)
			out.insert(out.end(), { main[i], main[i + 1], scaled(at(option, i)), scaled(at(option, i + 1)) });
	} else
	for (size_t i = 0; i < main.size(); i++) {
		const int16_t m = main[i], o = at(option, i);
		if (mode == "option")
			out.push_back(o);
		else if (mode == "mix" || mode == "quad")
			out.push_back(static_cast<int16_t>(
				std::clamp<int64_t>(m + ((static_cast<int64_t>(o) * gain) >> 15), -0x8000, 0x7FFF)));
		else
			out.push_back(m);
	}
	core->mixed_channels = channels;
	core->sound_mixed = true;
}

/* The sound play_sound mixed, to the frontend. With Threaded Board this comes
 * after the next frame has started on the board's thread: with the
 * frontend's audio sync the hand-over waits until there is room, and waiting
 * with the board at rest made each frame that much later - uneven frames
 * with Audio Thread off, the Gaelco games most (NNshi) */
void
hand_frame_sound()
{
	if (!core->sound_mixed)
		return;
	core->sound_mixed = false;
	hand_sound(core->mixed, core->mixed_channels);
}

/* A frame's sound to the frontend, or to the buffer of its audio thread */
void
hand_sound(std::vector<int16_t> &out, unsigned channels)
{
	out.resize(out.size() - out.size() % channels);

	if (core->audio_thread) {
		std::lock_guard lock(audio_mutex);
		/* Another channel count (Sound Mode changed): what was kept is dropped */
		if (audio_channels != channels) {
			audio_buffer.clear();
			audio_channels = channels;
		}
		audio_buffer.insert(audio_buffer.end(), out.begin(), out.end());
		if (audio_buffer.size() > channels * AUDIO_MOST)
			audio_buffer.erase(audio_buffer.begin(), audio_buffer.end() - channels * AUDIO_MOST);
		return;
	}
	for (size_t done = 0; done + channels <= out.size();) {
		const size_t frames = (out.size() - done) / channels;
		const size_t taken = send_frames(out.data() + done, frames, channels);

		if (!taken)
			break;
		done += taken * channels;
	}
}

/* The frontend's audio thread asks for sound (SET_AUDIO_CALLBACK): what the
 * frames buffered, a chunk at a time */
void
audio_thread_callback()
{
	int16_t chunk[1024 * 4];
	size_t frames = 0;
	unsigned channels = 2;

	{
		std::lock_guard lock(audio_mutex);
		channels = audio_channels;
		frames = std::min<size_t>(audio_buffer.size() / channels, 1024);
		std::copy_n(audio_buffer.begin(), frames * channels, chunk);
		audio_buffer.erase(audio_buffer.begin(), audio_buffer.begin() + static_cast<std::ptrdiff_t>(frames * channels));
	}
	if (frames)
		send_frames(chunk, frames, channels);
	else
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

void
audio_thread_state(bool) {}

} /* namespace */

/* The game's saves folder (defined below) */
SaveFolder game_saves();

/* ─── A Gaelco board ───────────────────────────────────────────────────── */

namespace {

/* The board of a Gaelco game, its settings kept in kept.bin of the game's
 * saves as the desktop keeps them; Smashing Drive's volume set at power-on
 * (Start Volume) */
void
make_gaelco()
{
	const std::string kept = game_saves().path("kept.bin");
	std::error_code ec;
	std::optional<unsigned> volume;

	fs::create_directories(fs::path(kept).parent_path(), ec);
	if (core->game->machine == "smashdrv")
		volume = GaelcoRun::volume_up_frames(core->start_volume);
	core->gaelco = std::make_unique<GaelcoRun>(*core->game, kept, volume);
}

/* Threaded Board: the board's frames on a thread of their own */
void
start_board_thread()
{
	if (!core->threaded)
		return;
	if (core->gaelco)
		core->board_thread.start([] {
			core->gaelco->run_frame(core->gaelco_in, core->gaelco_fine, core->gaelco_flat, core->gaelco_sorts);
		});
	else
		core->board_thread.start(*core->board);
}

/* The controls and the look of the next frame, as the desktop's
 * AtvGame::run_frame sets them: Snap where Native has no geometry shaders */
void
gaelco_next()
{
	core->panel.read_inputs(core->gaelco_in);
	core->gaelco_fine = core->scale;
	const DisplayUpscaleMode mode = core->upscale_mode == DisplayUpscaleMode::NATIVE && !core->own->draws_native() ?
	                                        DisplayUpscaleMode::SNAP :
	                                        core->upscale_mode;
	core->gaelco_flat = static_cast<unsigned>(mode);
	core->gaelco_sorts = core->own->sorts_per_pixel();
}

/* The frame the board ran: its scene, if it drew one, its sound and shakes */
void
take_gaelco_frame()
{
	if (PmxScene scene = core->gaelco->take_scene(); scene.rendered) {
		core->scene = std::move(scene);
		core->scene_fresh = true;
	}
	core->blanked = core->gaelco->blanked();
	play_sound();
	shake_pads();
}

/* Black in place of the picture while the board's DAC blanks it; the
 * picture before again on Vulkan */
void
present_black(HostPicture picture)
{
	const unsigned w = picture.w ? picture.w : Renderer::WIDTH * core->scale;
	const unsigned h = picture.h ? picture.h : Renderer::HEIGHT * core->scale;

	if (api == Api::VULKAN) {
		video_cb(nullptr, w, h, 0);
		return;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(hw_render.get_current_framebuffer()));
	glDisable(GL_SCISSOR_TEST);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	video_cb(RETRO_HW_FRAME_BUFFER_VALID, w, h, 0);
}

/* The last scene drawn, as the desktop's Display::draw_own: a new one drawn
 * at the picture scale, else the picture before again */
void
draw_gaelco()
{
	Drawing &own = *core->own;

	if (core->scene_fresh) {
		if (core->scale != core->own_scale) {
			own.set_scale(core->scale);
			core->own_scale = core->scale;
		}
		own.draw_pmx(core->scene, core->anisotropy);
		core->scene_fresh = false;
	}
	if (core->blanked)
		present_black(own.frame_picture());
	else
		present(own.frame_picture());
}

/* A frame of a Gaelco game, run and drawn in turn, or with Threaded Board
 * drawn while the board runs the next */
void
run_gaelco()
{
	if (api == Api::OPENGL)
		while (glGetError() != GL_NO_ERROR)
			;
	if (!core->threaded) {
		gaelco_next();
		core->gaelco->run_frame(core->gaelco_in, core->gaelco_fine, core->gaelco_flat, core->gaelco_sorts);
		take_gaelco_frame();
		hand_frame_sound();
		draw_gaelco();
		return;
	}
	core->board_thread.wait();
	const bool pending = core->frame_pending;

	if (pending)
		take_gaelco_frame();
	gaelco_next();
	core->board_thread.run_frame();
	core->frame_pending = true;
	hand_frame_sound();
	if (pending)
		draw_gaelco();
	else
		video_cb(nullptr, Renderer::WIDTH * core->scale, Renderer::HEIGHT * core->scale, 0);
}

} /* namespace */

/* GLEW's loader (shim/GL/osmesa.h): the frontend's functions */
extern "C" OSMESAproc
OSMesaGetProcAddress(const char *name)
{
	return reinterpret_cast<OSMESAproc>(hw_render.get_proc_address ? hw_render.get_proc_address(name) : nullptr);
}

/* ─── libretro API ─────────────────────────────────────────────────────── */

RETRO_API unsigned
retro_api_version(void)
{
	return RETRO_API_VERSION;
}

RETRO_API void
retro_set_environment(retro_environment_t cb)
{
	environ_cb = cb;
	retro_log_callback logging;
	if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
		log_cb = logging.log;
	bool no_game = false;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
	bool categories_supported = false;
	libretro_set_core_options(cb, &categories_supported);
	show_sound_options(nullptr);

	/* RetroPad, or Light Gun for a sight on the light gun */
	static const retro_controller_description DEVICES[] = {
		{ "RetroPad", RETRO_DEVICE_JOYPAD },
		{ "Light Gun", RETRO_DEVICE_LIGHTGUN },
	};
	static const retro_controller_info PORTS[] = {
		{ DEVICES, 2 },
		{ DEVICES, 2 },
		{ nullptr, 0 },
	};
	cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, const_cast<retro_controller_info *>(PORTS));
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t) {}
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

RETRO_API void
retro_get_system_info(retro_system_info *info)
{
	std::memset(info, 0, sizeof(*info));
	info->library_name = "Odin";
#ifdef ODIN_LIBRETRO_VERSION
	info->library_version = ODIN_LIBRETRO_VERSION;
#else
	info->library_version = "libretro";
#endif
	info->valid_extensions = "zip|7z";
	info->need_fullpath = true;
	info->block_extract = true;
}

RETRO_API void
retro_get_system_av_info(retro_system_av_info *info)
{
	std::memset(info, 0, sizeof(*info));
	const unsigned scale = core ? core->scale : 1;

	info->geometry.base_width = Renderer::WIDTH * scale;
	info->geometry.base_height = Renderer::HEIGHT * scale;
	info->geometry.max_width = Renderer::WIDTH * MOST_SCALE;
	info->geometry.max_height = Renderer::HEIGHT * MOST_SCALE;
	info->geometry.aspect_ratio = 4.0f / 3.0f;
	/* The board's frames as the desktop runs them: a Hikaru's 60 a second
	 * (its own timing gives 60.08, hikaru.cpp); a Gaelco board's frame of
	 * its screen, 59.3916 a second (atvtrack.cpp), as long as the desktop
	 * holds each (frame_nanoseconds). At 60 a Gaelco game handed over 1 %
	 * more sound than the frontend played in that time: its buffer full
	 * and, with audio sync, uneven frames (NNshi) */
	const bool gaelco = core && core->gaelco;
	info->timing.fps = gaelco ? 59.3916 : 60.0;
	/* A Gaelco board's DAC: 32 227 pairs a second */
	info->timing.sample_rate = gaelco ? static_cast<double>(AtvTrack::DAC_RATE) : 44100.0;
}

RETRO_API void retro_init(void) {}

RETRO_API void
retro_deinit(void)
{
	core.reset();
}

RETRO_API void
retro_set_controller_port_device(unsigned port, unsigned device)
{
	if (port < 2)
		port_device[port] = device;
	if (core && core->game)
		core->panel.describe();
}

RETRO_API bool
retro_load_game(const retro_game_info *info)
{
	if (!info || !info->path)
		return false;

	core = std::make_unique<Core>();
	state_size = 0;
	const char *dir = nullptr;
	core->system_dir = environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir ? dir : ".";
	dir = nullptr;
	core->save_dir = environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir ? dir : core->system_dir;

	if (!load_game_list(*core)) {
		log(RETRO_LOG_ERROR, "the game list could not be read\n");
		core.reset();
		return false;
	}

	/* The game by the name of its set, its files beside it and in
	 * system/odin, where the shared sets (hikaru, mie) may be too */
	const fs::path content(info->path);
	std::string name = content.stem().string();
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	if (!core->games.has(name)) {
		log(RETRO_LOG_ERROR, "'%s' is not a game Odin knows (a MAME set name: airtrix, braveff, pharrier, sgnascar, swracer, von4, atvtrack, gfootbal, smashdrv...)\n", name.c_str());
		message("Odin: not a Hikaru or Gaelco game set (airtrix, braveff, pharrier, atvtrack, smashdrv...)");
		core.reset();
		return false;
	}
	RomFiles files(content.parent_path().string(), (fs::path(core->system_dir) / "odin").string());
	core->game = core->games.load_game(files, name);
	if (!core->game) {
		std::string missing;
		for (const RomFiles::File &file : core->games.missing(files, name)) {
			log(RETRO_LOG_ERROR, "%s: %s is missing; MAME keeps it in %s.zip\n", name.c_str(), file.name.c_str(),
			    file.set_of(name).c_str());
			if (missing.find(file.set_of(name)) == std::string::npos)
				missing += (missing.empty() ? "" : ", ") + file.set_of(name) + ".zip";
		}
		message("Odin: files missing from " + (missing.empty() ? name + ".zip" : missing) +
		        " (beside the game or in system/odin)", 600);
		core.reset();
		return false;
	}
	const bool gaelco = core->game->machine == "atvtrack" || core->game->machine == "smashdrv";
	if (core->game->machine != "hikaru" && !gaelco) {
		log(RETRO_LOG_ERROR, "'%s' is a %s game, neither a Hikaru nor a Gaelco game\n", name.c_str(), core->game->machine.c_str());
		core.reset();
		return false;
	}

	read_options(*core);
	show_sound_options(sound_game(core->game->name));

	retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
	environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);

	/* Renderer: the one asked for, or with Automatic the one of the
	 * frontend's video driver; the other one when the frontend cannot give
	 * that one */
	const char *renderer_option = option_value("odin_renderer");
	bool want_vulkan;
	if (renderer_option && !std::strcmp(renderer_option, "vulkan"))
		want_vulkan = true;
	else if (renderer_option && !std::strcmp(renderer_option, "opengl"))
		want_vulkan = false;
	else {
		/* OpenGL where RetroArch's driver is one of OpenGL's, Vulkan for
		 * any other (d3d, metal...), RetroArch then switching to it (NNshi) */
		unsigned preferred = RETRO_HW_CONTEXT_NONE;
		const bool known = environ_cb(RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER, &preferred);
		want_vulkan = !(known && (preferred == RETRO_HW_CONTEXT_OPENGL || preferred == RETRO_HW_CONTEXT_OPENGL_CORE ||
		                          preferred == RETRO_HW_CONTEXT_OPENGLES2 || preferred == RETRO_HW_CONTEXT_OPENGLES3 ||
		                          preferred == RETRO_HW_CONTEXT_OPENGLES_VERSION));
	}
	const auto set_context = [](bool vulkan) {
		std::memset(&hw_render, 0, sizeof(hw_render));
		hw_render.context_reset = context_reset;
		hw_render.context_destroy = context_destroy;
		if (vulkan) {
			hw_render.context_type = RETRO_HW_CONTEXT_VULKAN;
			hw_render.version_major = VK_API_VERSION_1_3;
		} else {
			hw_render.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
			hw_render.version_major = 4;
			hw_render.version_minor = 3;
			hw_render.depth = true;
			hw_render.bottom_left_origin = true;
		}
		if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
			return false;
		if (vulkan)
			environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, vk_negotiation());
		api = vulkan ? Api::VULKAN : Api::OPENGL;
		return true;
	};
	if (!set_context(want_vulkan)) {
		if (!set_context(!want_vulkan)) {
			log(RETRO_LOG_ERROR, "the frontend gave neither Vulkan nor an OpenGL 4.3 core context\n");
			message("Odin needs Vulkan 1.3 or OpenGL 4.3: set Settings > Drivers > Video to vulkan or glcore", 600);
			core.reset();
			return false;
		}
		message(std::string("Odin: RetroArch could not give the core ") + (want_vulkan ? "Vulkan" : "OpenGL") +
		        "; running on " + (want_vulkan ? "OpenGL" : "Vulkan"));
	}
	log(RETRO_LOG_INFO, "renderer: %s\n", api == Api::VULKAN ? "Vulkan" : "OpenGL");

	if (gaelco)
		make_gaelco();
	else
		core->board = Hikaru::make(*core->game, false);
	if (!core->board && !core->gaelco) {
		log(RETRO_LOG_ERROR, "the board could not be made for '%s'\n", name.c_str());
		core.reset();
		return false;
	}
	core->panel.set_game(*core->game);
	core->panel.describe();
	/* Multi-channel sound for the games with an optional board (Quad),
	 * negotiated once at the load */
	audio_multi = {};
	if (sound_game(core->game->name) &&
	    !environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_SAMPLE_BATCH_MULTI, &audio_multi))
		audio_multi = {};
	log(RETRO_LOG_INFO, "sound: multi-channel %s\n", audio_multi.batch_int16 ? "taken (4.0 has four channels)" :
	                                                                         "not offered (4.0 is Both)");
	{
		const char *v = option_value("odin_audio_thread");
		const retro_audio_callback callback{ audio_thread_callback, audio_thread_state };
		core->audio_thread = v && !std::strcmp(v, "enabled") &&
		                     environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK, const_cast<retro_audio_callback *>(&callback));
		std::lock_guard lock(audio_mutex);
		audio_buffer.clear();
		audio_channels = 2;
		if (core->audio_thread)
			log(RETRO_LOG_INFO, "sound: on RetroArch's audio thread\n");
	}
	retro_rumble_interface rumble{};
	if (environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble))
		core->set_rumble = rumble.set_rumble_state;
	if (core->board) {
		core->board->set_screen(&core->screen);
		core->board->set_panel(&core->panel);
		core->board->keep_in(game_saves());
		core->board->reset();
	}
	/* Taken when the content starts (Restart) */
	const char *threaded = option_value("odin_threaded");
	core->threaded = !threaded || std::strcmp(threaded, "disabled") != 0;
	start_board_thread();
	log(RETRO_LOG_INFO, "%s (%s) loaded%s\n", core->game->title.c_str(), name.c_str(),
	    core->threaded ? ", the board on a thread of its own" : "");
	return true;
}

RETRO_API bool retro_load_game_special(unsigned, const retro_game_info *, size_t) { return false; }

RETRO_API void
retro_unload_game(void)
{
	if (!core)
		return;
	log(RETRO_LOG_INFO, "content unloaded\n");
	show_sound_options(nullptr);
	audio_multi = {};
	for (unsigned player = 0; player < 2; player++)
		if (core->set_rumble && core->rumbling[player]) {
			core->set_rumble(player, RETRO_RUMBLE_STRONG, 0);
			core->set_rumble(player, RETRO_RUMBLE_WEAK, 0);
		}
	core->board_thread.stop();
	if (core->board)
		core->board->power_off();
	if (core->gaelco)
		core->gaelco->power_off();
	/* The renderer's images may still be read by the frontend's frames */
	if (api == Api::VULKAN && vk_card)
		vk_card->fn().vkDeviceWaitIdle(vk_card->device());
	core->screen.renderer.reset();
	core->own.reset();
	core.reset();
}

/* saves/odin/<game>; saves/Odin/<game> where the frontend already keeps
 * each core's saves in a folder of the core's name */
SaveFolder
game_saves()
{
	const fs::path saves = fs::path(core->save_dir);
	std::string folder = saves.filename().string();
	std::transform(folder.begin(), folder.end(), folder.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	return SaveFolder(((folder == "odin" ? saves : saves / "odin") / core->game->name).string());
}

/* The board at rest between two frames: its thread, if it has one, done
 * with the frame it runs (which stays to be drawn: frame_pending) */
static void
board_at_rest()
{
	if (core && core->board_thread.running())
		core->board_thread.wait();
}

RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

/* The frames a second the core ran over the last SPEED_FRAMES, for the
 * log: the board's 60 is full speed */
static void
log_speed(unsigned frame)
{
	const auto now = std::chrono::steady_clock::now();
	if (core->speed_frames++ == 0)
		core->speed_since = now;
	else if (core->speed_frames > SPEED_FRAMES) {
		const double seconds = std::chrono::duration<double>(now - core->speed_since).count();

		log(RETRO_LOG_INFO, "speed: %.1f frames a second (60 is full speed), frame %u\n", SPEED_FRAMES / seconds, frame);
		core->speed_frames = 1;
		core->speed_since = now;
	}
}

RETRO_API void
retro_run(void)
{
	if (!core || (!core->board && !core->gaelco))
		return;
	if (input_poll_cb)
		input_poll_cb();
	core->panel.poll();
	if (core->gaelco) {
		if (!core->own && core->context_ready)
			create_drawing();
		if (!core->own) {
			video_cb(nullptr, Renderer::WIDTH, Renderer::HEIGHT, 0);
			return;
		}
		bool updated = false;
		if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
			read_options(*core);
		run_gaelco();
		log_speed(core->gaelco->frame());
		return;
	}
	if (!core->screen.renderer) {
		if (core->context_ready)
			create_renderer();
		if (!core->screen.renderer) {
			video_cb(nullptr, Renderer::WIDTH, Renderer::HEIGHT, 0);
			return;
		}
	}

	bool updated = false;
	if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
		read_options(*core);

	Renderer &renderer = *core->screen.renderer;

	/* What the frontend left in the error flag is not the renderer's,
	 * which stops on any error it finds after a frame (gl::check) */
	if (api == Api::OPENGL)
		while (glGetError() != GL_NO_ERROR)
			;
	const auto draw = [&] {
		/* As the desktop's Display::draw sets them, right before the drawing */
		renderer.set_scale(core->scale);
		renderer.set_upscale_mode(core->upscale_mode);
		renderer.set_anisotropy(core->anisotropy);
		renderer.draw_frame();
	};

	if (!core->threaded) {
		renderer.begin_frame();
		core->board->run_frame();
		renderer.take_frame(core->board->frame(), Renderer::WIDTH, Renderer::HEIGHT);
		draw();
		present(renderer.frame_picture());
		play_sound();
		hand_frame_sound();
		shake_pads();
	} else {
		/* The frame the thread ran is taken with the board at rest, its
		 * sound mixed before the next frame clears it; then the next frame
		 * runs on the thread while this one's sound is handed over and it
		 * is drawn */
		core->board_thread.wait();
		const bool pending = core->frame_pending;

		if (pending) {
			renderer.take_frame(core->board->frame(), Renderer::WIDTH, Renderer::HEIGHT);
			play_sound();
			shake_pads();
		}
		core->screen.set_drawing(pending);
		renderer.begin_frame();
		core->board_thread.run_frame();
		core->frame_pending = true;
		hand_frame_sound();
		if (pending) {
			draw();
			core->screen.set_drawing(false);
			present(renderer.frame_picture());
		} else
			video_cb(nullptr, Renderer::WIDTH * core->scale, Renderer::HEIGHT * core->scale, 0);
	}

	log_speed(core->board->frame());
}

RETRO_API void
retro_reset(void)
{
	/* As the desktop's Reset (Ended::RESTART, a new run_game): a new board,
	 * not the old one's reset, which left the I/O board unseen (I/O BD IS
	 * NOT CONNECTED TO GAME BD in Planet Harriers, NNshi) */
	if (core && core->gaelco) {
		/* A new Gaelco board, its settings kept as at a power-off */
		core->board_thread.stop();
		core->gaelco->power_off();
		core->gaelco.reset();
		make_gaelco();
		core->panel.set_game(*core->game);
		core->frame_pending = core->scene_fresh = core->blanked = core->sound_mixed = false;
		start_board_thread();
		log(RETRO_LOG_INFO, "reset: the board made again\n");
		return;
	}
	if (!core || !core->board)
		return;
	core->board_thread.stop();
	core->board->power_off();
	core->frame_pending = false;
	core->sound_mixed = false;
	/* The renderer reads the old board's memory (create_renderer): it goes
	 * first and is made again for the new board, as the desktop attaches
	 * its renderer to each run */
	if (api == Api::VULKAN && vk_card)
		vk_card->fn().vkDeviceWaitIdle(vk_card->device());
	core->screen.renderer.reset();
	std::unique_ptr<Hikaru> board = Hikaru::make(*core->game, false);
	if (!board) {
		log(RETRO_LOG_ERROR, "reset: the board could not be made again\n");
		return;
	}
	core->board = std::move(board);
	core->panel.set_game(*core->game);
	core->board->set_screen(&core->screen);
	core->board->set_panel(&core->panel);
	core->board->keep_in(game_saves());
	core->board->reset();
	if (core->context_ready)
		create_renderer();
	start_board_thread();
	log(RETRO_LOG_INFO, "reset: the board made again\n");
}

/* Save states. The board writes and reads them as files (StateFile,
 * hikaru_state.cpp): through a file of the core's own beside the game's
 * saves, whose bytes the frontend gets after a header of their count. Their
 * size changes little from a frame to the next; the frontend's buffer is
 * the size of the first one with room to spare (STATE_ROOM). */
namespace {

constexpr size_t STATE_HEADER = 8, STATE_ROOM = 1 << 20;

std::string
state_temp()
{
	return (fs::path(core->save_dir) / ("odin-" + core->game->name + ".state.tmp")).string();
}

/* The board's state as bytes, or nothing */
std::optional<std::vector<uint8_t>>
state_bytes()
{
	if (!core || (!core->board && !core->gaelco))
		return std::nullopt;
	const std::string path = state_temp();
	std::error_code ec;

	board_at_rest();
	if (!(core->gaelco ? core->gaelco->save_state(path) : core->board->save_state(path))) {
		fs::remove(path, ec);
		return std::nullopt;
	}
	std::ifstream in(path, std::ios::binary);
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();
	fs::remove(path, ec);
	return bytes;
}

} /* namespace */

RETRO_API size_t
retro_serialize_size(void)
{
	if (!state_size) {
		const std::optional<std::vector<uint8_t>> bytes = state_bytes();

		if (bytes)
			state_size = STATE_HEADER + bytes->size() + STATE_ROOM;
	}
	return state_size;
}

RETRO_API bool
retro_serialize(void *data, size_t size)
{
	const std::optional<std::vector<uint8_t>> bytes = state_bytes();

	if (!bytes || STATE_HEADER + bytes->size() > size) {
		if (bytes)
			log(RETRO_LOG_ERROR, "save state: %zu bytes, more than the %zu the frontend has room for\n",
			    bytes->size(), size);
		return false;
	}
	const uint64_t count = bytes->size();
	std::memcpy(data, &count, STATE_HEADER);
	std::memcpy(static_cast<uint8_t *>(data) + STATE_HEADER, bytes->data(), bytes->size());
	return true;
}

RETRO_API bool
retro_unserialize(const void *data, size_t size)
{
	uint64_t count = 0;

	if (!core || (!core->board && !core->gaelco) || size < STATE_HEADER)
		return false;
	std::memcpy(&count, data, STATE_HEADER);
	if (count > size - STATE_HEADER)
		return false;

	const std::string path = state_temp();
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(static_cast<const char *>(data) + STATE_HEADER, static_cast<std::streamsize>(count));
		if (!out)
			return false;
	}
	board_at_rest();
	const bool ok = core->gaelco ? core->gaelco->load_state(path) : core->board->load_state(path);
	core->frame_pending = core->scene_fresh = false;
	std::error_code ec;

	fs::remove(path, ec);
	if (!ok)
		log(RETRO_LOG_ERROR, "save state: not one of this game and build; the game started again\n");
	return ok;
}
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned, bool, const char *) {}
RETRO_API void *retro_get_memory_data(unsigned) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned) { return 0; }
