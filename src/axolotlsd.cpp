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
//   AxolotlSD for C++ source code
#include "../include/axolotlsd.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <forward_list>
#include <numbers>
#include <stdexcept>

using namespace axolotlsd;

constexpr static U32 MAGIC = 0x41585344; // "AXSD"
constexpr static U16 CURRENT_VERSION = 0x0003;
constexpr static F32 A440 = 440.0f;

const static std::map<command_type, size_t> byte_sizes{
    {command_type::note_on, sizeof(song_tick_t) + (sizeof(U8) * 3)},
    {command_type::note_off, sizeof(song_tick_t) + (sizeof(U8) * 1)},
    {command_type::pitchwheel,
     sizeof(song_tick_t) + (sizeof(U8) * 1) + sizeof(U32)},
    {command_type::program_change, sizeof(song_tick_t) + (sizeof(U8) * 2)},

    {command_type::patch_data,
     sizeof(U8) + sizeof(U32) + sizeof(U32) + sizeof(U32) + (sizeof(F32) * 3)},
    {command_type::drum_data, sizeof(U8) + sizeof(U32) + (sizeof(F32) * 3)},

    {command_type::version, sizeof(U16)},
    {command_type::rate, sizeof(U32)},
    {command_type::end_of_track, sizeof(song_tick_t)}};

static F32 calculate_12tet(U8 note, F32 bend) {
  return std::pow(2.0f, (note - 69.0f + bend) / 12.0f) * A440;
}

static F32 calculate_mix(F32 x, F32 y, F32 a) {
  return (x * (1.0f - a)) + (y * a);
}

player::player(U32 count, U32 freq, bool stereo)
    : frequency{1.0f / freq}, in_stereo{stereo}, max_voices{count} {}

void player::play(song &&next, std::optional<environment> &&env) {

  std::swap(env_params, env);
  std::swap(current, next);

  for (auto i = 0; i < 16; i++) {
    switch (i) {
    case 9: {
      channels[9].reset(new drum_group);
      break;
    }
    default: {
      channels[i].reset(new voice_group);
      break;
    }
    }
  }

  std::for_each(patch_ids.begin(), patch_ids.end(),
                [](auto &&p) { p = std::nullopt; });

  seconds_elapsed = 0.0f;
  seconds_end = current.ticks_end / static_cast<F32>(current.ticks_per_second);

  if (current.version != CURRENT_VERSION) {
    throw std::runtime_error{"Version mismatch in wanted song"};
  }

  on_voices = 0;
  cursor = 0;
  echo_cursor = 0;
  last_cursor = std::nullopt;
  playback = true;
}

void voice_group::accumulate_into(const patch_t &patch, F32 &l, F32 &r) {
  std::for_each(voices.begin(), voices.end(), [&patch, &l, &r](auto &&v) {
    auto sample = 0.0f;
    auto here = static_cast<U32>(std::floor(patch.ratio * v.phase));
    const auto can_loop = patch.loop_start != 0xFFFFFFFF;

    if (can_loop && (here > patch.loop_end)) {
      if (v.key) {
        here -= patch.loop_start;
        here %= patch.loop_end - patch.loop_start;
        here += patch.loop_start;
      }
    }
    if (here >= patch.waveform.size()) {
      v.active = false;
    } else {
      sample = (static_cast<F32>(patch.waveform.at(here)) - 128.0f) / 128.0f;
    }
    v.phase += v.phase_add_by;

    l += sample * v.velocity * patch.gain_L;
    r += sample * v.velocity * patch.gain_R;
  });
}

void drum_group::accumulate_into(const drum_map_t &mapping, F32 &l, F32 &r) {
  std::for_each(voices.begin(), voices.end(), [&mapping, &l, &r](auto &&d) {
    auto sample = 0.0f;
    auto &&map_found = mapping.find(d.note);
    auto gain_L = 0.0f;
		auto gain_R = 0.0f;

    if (map_found != mapping.end()) {
      auto &&[_, patch] = *map_found;
      auto here = static_cast<U32>(patch.ratio * d.phase);
      if (here >= patch.waveform.size()) {
        d.active = false;
      } else {
        sample = (static_cast<F32>(patch.waveform.at(here)) - 128.0f) / 128.0f;
      }
      gain_L = patch.gain_L;
      gain_R = patch.gain_R;
      d.phase += d.phase_add_by;
    } else {
      d.active = false;
    }

    l += sample * d.velocity * gain_L;
    r += sample * d.velocity * gain_R;
  });
}

void player::handle_one(F32 &l, F32 &r) {
  cursor = static_cast<U32>(current.ticks_per_second * seconds_elapsed);
  if ((!last_cursor.has_value()) || (cursor > last_cursor.value())) {
    auto [begin, end] = current.commands.equal_range(cursor);
    std::for_each(begin, end, [this](auto &&epair) {
      auto &&[_, e] = epair;
      switch (e->get_type()) {
      case command_type::note_on: {
        if (on_voices < max_voices) {
          auto &&ptr = static_cast<command_note_on *>(e.get());
          auto &&ch = channels[ptr->channel];
          if (ch->is_drum_kit()) {
            auto &&ch_casted = static_cast<drum_group *>(ch.get());
            auto phase = A440 * frequency * 32.0f * std::numbers::pi;
            ch_casted->voices.emplace_back(ptr->velocity / 127.0f, phase,
                                           ptr->note);
          } else {
            auto &&ch_casted = static_cast<voice_group *>(ch.get());
            auto phase = calculate_12tet(ptr->note, ch_casted->bend) *
                         frequency * 100.0f;
            ch_casted->voices.emplace_back(ptr->velocity / 127.0f, phase,
                                           ptr->note);
          }
        }
        break;
      }
      case command_type::note_off: {
        auto &&ptr = static_cast<command_note_off *>(e.get());
        auto &&voices = channels[ptr->channel]->voices;
        if (!voices.empty()) {
          auto &&first_on = std::find_if(voices.begin(), voices.end(),
                                         [](auto &&v) { return v.key; });
          if (first_on != voices.end()) {
            (*first_on).key = false;
          }
        }
        break;
      }
      case command_type::pitchwheel: {
        auto &&ptr = static_cast<command_pitchwheel *>(e.get());
        auto &&ch = channels[ptr->channel];
        if (!ch->is_drum_kit()) {
          auto &&ch_casted = static_cast<voice_group *>(ch.get());
          ch_casted->bend = ptr->bend / 4096.0f;
          std::for_each(ch_casted->voices.begin(), ch_casted->voices.end(),
                        [this, &ch_casted](auto &&c) {
                          c.phase_add_by =
                              calculate_12tet(c.note, ch_casted->bend) *
                              frequency * 100.0f;
                        });
        }
        break;
      }
      case command_type::program_change: {
        auto &&ptr = static_cast<command_program_change *>(e.get());
        patch_ids[ptr->channel] = ptr->program;
        break;
      }
      default: {
        break;
      }
      }
    });
    last_cursor = cursor;
  }
  on_voices = 0;
  for (auto i = 0; i < 16; i++) {
    auto &&ch_ptr = channels.at(i);
    std::erase_if(ch_ptr->voices, [](auto &&v) { return !v.active; });
    if (ch_ptr->is_drum_kit()) {
      auto &&channel = static_cast<drum_group *>(ch_ptr.get());
      channel->accumulate_into(current.drums, l, r);
      on_voices += channel->voices.size();
    } else {
      auto &&channel = static_cast<voice_group *>(ch_ptr.get());
      if (patch_ids.at(i).has_value()) {
        const auto &patch = current.patches.at(*(patch_ids.at(i)));
        channel->accumulate_into(patch, l, r);
        on_voices += channel->voices.size();
      }
    }
  }
}

void player::maybe_echo_one(F32 &l, F32 &r) {
  if (env_params.has_value()) {
    auto &&env = env_params.value();
    echo_buffer_L[echo_cursor] += l;
    echo_buffer_R[echo_cursor] += r;
    echo_buffer_L[echo_cursor] *= env.feedback_L;
    echo_buffer_R[echo_cursor] *= env.feedback_R;

    // protect against clipping
    echo_buffer_L[echo_cursor] =
        std::clamp(echo_buffer_L[echo_cursor], -1.0f, 1.0f);
    echo_buffer_R[echo_cursor] =
        std::clamp(echo_buffer_R[echo_cursor], -1.0f, 1.0f);

    l = calculate_mix(l, echo_buffer_L[echo_cursor], env.wet_L);
    r = calculate_mix(r, echo_buffer_R[echo_cursor], env.wet_R);
    echo_cursor += env.cursor_increment;
    echo_cursor %= env.cursor_max;
  }
}

void player::tick(std::vector<F32> &audio) {
  const auto size = audio.size();
  if (in_stereo) {
    // Stereo
    for (auto i = 0; i < size; i += 2) {
      auto l = 0.0f;
      auto r = 0.0f;

      if (playback) {
        handle_one(l, r);
        seconds_elapsed += frequency;
        if (seconds_elapsed > seconds_end) {
          seconds_elapsed = std::fmod(seconds_elapsed, seconds_end);
          last_cursor = std::nullopt;
        }
      }

      maybe_echo_one(l, r);
      audio[i + 0] = std::clamp(l, -1.0f, 1.0f);
      audio[i + 1] = std::clamp(r, -1.0f, 1.0f);
    }
  } else {
    // Mono
    for (auto i = 0; i < size; i += 1) {
      auto l = 0.0f;
      auto r = 0.0f;

      if (playback) {
        handle_one(l, r);
        seconds_elapsed += frequency;
        if (seconds_elapsed > seconds_end) {
          seconds_elapsed = std::fmod(seconds_elapsed, seconds_end);
          last_cursor = std::nullopt;
        }
      }

      maybe_echo_one(l, r);
      audio[i] = std::clamp((l + r) / 2.0f, -1.0f, 1.0f);
    }
  }
}

// This convenience loads an "xxd -i" format song dump
song song::load_xxd_format(unsigned char *data, unsigned int len) {
	auto vec = std::vector<U8>{};
	vec.resize(len);
	for(auto i = 0; i < len; i++) {
		vec[i] = data[i];
	}
	return song::load(vec);
}

song song::load(std::vector<U8> &data) {
  auto where = 4;
  auto end = data.size();
  auto &&the_song = song{};
  auto continue_for = 0;
  auto continue_data = std::forward_list<U8>{};

  auto magic_data = std::vector<U32>{data[0], data[1], data[2], data[3]};
  auto magic_concat = (magic_data[3] << 0) | (magic_data[2] << 8) |
                      (magic_data[1] << 16) | (magic_data[0] << 24);

  if (magic_concat != MAGIC) {
    throw std::runtime_error{"First 4 bytes of this song are not 'AXSD'!"};
  }

  auto data_byte = 0;

  while (where < end) {
    data_byte = data.at(where);

    auto what_value = static_cast<command_type>(data_byte);
    continue_for = byte_sizes.at(what_value);

    while (continue_for > 0) {
      where++;
      data_byte = data.at(where);
      continue_data.emplace_front(data_byte);
      continue_for--;
    }
    continue_data.reverse();

    switch (what_value) {
    case command_type::drum_data: {
      // initialize pointer
      auto &&command_ptr = new command_drum_data{};

      // initialize bytes
      auto drum = continue_data.front();
      continue_data.pop_front();

      // get sample size
      auto width = std::vector<U32>{0, 0, 0, 0};
      std::for_each(width.begin(), width.end(), [&continue_data](auto &&w) {
        w = continue_data.front();
        continue_data.pop_front();
      });
      auto width_calc = (width[0] << 0) | (width[1] << 8) | (width[2] << 16) |
                        (width[3] << 24);

      // get ratio, conscientious of the floating point nature
      auto ratio_castee = std::vector<U32>{0, 0, 0, 0};
      std::for_each(ratio_castee.begin(), ratio_castee.end(),
                    [&continue_data](auto &&r) {
                      r = continue_data.front();
                      continue_data.pop_front();
                    });
      auto ratio_calc =
          std::bit_cast<F32>((ratio_castee[0] << 0) | (ratio_castee[1] << 8) |
                             (ratio_castee[2] << 16) | (ratio_castee[3] << 24));

      // get gain, conscientious of the floating point nature
      auto gainL_castee = std::vector<U32>{0, 0, 0, 0};
      std::for_each(gainL_castee.begin(), gainL_castee.end(),
                    [&continue_data](auto &&g) {
                      g = continue_data.front();
                      continue_data.pop_front();
                    });
      auto gainL_calc =
          std::bit_cast<F32>((gainL_castee[0] << 0) | (gainL_castee[1] << 8) |
                             (gainL_castee[2] << 16) | (gainL_castee[3] << 24));
      auto gainR_castee = std::vector<U32>{0, 0, 0, 0};
      std::for_each(gainR_castee.begin(), gainR_castee.end(),
                    [&continue_data](auto &&g) {
                      g = continue_data.front();
                      continue_data.pop_front();
                    });
      auto gainR_calc =
          std::bit_cast<F32>((gainR_castee[0] << 0) | (gainR_castee[1] << 8) |
                             (gainR_castee[2] << 16) | (gainR_castee[3] << 24));
      auto drum_data = drum_t{};
      drum_data.waveform.resize(width_calc);
      drum_data.ratio = ratio_calc;
      drum_data.gain_L = gainL_calc;
      drum_data.gain_R = gainR_calc;

      // sample is loaded here
      std::for_each(drum_data.waveform.begin(), drum_data.waveform.end(),
                    [&data, &where](auto &&b) { b = data.at(++where); });
      the_song.drums.insert({drum, drum_data});

      // dispatch pointer
      the_song.commands.emplace(0, command_ptr);
      break;
    }
    case command_type::patch_data: {
      // initialize pointer
      auto &&command_ptr = new command_patch_data{};

      // initialize bytes
      auto patch = continue_data.front();
      continue_data.pop_front();

      // get sample size
      auto width = std::vector<U32>{0, 0, 0, 0};
      std::for_each(width.begin(), width.end(), [&continue_data](auto &&w) {
        w = continue_data.front();
        continue_data.pop_front();
      });
      auto width_calc = (width[0] << 0) | (width[1] << 8) | (width[2] << 16) |
                        (width[3] << 24);

      // get start point (if 0xFFFFFFFF we aren't looping)
      auto start = std::vector<U32>{0, 0, 0, 0};
      std::for_each(start.begin(), start.end(), [&continue_data](auto &&s) {
        s = continue_data.front();
        continue_data.pop_front();
      });
      auto start_calc = (start[0] << 0) | (start[1] << 8) | (start[2] << 16) |
                        (start[3] << 24);

      // get end point
      auto end = std::vector<U32>{0, 0, 0, 0};
      std::for_each(end.begin(), end.end(), [&continue_data](auto &&e) {
        e = continue_data.front();
        continue_data.pop_front();
      });
      auto end_calc =
          (end[0] << 0) | (end[1] << 8) | (end[2] << 16) | (end[3] << 24);

      // get ratio, conscientious of the floating point nature
      auto ratio_castee = std::vector<U32>{0, 0, 0, 0};
      std::for_each(ratio_castee.begin(), ratio_castee.end(),
                    [&continue_data](auto &&r) {
                      r = continue_data.front();
                      continue_data.pop_front();
                    });
      auto ratio_calc =
          std::bit_cast<F32>((ratio_castee[0] << 0) | (ratio_castee[1] << 8) |
                             (ratio_castee[2] << 16) | (ratio_castee[3] << 24));

      // get gain, conscientious of the floating point nature
      auto gainL_castee = std::vector<U32>{0, 0, 0, 0};
      std::for_each(gainL_castee.begin(), gainL_castee.end(),
                    [&continue_data](auto &&g) {
                      g = continue_data.front();
                      continue_data.pop_front();
                    });
      auto gainL_calc =
          std::bit_cast<F32>((gainL_castee[0] << 0) | (gainL_castee[1] << 8) |
                             (gainL_castee[2] << 16) | (gainL_castee[3] << 24));
      auto gainR_castee = std::vector<U32>{0, 0, 0, 0};
      std::for_each(gainR_castee.begin(), gainR_castee.end(),
                    [&continue_data](auto &&g) {
                      g = continue_data.front();
                      continue_data.pop_front();
                    });
      auto gainR_calc =
          std::bit_cast<F32>((gainR_castee[0] << 0) | (gainR_castee[1] << 8) |
                             (gainR_castee[2] << 16) | (gainR_castee[3] << 24));
      auto patch_data = patch_t{};
      patch_data.waveform.resize(width_calc);
      patch_data.loop_start = start_calc;
      patch_data.loop_end = end_calc;
      patch_data.ratio = ratio_calc;
      patch_data.gain_L = gainL_calc;
      patch_data.gain_R = gainR_calc;

      // sample is loaded here
      std::for_each(patch_data.waveform.begin(), patch_data.waveform.end(),
                    [&data, &where](auto &&b) { b = data.at(++where); });
      the_song.patches.insert({patch, patch_data});

      // dispatch pointer
      the_song.commands.emplace(0, command_ptr);
      break;
    }

    case command_type::note_on: {
      // initialize pointer
      auto &&command_ptr = new command_note_on{};

      // initialize time
      auto time = std::vector<song_tick_t>{0, 0, 0, 0};
      std::for_each(time.begin(), time.end(), [&continue_data](auto &&t) {
        t = continue_data.front();
        continue_data.pop_front();
      });

      // initialize bytes
      command_ptr->channel = continue_data.front();
      continue_data.pop_front();
      command_ptr->note = continue_data.front();
      continue_data.pop_front();
      command_ptr->velocity = continue_data.front();
      continue_data.pop_front();

      // dispatch pointer
      the_song.commands.emplace((time[0] << 0) | (time[1] << 8) |
                                    (time[2] << 16) | (time[3] << 24),
                                command_ptr);
      break;
    }
    case command_type::note_off: {
      // initialize pointer
      auto &&command_ptr = new command_note_off{};

      // initialize time
      auto &&time = std::vector<song_tick_t>{0, 0, 0, 0};
      std::for_each(time.begin(), time.end(), [&continue_data](auto &&t) {
        t = continue_data.front();
        continue_data.pop_front();
      });

      // initialize bytes
      command_ptr->channel = continue_data.front();
      continue_data.pop_front();

      // dispatch pointer
      the_song.commands.emplace((time[0] << 0) | (time[1] << 8) |
                                    (time[2] << 16) | (time[3] << 24),
                                command_ptr);
      break;
    }
    case command_type::pitchwheel: {
      // initialize pointer
      auto &&command_ptr = new command_pitchwheel{};

      // initialize time
      auto &&time = std::vector<song_tick_t>{0, 0, 0, 0};
      std::for_each(time.begin(), time.end(), [&continue_data](auto &&t) {
        t = continue_data.front();
        continue_data.pop_front();
      });

      // initialize bytes
      command_ptr->channel = continue_data.front();
      continue_data.pop_front();

      // initialize bend
      auto &&bend_vec = std::vector<U32>{0, 0, 0, 0};
      std::for_each(bend_vec.begin(), bend_vec.end(),
                    [&continue_data](auto &&b) {
                      b = continue_data.front();
                      continue_data.pop_front();
                    });
      auto bend = (bend_vec[0] << 0) | (bend_vec[1] << 8) |
                  (bend_vec[2] << 16) | (bend_vec[3] << 24);
      // bit cast to signed
      command_ptr->bend = std::bit_cast<S32>(bend);

      // dispatch pointer
      the_song.commands.emplace((time[0] << 0) | (time[1] << 8) |
                                    (time[2] << 16) | (time[3] << 24),
                                command_ptr);
      break;
    }
    case command_type::program_change: {
      // initialize pointer
      auto &&command_ptr = new command_program_change{};

      // initialize time
      auto &&time = std::vector<song_tick_t>{0, 0, 0, 0};
      std::for_each(time.begin(), time.end(), [&continue_data](auto &&t) {
        t = continue_data.front();
        continue_data.pop_front();
      });

      // initialize bytes
      command_ptr->channel = continue_data.front();
      continue_data.pop_front();
      command_ptr->program = continue_data.front();
      continue_data.pop_front();

      // dispatch pointer
      the_song.commands.emplace((time[0] << 0) | (time[1] << 8) |
                                    (time[2] << 16) | (time[3] << 24),
                                command_ptr);
      break;
    }
    case command_type::version: {
      // initialize pointer
      auto &&command_ptr = new command_version{};

      // initialize version
      auto &&ver = std::vector<U16>{0, 0};
      std::for_each(ver.begin(), ver.end(), [&continue_data](auto &&v) {
        v = continue_data.front();
        continue_data.pop_front();
      });
      command_ptr->song_version = (ver[0] << 0) | (ver[1] << 8);

      the_song.version = command_ptr->song_version;

      // dispatch pointer
      the_song.commands.emplace(0, command_ptr);
      break;
    }
    case command_type::rate: {
      // initialize pointer
      auto &&command_ptr = new command_rate{};

      // initialize version
      auto &&rate = std::vector<U32>{0, 0, 0, 0};
      std::for_each(rate.begin(), rate.end(), [&continue_data](auto &&r) {
        r = continue_data.front();
        continue_data.pop_front();
      });
      command_ptr->song_rate =
          (rate[0] << 0) | (rate[1] << 8) | (rate[2] << 16) | (rate[3] << 24);

      the_song.ticks_per_second = command_ptr->song_rate;

      // dispatch pointer
      the_song.commands.emplace(0, command_ptr);
      break;
    }
    case command_type::end_of_track: {
      // initialize pointer
      auto &&command_ptr = new command_end_of_track{};

      // initialize version
      auto &&time = std::vector<song_tick_t>{0, 0, 0, 0};
      std::for_each(time.begin(), time.end(), [&continue_data](auto &&e) {
        e = continue_data.front();
        continue_data.pop_front();
      });

      // dispatch pointer
      the_song.commands.emplace(the_song.ticks_end, command_ptr);

      the_song.ticks_end =
          (time[0] << 0) | (time[1] << 8) | (time[2] << 16) | (time[3] << 24);
      break;
    }
    }
    where++;
  }

  return the_song;
}
