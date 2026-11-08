/*
 * Odin libretro core: a Gaelco board (ATV Track, Smashing Drive, Gaelco
 * Football) run a frame at a time, as the desktop's AtvGame runs it
 * (host/app/atvtrack_play.cpp), with the controls given as CabinetInputs
 * and the picture taken as the Neon 250's scene.
 *
 * Odin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#ifndef ODIN_LIBRETRO_GAELCO_RUN_HPP
#define ODIN_LIBRETRO_GAELCO_RUN_HPP

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "odin/host/app/games.hpp"
#include "odin/machine/boards/atvtrack/atvtrack.hpp"
#include "odin/machine/boards/hikaru/hikaru_host.hpp"

namespace odin {

class GaelcoRun {
public:
	/* `kept`: the file the board keeps its settings in (kept.bin of the
	 * game's saves), read now and written by power_off; `volume_frames`:
	 * Smashing Drive's volume set at power-on (VOLUME_UP_FRAMES of a
	 * START_VOLUMES), none for the others */
	GaelcoRun(const Game &game, const std::string &kept, std::optional<unsigned> volume_frames);

	/* The frames VOLUME + is held after VOLUME - for each of START_VOLUMES */
	static unsigned volume_up_frames(unsigned start_volume);

	/* One frame with these controls, drawn at that look (AtvTrack::set_look) */
	void run_frame(const CabinetInputs &in, unsigned fine, unsigned flat_mode, bool card_sorts);
	/* The scene of the last frame, if it drew one */
	PmxScene take_scene() { return m_board->take_scene(); }
	bool blanked() const { return m_board->dac_blanked(); }

	unsigned frame() const { return m_board->frame(); }
	/* Left and right, AtvTrack::DAC_RATE pairs a second; the woofer's pairs */
	const std::vector<int16_t> &sound() const { return m_board->sound(); }
	const std::vector<int16_t> &woofer() const { return m_board->woofer(); }
	/* The outputs as JVS counts them, and how hard each drives (Board) */
	std::span<const uint8_t> io_outputs() const { return m_outputs; }
	std::span<const uint8_t> output_levels() const { return m_levels; }
	/* The wheel motor's torque, as a drive board's (none without one) */
	std::optional<int> drive_torque() const;

	bool save_state(const std::string &path) { return m_board->save_state(path); }
	bool load_state(const std::string &path) { return m_board->load_state(path); }
	/* Its settings written to the kept file */
	void power_off();

private:
	struct Kick {
		float x = 0.0f, forward = 0.0f, back = 0.0f;
		unsigned rise = 0, frames = 0, rested = 0;
		bool spent = false;
		AtvTrack::BallSensors::value_type sensors = AtvTrack::BALL_AT_REST;
	};

	void read_controls(const CabinetInputs &in);
	void read_kick(const CabinetInputs &in, unsigned player);

	std::string m_kept;
	std::unique_ptr<AtvTrack> m_board;
	std::vector<GameControl> m_wiring;
	std::vector<PortSwitch> m_grounded;
	std::vector<unsigned> m_grounded_analog;
	std::array<std::optional<std::array<uint8_t, 2>>, 2> m_balls;
	unsigned m_volume_down, m_volume_up;
	unsigned m_coin = 0;
	Kick m_kick[2];
	std::array<uint8_t, 3> m_outputs{};
	std::array<uint8_t, 24> m_levels{};
	bool m_wheel_motor;
};

} /* namespace odin */

#endif
