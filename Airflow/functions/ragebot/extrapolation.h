#pragma once
#include <deque>
#include <vector>

#include "../../base/sdk/entity.h"
#include "../../base/tools/math.h"

class c_csplayer;
struct records_t;

struct extrap_state_t
{
  vector3d origin{ };
  vector3d velocity{ };
  vector3d wish_dir{ };
  vector3d mins{ };
  vector3d maxs{ };

  int flags{ };
  float duck_amount{ };
};

class c_target_extrapolation
{
private:
  struct extrap_record_t
  {
    float sim_time{ };
    vector3d origin{ };
    vector3d velocity{ };
    vector3d mins{ };
    vector3d maxs{ };
    int flags{ };
    float duck_amount{ };
  };

  bool fetch_records( c_csplayer* player, std::vector< extrap_record_t >& out, int count );
  vector3d get_median_velocity( const std::vector< extrap_record_t >& records );
  vector3d infer_wish_dir( const std::vector< extrap_record_t >& records );
  float get_max_speed( c_csplayer* player, bool ducked );

  void apply_friction( extrap_state_t& state, float dt );
  void accelerate( extrap_state_t& state, float wish_speed, float accel, float dt );
  void clamp_speed( extrap_state_t& state, float max_speed );
  void move_and_collide( c_csplayer* player, extrap_state_t& state );
  void check_ground( c_csplayer* player, extrap_state_t& state );
  void air_accelerate( vector3d& velocity, float dir_yaw, float smove, float max_speed, float air_accel, float dt );

public:
  int estimate_choke( c_csplayer* player );
  bool build_state( c_csplayer* player, extrap_state_t& out );
  void simulate_tick( c_csplayer* player, extrap_state_t& state );

  vector3d extrapolate( c_csplayer* player, int ticks );
  std::vector< extrap_state_t > extrapolate_path( c_csplayer* player, int ticks );

  // gamesense-style playermove: extrapolate a record forward and shift its
  // bones. into_rewind targets the server rewind time ( sim_time + lerp ),
  // otherwise the record is brought to now plus lead_ticks ahead.
  // forced_ticks >= 0 overrides the internal tick estimation (ping-shift for broke records)
  bool extrapolate_record( c_csplayer* player, records_t* record, records_t* out, int lead_ticks = 0, bool into_rewind = false, int forced_ticks = -1 );
};
