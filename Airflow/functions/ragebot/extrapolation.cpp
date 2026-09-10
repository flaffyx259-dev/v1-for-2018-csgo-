#include "extrapolation.h"

#include "animfix.h"
#include "../features.h"

#include "../../base/interfaces/engine_trace.h"

#include <algorithm>

create_feature_ptr( target_extrapolation );

bool c_target_extrapolation::extrapolate_record( c_csplayer* player, records_t* record, records_t* out, int lead_ticks, bool into_rewind, int forced_ticks )
{
  if( !record || !out || !player )
    return false;

  *out = *record;

  float interval = interfaces::global_vars->interval_per_tick;
  float time_now = g_ctx.predicted_curtime > 0.f ? g_ctx.predicted_curtime : interfaces::global_vars->cur_time;

  // trend data from the previous records ( supremacy style ):
  // fakelag cycle length + velocity direction change per tick
  std::vector< extrap_record_t > history{ };
  fetch_records( player, history, 3 );

  int lag_cycle{ };
  if( history.size( ) >= 3 )
    lag_cycle = math::time_to_ticks( history [ 1 ].sim_time - history [ 2 ].sim_time );
  else if( history.size( ) >= 2 )
    lag_cycle = math::time_to_ticks( history [ 0 ].sim_time - history [ 1 ].sim_time );

  lag_cycle = std::clamp( lag_cycle, 1, 15 );

  float change{ };
  float dir_yaw{ };

  if( !history.empty( ) )
  {
    const auto& vel = history [ 0 ].velocity;
    if( vel.y != 0.f || vel.x != 0.f )
      dir_yaw = math::rad_to_deg( std::atan2( vel.y, vel.x ) );

    if( history.size( ) >= 2 )
    {
      const auto& prev_vel = history [ 1 ].velocity;
      float dt = history [ 0 ].sim_time - history [ 1 ].sim_time;

      float prev_dir = dir_yaw;
      if( prev_vel.y != 0.f || prev_vel.x != 0.f )
        prev_dir = math::rad_to_deg( std::atan2( prev_vel.y, prev_vel.x ) );

      if( dt > 0.0001f )
        change = math::normalize( dir_yaw - prev_dir ) / dt * interval;

      // turning too hard - assume straight line
      if( std::fabsf( change ) > 6.f )
        change = 0.f;
    }
  }

  int ticks{ };
  if( forced_ticks >= 0 )
  {
    // explicit shift (broke-record ping compensation), no cycle guessing
    ticks = forced_ticks;
  }
  else if( into_rewind )
  {
    // the server rewinds to the tick at sim_time + lerp; that state was
    // reconstructed with RunCommand, so it is lerp ticks ahead of the record
    ticks = math::time_to_ticks( g_ctx.lerp_time );
  }
  else
  {
    int elapsed = math::time_to_ticks( time_now - record->sim_time );

    if( lead_ticks > 0 )
    {
      ticks = elapsed + std::clamp( lead_ticks, 1, 4 );
    }
    else
    {
      // shoot ahead of the fakelag: predict how much of the choke cycle
      // is still remaining ( uses the PREVIOUS cycle length to counter
      // fakelags that alternate between two values ) plus one tick for
      // the shot being processed on the next server tick
      int remaining = std::max( lag_cycle - elapsed, 0 );
      int lead = std::clamp( remaining, 0, 3 ) + 1;

      ticks = elapsed + lead;
    }
  }

  ticks = std::clamp( ticks, 0, 16 );

  out->extrap_ticks = ticks;

  if( ticks <= 0 )
    return true;

  vector3d origin = record->origin;
  vector3d velocity = record->velocity;
  vector3d mins = record->mins;
  vector3d maxs = record->maxs;
  int flags = record->flags;

  static auto air_acceleration = interfaces::convar->find_convar( xor_c_s( "sv_airaccelerate" ) );
  float air_accel = air_acceleration ? air_acceleration->get_float( ) : 12.f;
  float max_speed = get_max_speed( player, record->duck_amount > 0.5f );

  // seed the alternating strafe with the observed turn direction so the
  // first predicted tick continues the real strafe instead of guessing
  float strafe_mod = change < 0.f ? -1.f : 1.f;

  c_trace_filter_world_only filter{ };

  for( int i = 0; i < ticks; ++i )
  {
    // continue the velocity rotation trend
    dir_yaw = math::normalize( dir_yaw + change );

    float hyp = velocity.length( true );
    velocity.x = std::cos( math::deg_to_rad( dir_yaw ) ) * hyp;
    velocity.y = std::sin( math::deg_to_rad( dir_yaw ) ) * hyp;

    if( flags & fl_onground )
    {
      // hop prediction only when the player is actually jumping ( rising z ).
      // forcing a jump impulse on a running grounded player lifts the model
      // ~5 units into the air even though it stands on the ground
      bool hopping = velocity.z > 32.f;

      if( hopping )
      {
        float speed = velocity.length( false );
        float cap = max_speed * 1.1f;

        if( cap > 0.f && speed > cap )
          velocity *= ( cap / speed );

        velocity.z = cvars::sv_jump_impulse->get_float( );
      }
      else
      {
        // grounded movement decays with friction
        float speed = velocity.length( true );

        if( speed > 0.1f )
        {
          float stop_speed = std::max< float >( cvars::sv_stopspeed->get_float( ), 0.1f );
          float control = std::max( speed, stop_speed );
          float drop = control * cvars::sv_friction->get_float( ) * interval;
          float scale = std::max( speed - drop, 0.f ) / speed;

          velocity.x *= scale;
          velocity.y *= scale;
        }
      }
    }
    else
    {
      // gravity
      velocity.z -= cvars::sv_gravity->get_float( ) * interval;

      // ideal strafe prediction
      float speed_2d = velocity.length( true );
      float ideal = speed_2d > 0.f ? math::rad_to_deg( std::asinf( std::clamp( 15.f / speed_2d, 0.f, 1.f ) ) ) : 90.f;
      ideal = std::clamp( ideal, 0.f, 90.f );

      float smove{ };
      if( std::fabsf( change ) <= ideal || std::fabsf( change ) >= 30.f )
      {
        dir_yaw += ideal * strafe_mod;
        smove = 450.f * strafe_mod;
        strafe_mod *= -1.f;
      }
      else
        smove = change > 0.f ? -450.f : 450.f;

      air_accelerate( velocity, dir_yaw, smove, max_speed, air_accel, interval );
    }

    // playermove: collide and slide
    vector3d start = origin;
    vector3d end = start + velocity * interval;

    c_game_trace trace{ };
    interfaces::engine_trace->trace_ray( ray_t( start, end, mins, maxs ), contents_solid, &filter, &trace );

    if( trace.fraction != 1.f )
    {
      for( int j = 0; j < 2; ++j )
      {
        if( velocity.length( false ) == 0.f )
          break;

        velocity -= trace.plane.normal * velocity.dot( trace.plane.normal );

        float adjust = velocity.dot( trace.plane.normal );
        if( adjust < 0.f )
          velocity -= trace.plane.normal * adjust;

        start = trace.end;
        end = start + velocity * ( interval * ( 1.f - trace.fraction ) );

        interfaces::engine_trace->trace_ray( ray_t( start, end, mins, maxs ), contents_solid, &filter, &trace );

        if( trace.fraction == 1.f )
          break;
      }
    }

    start = end = origin = trace.end;

    end.z -= 2.f;

    interfaces::engine_trace->trace_ray( ray_t( start, end, mins, maxs ), contents_solid, &filter, &trace );

    // grounded only on a real downward hit with a walkable plane -
    // brushing a wall must not set onground ( it kept ground friction on )
    if( trace.fraction < 1.f && trace.plane.normal.z > 0.7f )
      flags |= fl_onground;
    else if( trace.fraction == 1.f )
      flags &= ~fl_onground;
  }

  vector3d delta = origin - record->origin;

  for( auto& mat : out->sim_orig.bone )
    mat.set_origin( mat.get_origin( ) + delta );

  for( auto& mat : out->render_bones )
    mat.set_origin( mat.get_origin( ) + delta );

  out->origin = origin;
  out->abs_origin = origin;
  out->velocity = velocity;
  out->flags = flags;

  return true;
}

bool c_target_extrapolation::fetch_records( c_csplayer* player, std::vector< extrap_record_t >& out, int count )
{
  auto anim_player = g_animation_fix->get_animation_player( player->index( ) );
  if( !anim_player || anim_player->records.empty( ) )
    return false;

  const std::unique_lock< std::mutex > lock( mutexes::animfix );

  for( auto& record : anim_player->records )
  {
    if( !record.is_valid( ) )
      continue;

    out.push_back( { record.sim_time, record.origin, record.velocity, record.mins, record.maxs, record.flags, record.duck_amount } );

    if( ( int )out.size( ) >= count )
      break;
  }

  return !out.empty( );
}

vector3d c_target_extrapolation::get_median_velocity( const std::vector< extrap_record_t >& records )
{
  if( records.empty( ) )
    return {};

  int count = std::min< int >( 3, ( int )records.size( ) );

  std::vector< float > xs, ys, zs;
  xs.reserve( count );
  ys.reserve( count );
  zs.reserve( count );

  for( int i = 0; i < count; ++i )
  {
    xs.push_back( records [ i ].velocity.x );
    ys.push_back( records [ i ].velocity.y );
    zs.push_back( records [ i ].velocity.z );
  }

  auto median = []( std::vector< float >& v )
  {
    size_t mid = v.size( ) / 2;
    std::nth_element( v.begin( ), v.begin( ) + mid, v.end( ) );
    return v [ mid ];
  };

  return { median( xs ), median( ys ), median( zs ) };
}

vector3d c_target_extrapolation::infer_wish_dir( const std::vector< extrap_record_t >& records )
{
  if( records.empty( ) )
    return {};

  vector3d dir{ };

  int n = std::min< int >( 4, ( int )records.size( ) - 1 );

  for( int i = 0; i < n; ++i )
  {
    const auto& new_rec = records [ i ];
    const auto& old_rec = records [ i + 1 ];

    float dt = new_rec.sim_time - old_rec.sim_time;
    if( dt <= 0.0001f )
      continue;

    vector3d dv = new_rec.velocity - old_rec.velocity;
    dv.z = 0.f;

    float len = dv.length( true );
    if( len > 3.f )
      dir += dv * ( 1.f / len );
  }

  if( dir.length( true ) > 0.25f )
    return dir * ( 1.f / dir.length( true ) );

  vector3d vel = records.front( ).velocity;
  vel.z = 0.f;

  float len = vel.length( true );
  if( len > 35.f )
    return vel * ( 1.f / len );

  return {};
}

float c_target_extrapolation::get_max_speed( c_csplayer* player, bool ducked )
{
  float max_speed = 260.f;

  auto weapon = player->get_active_weapon( );
  if( weapon )
  {
    auto info = weapon->get_weapon_info( );
    if( info )
      max_speed = player->is_scoped( ) ? info->max_speed_alt : info->max_speed;
  }

  if( ducked )
    max_speed *= 0.34f;

  return max_speed;
}

int c_target_extrapolation::estimate_choke( c_csplayer* player )
{
  std::vector< extrap_record_t > records{ };
  if( !fetch_records( player, records, 2 ) || records.size( ) < 2 )
    return 0;

  float dt = records [ 0 ].sim_time - records [ 1 ].sim_time;
  if( dt <= interfaces::global_vars->interval_per_tick * 1.5f )
    return 0;

  return std::clamp( math::time_to_ticks( dt ) - 1, 0, g_ctx.max_choke );
}

bool c_target_extrapolation::build_state( c_csplayer* player, extrap_state_t& out )
{
  std::vector< extrap_record_t > records{ };
  if( !fetch_records( player, records, 5 ) )
    return false;

  const auto& last = records.front( );

  out.origin = last.origin;
  out.mins = last.mins;
  out.maxs = last.maxs;
  out.flags = last.flags;
  out.duck_amount = last.duck_amount;

  vector3d smoothed = get_median_velocity( records );

  if( records.size( ) >= 2 )
  {
    const auto& prev = records [ 1 ];

    float dt = last.sim_time - prev.sim_time;
    if( dt > 0.0001f )
    {
      vector3d measured = ( last.origin - prev.origin ) * ( 1.f / dt );
      measured.z = last.velocity.z;

      if( measured.length( true ) > 1.f )
        smoothed = measured * 0.35f + smoothed * 0.65f;
    }
  }

  out.velocity = smoothed;
  out.wish_dir = infer_wish_dir( records );

  return true;
}

void c_target_extrapolation::apply_friction( extrap_state_t& state, float dt )
{
  if( !( state.flags & fl_onground ) )
    return;

  float speed = state.velocity.length( true );

  if( speed < 0.1f )
  {
    state.velocity.x = 0.f;
    state.velocity.y = 0.f;
    return;
  }

  float stop_speed = std::max< float >( cvars::sv_stopspeed->get_float( ), 0.1f );
  float control = std::max( speed, stop_speed );
  float drop = control * cvars::sv_friction->get_float( ) * dt;

  float scale = std::max( speed - drop, 0.f ) / speed;

  state.velocity.x *= scale;
  state.velocity.y *= scale;
}

void c_target_extrapolation::accelerate( extrap_state_t& state, float wish_speed, float accel, float dt )
{
  if( state.wish_dir.length( true ) < 0.01f || wish_speed <= 0.f )
    return;

  float current_speed = state.velocity.dot( state.wish_dir );
  float add_speed = wish_speed - current_speed;

  if( add_speed <= 0.f )
    return;

  float accel_speed = accel * wish_speed * dt;
  if( accel_speed > add_speed )
    accel_speed = add_speed;

  state.velocity += state.wish_dir * accel_speed;
}

void c_target_extrapolation::clamp_speed( extrap_state_t& state, float max_speed )
{
  float speed = state.velocity.length( true );

  if( speed <= max_speed || speed <= 0.f )
    return;

  float scale = max_speed / speed;

  state.velocity.x *= scale;
  state.velocity.y *= scale;
}

void c_target_extrapolation::move_and_collide( c_csplayer* player, extrap_state_t& state )
{
  const auto src = state.origin;
  auto end = src + state.velocity * interfaces::global_vars->interval_per_tick;

  c_game_trace t{ };
  c_trace_filter filter;
  filter.skip = player;

  interfaces::engine_trace->trace_ray( ray_t( src, end, state.mins, state.maxs ), mask_playersolid, &filter, &t );

  if( t.fraction != 1.f )
  {
    for( auto i = 0; i < 2; i++ )
    {
      state.velocity -= t.plane.normal * state.velocity.dot( t.plane.normal );

      const auto dot = state.velocity.dot( t.plane.normal );
      if( dot < 0.f )
        state.velocity -= vector3d( dot * t.plane.normal.x, dot * t.plane.normal.y, dot * t.plane.normal.z );

      end = t.end + state.velocity * ( interfaces::global_vars->interval_per_tick * ( 1.f - t.fraction ) );

      interfaces::engine_trace->trace_ray( ray_t( t.end, end, state.mins, state.maxs ), mask_playersolid, &filter, &t );

      if( t.fraction == 1.f )
        break;
    }

    if( t.plane.normal.z > 0.7f && state.velocity.z < 0.f )
      state.velocity.z = 0.f;
  }

  state.origin = t.end;
}

void c_target_extrapolation::check_ground( c_csplayer* player, extrap_state_t& state )
{
  c_game_trace t{ };
  c_trace_filter filter;
  filter.skip = player;

  auto end = state.origin - vector3d( 0.f, 0.f, 2.f );

  interfaces::engine_trace->trace_ray( ray_t( state.origin, end, state.mins, state.maxs ), mask_playersolid, &filter, &t );

  state.flags &= ~fl_onground;

  if( t.did_hit( ) && t.plane.normal.z > 0.7f )
  {
    state.flags |= fl_onground;

    if( state.velocity.z < 0.f )
      state.velocity.z = 0.f;
  }
}

void c_target_extrapolation::simulate_tick( c_csplayer* player, extrap_state_t& state )
{
  float dt = interfaces::global_vars->interval_per_tick;

  bool ducked = state.duck_amount > 0.5f;
  float max_speed = get_max_speed( player, ducked );

  if( state.flags & fl_onground )
  {
    apply_friction( state, dt );
    accelerate( state, max_speed, 5.5f, dt );
    clamp_speed( state, max_speed );
  }
  else
    state.velocity.z -= cvars::sv_gravity->get_float( ) * dt;

  move_and_collide( player, state );
  check_ground( player, state );
}

vector3d c_target_extrapolation::extrapolate( c_csplayer* player, int ticks )
{
  extrap_state_t state{ };

  if( !build_state( player, state ) )
    return player->get_abs_origin( );

  ticks = std::clamp( ticks, 0, g_ctx.max_choke );

  for( int i = 0; i < ticks; ++i )
    simulate_tick( player, state );

  return state.origin;
}

std::vector< extrap_state_t > c_target_extrapolation::extrapolate_path( c_csplayer* player, int ticks )
{
  extrap_state_t state{ };

  std::vector< extrap_state_t > path{ };

  if( !build_state( player, state ) )
    return path;

  ticks = std::clamp( ticks, 0, g_ctx.max_choke );
  path.reserve( ticks );

  for( int i = 0; i < ticks; ++i )
  {
    simulate_tick( player, state );
    path.push_back( state );
  }

  return path;
}

void c_target_extrapolation::air_accelerate( vector3d& velocity, float dir_yaw, float smove, float max_speed, float air_accel, float dt )
{
  vector3d forward{ }, right{ }, up{ };
  math::angle_to_vectors( vector3d( 0.f, dir_yaw, 0.f ), forward, right, up );

  forward.z = 0.f;
  right.z = 0.f;

  vector3d wishvel = right * smove;
  wishvel.z = 0.f;

  vector3d wishdir = wishvel;
  float wishspeed = wishdir.normalized_float( );

  if( wishspeed <= 0.f )
    return;

  if( wishspeed > max_speed )
    wishspeed = max_speed;

  float wishspd = std::min( wishspeed, 30.f );

  float current_speed = velocity.dot( wishdir );
  float add_speed = wishspd - current_speed;

  if( add_speed <= 0.f )
    return;

  float accel_speed = air_accel * wishspeed * dt;
  if( accel_speed > add_speed )
    accel_speed = add_speed;

  velocity += wishdir * accel_speed;
}
