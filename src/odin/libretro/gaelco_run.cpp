/*
 * Odin libretro core: a Gaelco board run a frame at a time (gaelco_run.hpp).
 * What it does with the controls, the outputs and the settings at power-on
 * is the desktop's AtvGame and play_atvtrack (host/app/atvtrack_play.cpp),
 * without its window, gamepads and link cable.
 *
 * Copyright (C) 2026 Réda Chérif-Touil
 * Copyright (C) 2026 Daniel Martin
 *
 * Odin is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include "gaelco_run.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "odin/machine/cabinet/drive_board.hpp"

namespace odin {

namespace {

/* The operator panel, on the same pins of every board: SERVICE, TEST, the
 * one coin entry, VOLUME + and VOLUME - */
constexpr PortSwitch SERVICE_PIN{ 0, 1 }, TEST_PIN{ 0, 3 }, COIN_PIN{ 0, 5 }, VOLUME_UP_PIN{ 0, 0 }, VOLUME_DOWN_PIN{ 0, 2 };
/* SERVICE of player 1 in CabinetInputs */
constexpr uint16_t SERVICE = 0x4000;
constexpr unsigned COIN_FRAMES = 6;
/* Smashing Drive's volume at power-on, as the desktop sets it: the
 * frames VOLUME + is held for each of its START_VOLUMES (0, 25, 50, 75, 96;
 * host/app/options.hpp, which the core does not take: it needs SDL) */
constexpr unsigned VOLUME_UP_FRAMES[] = { 0, 96, 122, 147, 180 };
constexpr unsigned VOLUME_FROM = 60, VOLUME_DOWN_FRAMES = 200, VOLUME_GAP = 10;
/* Gaelco Football's kick on the right stick, as the desktop's */
constexpr unsigned KICK_FRAMES = 3, KICK_RISE = 3, BACKSWING_FRAMES = 20;
constexpr float KICK_FROM = 0.3f, KICK_REST = 0.2f;
constexpr float KICK_SOFT = 10.0f, KICK_HARD = 127.0f, KICK_GAIN = 1.2f;

std::unique_ptr<AtvTrack>
make_board(const Game &game)
{
	if (game.machine == "smashdrv") {
		const std::span<const uint8_t> program = game.section("program");

		return std::make_unique<Ref010131>(AtvTrack::rom_of(game.section("sdra"), game.section("sdrb"), game.section("sdrc")),
		                                  std::vector<uint8_t>(program.begin(), program.end()));
	}
	std::array<std::vector<uint8_t>, NandBank::CHIPS> nand;

	for (unsigned c = 0; c < NandBank::CHIPS; c++) {
		const std::span<const uint8_t> chip = game.section("nand" + std::to_string(c));

		nand[c].assign(chip.begin(), chip.end());
	}
	return std::make_unique<Ref020419>(std::move(nand), game.section("fpga"));
}

uint8_t
adc_value(const std::array<uint8_t, 2> &span, uint16_t channel)
{
	return static_cast<uint8_t>(std::lround(span[0] + (span[1] - span[0]) * (channel / 65535.0)));
}

} /* namespace */

unsigned
GaelcoRun::volume_up_frames(unsigned start_volume)
{
	return VOLUME_UP_FRAMES[std::min<size_t>(start_volume, std::size(VOLUME_UP_FRAMES) - 1)];
}

GaelcoRun::GaelcoRun(const Game &game, const std::string &kept, std::optional<unsigned> volume_frames)
	: m_kept(kept), m_board(make_board(game)), m_wiring(game.controls), m_grounded(game.grounded_pins),
	  m_grounded_analog(game.grounded_analog), m_volume_down(volume_frames ? VOLUME_DOWN_FRAMES : 0),
	  m_volume_up(volume_frames.value_or(0)), m_wheel_motor(game.wheel_motor)
{
	for (const GameControl &control : m_wiring)
		if (control.ball != GameControl::Ball::NONE) {
			std::optional<std::array<uint8_t, 2>> &stick = m_balls[control.player & 1];

			if (!stick)
				stick.emplace();
			(*stick)[control.ball == GameControl::Ball::FORWARD] = control.analog;
		}
	m_board->set_name(game.name);
	m_board->set_verbose(false);
	m_board->set_motion_platform(game.motion_platform);
	if (!m_kept.empty())
		m_board->load_kept(m_kept);
	m_board->reset();

	/* No link cable: a game that keeps a Link Mode plays alone (NO LINK),
	 * as the desktop sets it without the cable */
	if (game.link.seat_in == Game::Link::In::FLASH)
		m_board->set_flash_setting(game.link.seat_byte, game.link.seat_shift, game.link.seat_bits, 0);
	else if (game.link.seat_in == Game::Link::In::NAND)
		m_board->set_nand_setting(game.link.seat_byte, game.link.seat_shift, game.link.seat_bits, 0);
	/* The standard cabinet: a setting another cabinet asks for back to the
	 * standard one's, only from that cabinet's own */
	for (const Game::Cabinet &model : game.cabinets) {
		if (!model.nand_setting)
			continue;

		const Game::Cabinet::Setting &setting = *model.nand_setting;
		const bool played = game.cabinet == model.id;

		m_board->set_nand_setting(setting.byte, 0, 8, played ? setting.value : setting.standard,
		                          played ? std::nullopt : std::optional<uint8_t>(setting.value));
	}
}

void
GaelcoRun::power_off()
{
	if (!m_kept.empty())
		m_board->save_kept(m_kept);
}

std::optional<int>
GaelcoRun::drive_torque() const
{
	if (!m_wheel_motor)
		return std::nullopt;

	const auto &motors = m_board->motors();

	return (static_cast<int>(motors[0]) - static_cast<int>(motors[1])) * DriveBoard::MOST / 255;
}

void
GaelcoRun::read_kick(const CabinetInputs &in, unsigned player)
{
	Kick &kick = m_kick[player];
	const auto axis = [&](unsigned half) {
		return (static_cast<float>(in.jvs_analog[(*m_balls[player])[half] & 7]) - 32768.0f) / 32768.0f;
	};
	const float x = axis(0), forward = -axis(1), far = std::hypot(x, forward);

	kick.back = std::max(kick.back, -forward);
	kick.rested = far < KICK_REST ? kick.rested + 1 : 0;
	if (kick.rested >= BACKSWING_FRAMES)
		kick.back = 0.0f;

	if (kick.frames > 0) {
		if (--kick.frames == 0)
			kick.sensors = AtvTrack::BALL_AT_REST;
	} else if (kick.spent) {
		kick.spent = forward > KICK_REST;
	} else if (forward > KICK_FROM) {
		if (forward >= kick.forward) {
			kick.forward = forward;
			kick.x = x;
		}
		if (++kick.rise >= KICK_RISE) {
			const float travel = std::clamp((std::hypot(kick.x, kick.forward) - KICK_FROM) / (1.0f - KICK_FROM), 0.0f, 1.0f);
			const float power = KICK_SOFT + (KICK_HARD - KICK_SOFT) * travel * travel * travel;
			const float force = power / KICK_GAIN / std::sqrt(2.0f * (1.0f + kick.x * kick.x));
			const auto side = [&](float turned) { return static_cast<uint8_t>(128.0f + force * (1.0f + turned)); };

			kick.sensors = { static_cast<uint8_t>(0x30 + static_cast<int>(kick.back * 200.0f)), side(-kick.x), side(kick.x) };
			kick.frames = KICK_FRAMES;
			kick.spent = true;
			kick.back = kick.forward = 0.0f;
			kick.rise = 0;
		}
	} else {
		kick.forward = 0.0f;
		kick.rise = 0;
	}
}

void
GaelcoRun::read_controls(const CabinetInputs &in)
{
	std::array<uint8_t, 4> switches{};
	std::array<uint8_t, 4> analog{ 0x80, 0x80, 0x80, 0x80 };
	const auto close = [&](PortSwitch pin) {
		switches[pin.group] = static_cast<uint8_t>(switches[pin.group] | 1u << pin.bit);
	};

	if (in.jvs_coin[0] || in.jvs_coin[1])
		m_coin = COIN_FRAMES;
	if (m_coin > 0) {
		close(COIN_PIN);
		m_coin--;
	}
	if (in.jvs_system & 0x80)
		close(TEST_PIN);
	if (in.jvs_player[0] & SERVICE)
		close(SERVICE_PIN);

	const unsigned up_from = VOLUME_FROM + m_volume_down + VOLUME_GAP;

	if (frame() >= VOLUME_FROM && frame() < VOLUME_FROM + m_volume_down)
		close(VOLUME_DOWN_PIN);
	else if (m_volume_down && frame() >= up_from && frame() < up_from + m_volume_up)
		close(VOLUME_UP_PIN);
	for (const GameControl &control : m_wiring) {
		if (control.port && in.jvs_player[control.button.player & 1] & control.button.mask)
			close(*control.port);
		if (control.span)
			analog[control.analog & 3] = adc_value(*control.span, in.jvs_analog[control.analog & 7]);
	}
	for (const PortSwitch pin : m_grounded)
		close(pin);
	for (const unsigned channel : m_grounded_analog)
		analog[channel & 3] = 0;
	if (m_balls[0] || m_balls[1]) {
		for (unsigned player = 0; player < 2; player++)
			if (m_balls[player])
				read_kick(in, player);
		m_board->set_ball_sensors({ m_kick[0].sensors, m_kick[1].sensors },
		                          { m_kick[0].frames > 0, m_kick[1].frames > 0 });
	}
	m_board->set_switches(switches);
	m_board->set_analog(analog);
}

void
GaelcoRun::run_frame(const CabinetInputs &in, unsigned fine, unsigned flat_mode, bool card_sorts)
{
	read_controls(in);
	m_board->set_look(fine, flat_mode, card_sorts);
	m_board->run_frame();
	m_outputs.fill(0);
	m_levels.fill(0);
	for (unsigned bit = 0; bit < 11; bit++)
		if (m_board->system_pins() & 1u << bit) {
			m_outputs[bit / 8] = static_cast<uint8_t>(m_outputs[bit / 8] | 0x80u >> (bit % 8));
			m_levels[bit] = 255;
		}
	for (unsigned channel = 0; channel < 4; channel++) {
		m_levels[16 + channel] = m_board->motors()[channel];
		if (m_levels[16 + channel])
			m_outputs[2] = static_cast<uint8_t>(m_outputs[2] | 0x80u >> channel);
	}
}

} /* namespace odin */
