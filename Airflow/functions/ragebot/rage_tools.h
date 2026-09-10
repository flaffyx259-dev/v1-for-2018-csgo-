#pragma once
#include <array>
#include <vector>
#include <algorithm>

#include "../../base/sdk/entity.h"
#include "../../base/global_context.h"

#include "../ragebot/animfix.h"
#include "../config_vars.h"

struct point_t;

namespace cheat_tools
{
  inline bool debug_hitchance = false;
  inline vector2d spread_point{ };
  inline float current_spread{ };
  inline std::vector< vector2d > spread_points{ };

  int get_legit_tab( c_basecombatweapon* temp_weapon = nullptr );
  skin_weapon_t get_skin_weapon_config( );
  weapon_config_t get_weapon_config( );

  std::string hitbox_to_string( int id );
  std::string hitgroup_to_string( int hitgroup );
  int hitbox_to_hitgroup( int hitbox );

  bool can_hit_hitbox( const vector3d& start, const vector3d& end, c_csplayer* player, int hitbox, records_t* record, matrix3x4_t* matrix = nullptr );
  bool is_accuracy_valid( c_csplayer* player, point_t& point, float amount, float* out_chance );

  std::vector< std::pair< vector3d, bool > > get_multipoints( c_csplayer* player, int hitbox, matrix3x4_t* matrix );
}

struct resolver_mode_id_t
{
  enum id_t : int
  {
    none = 0,
    moving,
    air,
    lby_flick,
    freestand,
    last_move,
    brute,
    shot,
    sideways,
    backward
  };
};

struct resolver_info_t
{
  bool valid{ };

  // current resolver mode:
  std::string mode{ };
  int mode_id{ resolver_mode_id_t::none };

  float last_moving_lby{ };
  float last_move_time{ -1000.f };
  float last_stand_angle{ };

  // bruteforce indices (per-player, advanced by missed shots)
  int stand_index{ };
  int stand_index2{ };
  int air_index{ };
  int body_index{ };

  // lby tracking (proxy-style, 2018 era)
  float body{ };
  float old_body{ };
  float body_update{ -1000.f };
  bool moved{ };
  bool has_walk{ };
  records_t walk_record{ };

  // enemy desync-flick detection (Kaaba SPECIFIC_DETECT homage): layer 6
  // playback-rate restart pattern across 3 records + constant tick frequency.
  // confirmed breaker -> stop forcing lby, bruteforce/freestand instead.
  int flick_count{ };
  int flick_freq{ };
  int flick_freq_prev{ };
  int last_flick_tick{ -10000 };
  bool desync_flicking{ };

  // last freestanding side, used for hysteresis (anti-spin)
  int freestanding_side{ }; // -1 left, 0 back/none, 1 right
  float freestanding_time{ -1000.f };

  struct
  {
    bool available = false;

    float left_fraction = 0.f;
    float right_fraction = 0.f;
    float back_fraction = 0.f;
    float yaw = 0.f;

    int left_damage = 0;
    int right_damage = 0;
    int back_damage = 0;

    __forceinline void reset( )
    {
      available = false;

      left_fraction = right_fraction = back_fraction = 0.f;
      yaw = 0.f;
      left_damage = right_damage = back_damage = 0;
    }
  } freestanding;

  struct
  {
    float lby_time{ };
    float old_lby{ };

    __forceinline void reset( )
    {
      lby_time = 0.f;
      old_lby = 0.f;
    }
  } lby;

  __forceinline void reset( )
  {
    valid = false;
    mode = "";
    mode_id = resolver_mode_id_t::none;

    last_moving_lby = 0.f;
    last_move_time = -1000.f;
    last_stand_angle = 0.f;

    stand_index = 0;
    stand_index2 = 0;
    air_index = 0;
    body_index = 0;

    body = 0.f;
    old_body = 0.f;
    body_update = -1000.f;
    moved = false;
    has_walk = false;
    walk_record.reset( );

    flick_count = 0;
    flick_freq = 0;
    flick_freq_prev = 0;
    last_flick_tick = -10000;
    desync_flicking = false;

    freestanding_side = 0;
    freestanding_time = -1000.f;

    freestanding.reset( );
    lby.reset( );
  }

  __forceinline void on_miss( )
  {
    // advance bruteforce indices; body flick gets 2 chances then falls to brute
    if( mode_id == resolver_mode_id_t::lby_flick )
    {
      body_index++;
      if( body_index >= 2 )
      {
        stand_index++;
        body_index = 0;
      }
    }
    else if( mode_id == resolver_mode_id_t::air )
      air_index++;
    else if( mode_id == resolver_mode_id_t::freestand || mode_id == resolver_mode_id_t::last_move || mode_id == resolver_mode_id_t::brute )
    {
      stand_index++;
      stand_index2++;
    }
    else
    {
      stand_index++;
      stand_index2++;
      air_index++;
    }
  }

  __forceinline void on_hit( )
  {
    // do not reset fully - keep lby tracking, only reset brute indices
    // (prevents one lucky hit from wiping learned desync side)
    stand_index = 0;
    stand_index2 = 0;
    air_index = 0;
    body_index = 0;
  }
};

namespace resolver
{
  inline std::array< resolver_info_t, 65 > info{ };

  __forceinline void reset_info( c_csplayer* player )
  {
    auto& i = info [ player->index( ) ];
    i.reset( );
  }

  __forceinline void on_resolver_miss( int idx )
  {
    if( idx > 0 && idx < 65 )
      info [ idx ].on_miss( );
  }

  __forceinline void on_resolver_hit( int idx )
  {
    if( idx > 0 && idx < 65 )
      info [ idx ].on_hit( );
  }

  float get_away_angle( c_csplayer* player, records_t* record );
  float get_backward( c_csplayer* player, records_t* record );
  bool is_yaw_sideways( c_csplayer* player, records_t* record, float yaw );
  bool is_yaw_backward( c_csplayer* player, records_t* record, float yaw );
  void anti_freestand( c_csplayer* player, records_t* record, float& out_yaw );
  void resolve_walk( c_csplayer* player, records_t* current, resolver_info_t& resolver_info, float backward );
  void resolve_stand( c_csplayer* player, records_t* current, resolver_info_t& resolver_info, float backward, int misses );
  void resolve_air( c_csplayer* player, records_t* current, resolver_info_t& resolver_info, float backward, int misses );

  void start( c_csplayer* player, records_t* current );
  void on_fsn( );
}