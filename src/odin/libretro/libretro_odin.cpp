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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "libretro.h"
#include <GL/osmesa.h>

#include "odin/host/app/games.hpp"
#include "odin/host/app/rom_files.hpp"
#include "odin/host/app/user_files.hpp"
#include "odin/host/gfx/host_graphics.hpp"
#include "odin/host/input/roles.hpp"
#include "odin/host/renderer/hikaru/renderer.hpp"
#include "odin/machine/boards/hikaru/hikaru.hpp"
#include "odin/machine/boards/hikaru/hikaru_host.hpp"
#include "odin/machine/core/state_file.hpp"

extern "C" const char odin_games_json[];

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
	bool vulkan() const override { return false; }
	const char *api() const override { return "OpenGL"; }
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
	std::unique_ptr<Drawing> make_drawing() override { return Drawing::make_gl(); }

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

	void list_launched() override
	{
		if (renderer)
			renderer->launch();
	}
	void before_texture_write(unsigned bank, uint32_t x, uint32_t y, uint32_t w, uint32_t h) override
	{
		if (renderer)
			renderer->invalidate_textures(bank, x, y, w, h);
	}
	void before_memory_changes() override {}
	void memory_changed() override
	{
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
};

/* ─── The control panel ────────────────────────────────────────────────── */

/* The roles of the game list (roles.hpp) on the RetroPad, the analog sticks
 * and triggers and the light gun, as the desktop gives them to a gamepad:
 *
 *  START Start, SERVICE L3, COIN1/COIN2 Select of player 1/2, TEST R3 of
 *  player 1; UP/DOWN/LEFT/RIGHT the D-pad; BUTTON1 to BUTTON8 B, A, Y, X,
 *  L, R, L2, R2; STICK the left stick (and the D-pad), STICK2 the right one;
 *  GEAR_UP/GEAR_DOWN and a gear lever R and L; STEER the left stick across,
 *  ACCEL R2, BRAKE L2; GUN_X/GUN_Y the light gun, GUN_OFFSCREEN its reload.
 */
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
				if (switch_on(control.role, player))
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
			return input_state_cb &&
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

	static uint16_t analog(const GameControl &control, unsigned player)
	{
		const unsigned L = RETRO_DEVICE_INDEX_ANALOG_LEFT, R = RETRO_DEVICE_INDEX_ANALOG_RIGHT;

		switch (control.role) {
		case Role::STEER:
		case Role::STICK_X: return channel(control.low, axis(player, L, RETRO_DEVICE_ID_ANALOG_X), false);
		case Role::STICK_Y: return channel(control.low, axis(player, L, RETRO_DEVICE_ID_ANALOG_Y), false);
		case Role::STICK2_X: return channel(control.low, axis(player, R, RETRO_DEVICE_ID_ANALOG_X), false);
		case Role::STICK2_Y: return channel(control.low, axis(player, R, RETRO_DEVICE_ID_ANALOG_Y), false);
		case Role::ACCEL: return channel(control.low, trigger(player, RETRO_DEVICE_ID_JOYPAD_R2), true);
		case Role::BRAKE: return channel(control.low, trigger(player, RETRO_DEVICE_ID_JOYPAD_L2), true);
		case Role::GUN_X:
		case Role::GUN_Y: {
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
	LibretroScreen screen;
	LibretroPanel panel;
	LibretroGraphics graphics;
	std::string system_dir, save_dir;
	bool context_ready = false;
	GLuint read_fbo = 0;
	std::vector<int16_t> mixed;
};

std::unique_ptr<Core> core;

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

void
create_renderer()
{
	if (!core || !core->board || core->screen.renderer)
		return;

	Renderer::Memory memory;
	Renderer::Options looks;

	memory.fb = core->board->framebuffer_ram();
	memory.fb_writes = &core->board->framebuffer_writes();
	memory.texram[0] = core->board->texture_ram(0);
	memory.texram[1] = core->board->texture_ram(1);
	core->screen.renderer = std::make_unique<Renderer>(core->graphics, memory, looks);
	core->screen.renderer->set_gpu(&core->board->gpu());
}

void
context_reset()
{
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
	if (!core)
		return;
	core->screen.renderer.reset();
	if (core->read_fbo)
		glDeleteFramebuffers(1, &core->read_fbo);
	core->read_fbo = 0;
	core->context_ready = false;
}

/* The picture of the frame into the frontend's framebuffer, as large as it
 * was drawn */
void
present(HostPicture picture)
{
	if (!picture.id || !picture.w || !picture.h) {
		video_cb(nullptr, Renderer::WIDTH, Renderer::HEIGHT, 0);
		return;
	}
	if (!core->read_fbo)
		glGenFramebuffers(1, &core->read_fbo);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, core->read_fbo);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, picture.id, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(hw_render.get_current_framebuffer()));
	glDisable(GL_SCISSOR_TEST);
	glBlitFramebuffer(0, 0, static_cast<GLint>(picture.w), static_cast<GLint>(picture.h), 0, 0,
	                  static_cast<GLint>(picture.w), static_cast<GLint>(picture.h), GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(hw_render.get_current_framebuffer()));
	video_cb(RETRO_HW_FRAME_BUFFER_VALID, picture.w, picture.h, 0);
}

/* The sound of the frame: the main board's, with the optional board's in the
 * mix at the level of the list where the cabinet has speakers for it (the
 * desktop's -o mix); 44 100 pairs a second */
void
play_sound()
{
	const std::vector<int16_t> &main = core->board->sound(false);
	const std::vector<int16_t> &option = core->board->sound(true);
	std::vector<int16_t> &out = core->mixed;

	out.assign(main.begin(), main.end());
	if (core->game->option_speakers && !option.empty()) {
		const double gain = std::pow(10.0, core->game->option_level / 20.0);

		if (out.size() < option.size())
			out.resize(option.size(), 0);
		for (size_t i = 0; i < option.size(); i++)
			out[i] = static_cast<int16_t>(std::clamp(std::lround(out[i] + option[i] * gain), -32768L, 32767L));
	}
	for (size_t done = 0; done + 1 < out.size();) {
		const size_t frames = (out.size() - done) / 2;
		const size_t taken = audio_batch_cb(out.data() + done, frames);

		if (!taken)
			break;
		done += taken * 2;
	}
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
	info->geometry.base_width = Renderer::WIDTH;
	info->geometry.base_height = Renderer::HEIGHT;
	info->geometry.max_width = Renderer::WIDTH * 4;
	info->geometry.max_height = Renderer::HEIGHT * 4;
	info->geometry.aspect_ratio = 4.0f / 3.0f;
	/* The board's frames as the desktop runs them: 60 a second (its own
	 * timing gives 60.08, hikaru.cpp) */
	info->timing.fps = 60.0;
	info->timing.sample_rate = 44100.0;
}

RETRO_API void retro_init(void) {}

RETRO_API void
retro_deinit(void)
{
	core.reset();
}

RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}

RETRO_API bool
retro_load_game(const retro_game_info *info)
{
	if (!info || !info->path)
		return false;

	core = std::make_unique<Core>();
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
		log(RETRO_LOG_ERROR, "'%s' is not a game Odin knows (a MAME set name: airtrix, braveff, pharrier, sgnascar, swracer, von4...)\n", name.c_str());
		message("Odin: not a Hikaru game set (airtrix, braveff, pharrier, sgnascar, swracer, von4...)");
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
	if (core->game->machine != "hikaru") {
		log(RETRO_LOG_ERROR, "'%s' is a %s game, not a Hikaru game\n", name.c_str(), core->game->machine.c_str());
		core.reset();
		return false;
	}

	retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
	environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);

	std::memset(&hw_render, 0, sizeof(hw_render));
	hw_render.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
	hw_render.version_major = 4;
	hw_render.version_minor = 3;
	hw_render.context_reset = context_reset;
	hw_render.context_destroy = context_destroy;
	hw_render.depth = true;
	hw_render.stencil = false;
	hw_render.bottom_left_origin = true;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render)) {
		log(RETRO_LOG_ERROR, "the frontend gave no OpenGL 4.3 core context\n");
		message("Odin needs OpenGL 4.3: set Settings > Drivers > Video to glcore", 600);
		core.reset();
		return false;
	}

	core->board = Hikaru::make(*core->game, false);
	if (!core->board) {
		log(RETRO_LOG_ERROR, "the board could not be made for '%s'\n", name.c_str());
		core.reset();
		return false;
	}
	core->panel.set_game(*core->game);
	core->board->set_screen(&core->screen);
	core->board->set_panel(&core->panel);
	core->board->keep_in(SaveFolder((fs::path(core->save_dir) / "odin" / name).string()));
	core->board->reset();
	log(RETRO_LOG_INFO, "%s (%s) loaded\n", core->game->title.c_str(), name.c_str());
	return true;
}

RETRO_API bool retro_load_game_special(unsigned, const retro_game_info *, size_t) { return false; }

RETRO_API void
retro_unload_game(void)
{
	if (!core)
		return;
	if (core->board)
		core->board->power_off();
	core->screen.renderer.reset();
	core.reset();
}

RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

RETRO_API void
retro_run(void)
{
	if (!core || !core->board)
		return;
	if (input_poll_cb)
		input_poll_cb();
	core->panel.poll();
	if (!core->screen.renderer) {
		if (core->context_ready)
			create_renderer();
		if (!core->screen.renderer) {
			video_cb(nullptr, Renderer::WIDTH, Renderer::HEIGHT, 0);
			return;
		}
	}

	Renderer &renderer = *core->screen.renderer;

	renderer.begin_frame();
	core->board->run_frame();
	renderer.take_frame(core->board->frame(), Renderer::WIDTH, Renderer::HEIGHT);
	renderer.draw_frame();
	present(renderer.frame_picture());
	play_sound();
}

RETRO_API void
retro_reset(void)
{
	if (core && core->board)
		core->board->reset();
}

/* Save states come later: the board writes them to files (hikaru_state.cpp) */
RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void *, size_t) { return false; }
RETRO_API bool retro_unserialize(const void *, size_t) { return false; }
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned, bool, const char *) {}
RETRO_API void *retro_get_memory_data(unsigned) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned) { return 0; }
