// ============================================================================
//   Copyright 2023 Roland Metivier <metivier.roland@chlorophyt.us>
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
// ============================================================================
//   AxolotlSD for C++ public header
#pragma once
#include "axolotlsd_configuration.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace axolotlsd {
using U8 = std::uint8_t;
using U16 = std::uint16_t;
using U32 = std::uint32_t;
using U64 = std::uint64_t;

using S8 = std::int8_t;
using S16 = std::int16_t;
using S32 = std::int32_t;
using S64 = std::int64_t;

using F32 = float;
using F64 = double;

using audio_data_t = F32;
using song_tick_t = U32;
using patch_data_t = std::vector<U8>;
// ============================================================================
enum class command_type : U8 {
  // regular
  note_on = 0x01,
  note_off = 0x02,
  pitchwheel = 0x03,
  program_change = 0x04,
  // patches
  patch_data = 0x80,
  drum_data = 0x81,
  // meta
  version = 0xFC,
  rate = 0xFD,
  end_of_track = 0xFE,
};
struct command {
  virtual command_type get_type() = 0;
};
struct command_note_on : command {
  virtual command_type get_type() { return command_type::note_on; }
  U8 channel;
  U8 note;
  U8 velocity;
};
struct command_note_off : command {
  virtual command_type get_type() { return command_type::note_off; }
  U8 channel;
};
struct command_pitchwheel : command {
  virtual command_type get_type() { return command_type::pitchwheel; }
  U8 channel;
  S32 bend;
};
struct command_program_change : command {
  virtual command_type get_type() { return command_type::program_change; }
  U8 channel;
  U8 program;
};
struct command_patch_data : command {
  virtual command_type get_type() { return command_type::patch_data; }
};
struct command_drum_data : command {
  virtual command_type get_type() { return command_type::drum_data; }
};
struct command_version : command {
  virtual command_type get_type() { return command_type::version; }
  U16 song_version;
};
struct command_rate : command {
  virtual command_type get_type() { return command_type::rate; }
  U32 song_rate;
};
struct command_end_of_track : command {
  virtual command_type get_type() { return command_type::end_of_track; }
};
// ============================================================================
struct patch_base_t {
  virtual bool is_drum() = 0;
  patch_data_t waveform{};
  F32 ratio;
  F32 gain_L;
  F32 gain_R;
};
struct patch_t : patch_base_t {
  U32 loop_start;
  U32 loop_end;

  virtual bool is_drum() { return false; }
};
struct drum_t : patch_base_t {
  virtual bool is_drum() { return true; }
};
using drum_map_t = std::map<U8, drum_t>;
// ============================================================================
struct voice_single {
  F32 velocity;
  F32 phase_add_by;
  U8 note;
  F32 phase = 0.0f;
  bool key = true;
  bool active = true;
};
struct voice_group_base {
  std::vector<voice_single> voices{};
  virtual bool is_drum_kit() = 0;
};
struct voice_group : voice_group_base {
  F32 bend = 0.0f;
  void accumulate_into(const patch_t &, F32 &, F32 &);
  virtual bool is_drum_kit() { return false; }
};
struct drum_group : voice_group_base {
  void accumulate_into(const drum_map_t &, F32 &, F32 &);
  virtual bool is_drum_kit() { return true; }
};
// ============================================================================
struct song {
  U16 version;
  song_tick_t ticks_end;
  song_tick_t ticks_per_second;

  std::multimap<song_tick_t, std::unique_ptr<command>> commands{};
  std::map<U8, patch_t> patches{};
  drum_map_t drums{};

  static song load(std::vector<U8> &);
  static song load_xxd_format(unsigned char *, unsigned int);
};
struct environment {
  F32 feedback_L;
  F32 feedback_R;
  F32 wet_L;
  F32 wet_R;
  U16 cursor_increment;
  U16 cursor_max;
};
struct player {
  F32 seconds_elapsed = 0.0f;
  F32 seconds_end;
  F32 frequency;
  U32 max_voices;
  U32 on_voices = 0;

  F32 echo_buffer_L[65535]{0.0f};
  F32 echo_buffer_R[65535]{0.0f};
  U16 echo_cursor = 0;
  std::optional<environment> env_params = std::nullopt;

  U32 cursor = 0;
  std::optional<U32> last_cursor = std::nullopt;

  song current;
  bool in_stereo;

  explicit player(U32, U32, bool);

  std::array<std::unique_ptr<voice_group_base>, 16> channels{};
  std::array<std::optional<U8>, 16> patch_ids{std::nullopt};

  bool playback = false;

  void play(song &&, std::optional<environment> &&);
  void pause();
  void tick(std::vector<F32> &);
  void handle_one(F32 &, F32 &);
  void maybe_echo_one(F32 &, F32 &);
};
} // namespace axolotlsd
