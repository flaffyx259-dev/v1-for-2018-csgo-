#pragma once
#include "../../base/sdk.h"
#include "../../base/global_context.h"

class c_game_trace;
class ray_t;

struct anti_aim_angles_t;

class c_anti_aim
{
private:
  bool fake_ducking{ };

  bool flip_side{ };
  bool flip_jitter{ };
  bool flip_move{ };
  bool flip_fake{ };

  int fake_side{ };
  float fake_angle{ };
  float best_dist{ };

  // real/fake tracking (fixed: were never updated before)
  float last_real_angle{ 0.f };
  bool has_last_real{ false };
  float last_fake_angle{ 0.f };

  // lby breaker state (2018 style, getze/fatality + jre proxy tracking)
  float next_lby_update{ 0.f };
  float last_lby_angle{ 0.f };
  bool will_lby_update{ false };
  float spin_angle{ 0.f };
  float random_jitter_angle{ 0.f };

  // self-tracking: last observed server lby, resyncs the predictor on real change
  float prev_lby_value{ 0.f };
  bool has_prev_lby{ false };

  // distortion spin randomization (fixes dead random_speed checkbox)
  float spin_rand_speed{ 25.f };
  float last_spin_rand{ -10.f };

  // freestanding cache (anti-spin hysteresis)
  float freestanding_yaw{ 0.f };
  int freestanding_side{ 0 }; // -1 left, 1 right, 0 back/none
  float freestanding_time{ -1000.f };
  int freestanding_target{ -1 };

  int aa_shot_cmd{ };

  std::vector< int > hitbox_list = { hitbox_head, hitbox_chest, hitbox_stomach, hitbox_pelvis };

  void fake_duck( );
  void slow_walk( );
  void fake( );
  void manual_yaw( float& yaw, bool& overridden );

  bool get_freestanding_yaw( float& out_yaw );
  void automatic_edge( float& yaw );
  void at_targets( float& base );

  void update_lby_predictor( );
  float get_lby_flick_delta( bool update_tick, float real_yaw );
  float get_fake_delta( );

public:
  int get_ticks_to_stop( );

  anti_aim_angles_t* get_config( );

  c_csplayer* get_closest_player( bool skip = false, bool local_distance = false );
  bool is_peeking( );
  bool is_fake_ducking( );

  void on_pre_predict( );
  void on_predict_start( );
  void on_predict_end( );
};