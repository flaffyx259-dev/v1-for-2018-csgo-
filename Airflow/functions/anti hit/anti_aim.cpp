#include "anti_aim.h"
#include "exploits.h"

#include "../config_vars.h"

#include "../features.h"

#include "../../base/tools/render.h"
#include "../../base/sdk/c_usercmd.h"
#include "../../base/sdk/c_animstate.h"
#include "../../base/sdk/entity.h"

#include <algorithm>
#include <cmath>

anti_aim_angles_t* c_anti_aim::get_config( )
{
  bool ground = g_utils->on_ground( );

  bool stand = ground && ( g_cfg.binds [ sw_b ].toggled || g_rage_bot->should_slide || g_ctx.local->velocity( ).length( true ) < 10.f );

  if( stand )
    return &g_cfg.antihit.angles [ 0 ];
  else if( g_ctx.local->velocity( ).length( true ) > 10.f && ground )
    return &g_cfg.antihit.angles [ 1 ];
  else if( !ground )
    return &g_cfg.antihit.angles [ 2 ];
}

c_csplayer* c_anti_aim::get_closest_player( bool skip, bool local_distance )
{
  auto& player_array = g_listener_entity->get_entity( ent_player );
  if( player_array.empty( ) )
    return nullptr;

  c_csplayer* best = nullptr;
  float best_dist = FLT_MAX;

  vector2d center = vector2d( g_render->screen_size.w * 0.5f, g_render->screen_size.h * 0.5f );

  vector3d view_angles{ };
  interfaces::engine->get_view_angles( view_angles );

  for( const auto& player_info : player_array )
  {
    auto player = ( c_csplayer* )player_info.m_entity;
    if( !player )
      continue;

    if( !player->is_alive( ) || player->gun_game_immunity( ) )
      continue;

    if( player == g_ctx.local || player->team( ) == g_ctx.local->team( ) )
      continue;

    auto& esp_info = g_esp_store->playerinfo [ player->index( ) ];
    if( skip )
    {
      if( player->dormant( ) )
        continue;
    }

    auto base_origin = player->get_abs_origin( );
    base_origin += vector3d( 0.f, 0.f, player->view_offset( ).z / 2.f );

    vector2d origin = { };
    g_render->world_to_screen( base_origin, origin );

    auto angle = math::angle_from_vectors( g_ctx.eye_position, base_origin );

    float dist = local_distance ? math::get_fov( view_angles, angle ) : center.dist_to( origin );
    if( dist < best_dist )
    {
      best = player;
      best_dist = dist;
    }
  }

  return best;
}

vector3d get_predicted_pos( )
{
  float speed = std::max< float >( g_engine_prediction->unprediced_velocity.length( true ), 1.f );

  int max_stop_ticks = std::max< int >( ( ( speed / g_movement->get_max_speed( ) ) * 5.f ) - 1, 0 );
  int max_predict_ticks = std::clamp( 17 - max_stop_ticks, 0, 17 );
  if( max_predict_ticks == 0 )
    return { };

  vector3d last_predicted_velocity = g_engine_prediction->unprediced_velocity;
  for( int i = 0; i < max_predict_ticks; ++i )
  {
    auto pred_velocity = g_engine_prediction->unprediced_velocity * math::ticks_to_time( i + 1 );

    vector3d local_origin = g_ctx.eye_position + pred_velocity;
    int flags = g_ctx.local->flags( );

    g_utils->extrapolate( g_ctx.local, local_origin, pred_velocity, flags, flags & fl_onground );

    last_predicted_velocity = pred_velocity;
  }

  auto predicted_eye_pos = g_ctx.eye_position + last_predicted_velocity;
  return predicted_eye_pos;
}

bool c_anti_aim::is_peeking( )
{
  auto player = this->get_closest_player( );
  if( !player )
    return false;

  auto predicted_eye_pos = get_predicted_pos( );

  bool can_peek = false;
  auto& esp_info = g_esp_store->playerinfo [ player->index( ) ];
  if( !esp_info.valid )
    return false;

  auto origin = player->dormant( ) ? esp_info.dormant_origin : player->get_abs_origin( );

  if( player->dormant( ) )
  {
    vector3d poses [ 3 ]{ origin, origin + player->view_offset( ), origin + vector3d( 0.f, 0.f, player->view_offset( ).z / 2.f ) };

    for( int i = 0; i < 3; ++i )
    {
      c_trace_filter filter{ };
      filter.skip = g_ctx.local;

      c_game_trace out{ };
      interfaces::engine_trace->trace_ray( ray_t( predicted_eye_pos, poses [ i ] ), mask_shot_hull | contents_hitbox, &filter, &out );

      if( out.fraction >= 0.97f )
      {
        can_peek = true;
        break;
      }
      else
        continue;
    }

    if( can_peek )
      return true;
  }
  else
  {
    auto valid_dmg_on_record = [ & ]( records_t* record )
    {
      for( auto& hitbox : hitbox_list )
      {
        auto pts = cheat_tools::get_multipoints( player, hitbox, record->sim_orig.bone );

        for( auto& p : pts )
        {
          g_rage_bot->store( player );
          g_rage_bot->set_record( player, record );

          bool ret = g_auto_wall->can_hit_point( player, p.first, predicted_eye_pos, 0 );

          g_rage_bot->restore( player );

          if( ret )
            return true;
          else
            continue;
        }

        return false;
      }

      return false;
    };

    auto old = g_animation_fix->get_oldest_record( player );

    bool can_peek_to_track = false;
    if( old )
      can_peek_to_track = valid_dmg_on_record( old );

    if( can_peek_to_track )
    {
      can_peek = true;
      return true;
    }

    auto last = g_animation_fix->get_latest_record( player );

    if( last )
    {
      if( valid_dmg_on_record( last ) )
      {
        can_peek = true;
        return true;
      }
    }

    for( auto& hitbox : hitbox_list )
    {
      auto pts = cheat_tools::get_multipoints( player, hitbox, player->bone_cache( ).base( ) );

      for( auto& p : pts )
      {
        bool ret = g_auto_wall->can_hit_point( player, p.first, predicted_eye_pos, 0 );

        if( ret )
        {
          can_peek = true;
          break;
        }
        else
          continue;
      }
    }
  }

  return can_peek;
}

bool c_anti_aim::is_fake_ducking( )
{
  return fake_ducking;
}

void c_anti_aim::fake_duck( )
{
  auto state = g_ctx.local->animstate( );
  if( !state )
    return;

  g_ctx.cmd->buttons |= in_bullrush;

  if( !g_cfg.binds [ fd_b ].toggled )
  {
    fake_ducking = false;
    return;
  }

  if( !g_utils->on_ground( ) || g_ctx.cmd->buttons & in_jump )
  {
    fake_ducking = false;
    return;
  }

  fake_ducking = true;

  if( interfaces::client_state->choked_commands <= 6 )
    g_ctx.cmd->buttons &= ~in_duck;
  else
    g_ctx.cmd->buttons |= in_duck;
}

int c_anti_aim::get_ticks_to_stop( )
{
  static auto predict_velocity = []( vector3d* velocity )
  {
    float speed = velocity->length( true );
    if( speed >= 1.f )
    {
      float friction = cvars::sv_friction->get_float( );
      float stop_speed = std::max< float >( speed, cvars::sv_stopspeed->get_float( ) );
      float time = std::max< float >( interfaces::global_vars->interval_per_tick, interfaces::global_vars->frame_time );
      *velocity *= std::max< float >( 0.f, speed - friction * stop_speed * time / speed );
    }
  };
  auto vel = g_ctx.local->velocity( );
  int ticks_to_stop = 0;
  for( ;; )
  {
    if( vel.length( true ) < 1.f )
      break;
    predict_velocity( &vel );
    ticks_to_stop++;
  }
  return ticks_to_stop;
}

void c_anti_aim::slow_walk( )
{
  if( !g_utils->on_ground( ) )
    return;

  if( g_rage_bot->should_slide )
  {
    g_movement->force_speed( g_movement->get_max_speed( ) * 0.33f );
    g_rage_bot->should_slide = false;
    return;
  }

  if( g_cfg.binds [ sw_b ].toggled )
  {
    g_ctx.cmd->buttons &= ~in_speed;

    int lby_ticks = math::time_to_ticks( g_local_animation_fix->local_info.last_lby_time - interfaces::global_vars->cur_time );
    int ticks = this->get_ticks_to_stop( );

    if( ticks > ( ( g_cfg.antihit.fakewalk_speed - 1 ) - interfaces::client_state->choked_commands ) || !interfaces::client_state->choked_commands )
      g_movement->force_stop( );
  }
}

void c_anti_aim::update_lby_predictor( )
{
  float curtime = interfaces::global_vars->cur_time;
  auto state = g_ctx.local->animstate( );

  bool ground = g_utils->on_ground( );
  float speed = g_ctx.local->velocity( ).length( true );
  float cur_lby = g_ctx.local->lby( );

  // self-tracking (jre proxy idea, local edition): if the server actually rotated
  // our lby since last tick (flick landed or timer expired on real), resync the
  // prediction to reality instead of drifting on a stale timer.
  if( has_prev_lby && cur_lby != prev_lby_value )
  {
    next_lby_update = curtime + ( ( ground && speed > 0.1f ) ? 0.22f : 1.1f );
    will_lby_update = false;
  }
  prev_lby_value = cur_lby;
  has_prev_lby = true;

  if( !ground )
  {
    // airborne: lby doesn't update, keep timer far
    return;
  }

  // BreakLastBody: nearly stopped (<=4 ticks of friction left) counts as standing
  // for the timer, so the flick below can land while still moving and poison
  // enemy last-moving-lby records. full-speed runs keep the 0.22s move timer.
  bool stopping = speed > 0.1f && speed < 120.f && this->get_ticks_to_stop( ) <= 4;

  if( speed > 0.1f && !stopping )
  {
    next_lby_update = curtime + 0.22f;
    will_lby_update = false;
  }
  else
  {
    if( curtime >= next_lby_update )
    {
      next_lby_update = curtime + 1.1f;
      will_lby_update = true;
    }
  }

  if( state )
    last_lby_angle = cur_lby;
}

// lby breaker delta for the flick tick (called only on predicted update).
// flicks the SERVER lby, so weak small deltas are banned: anything under ~70
// leaves real~=fake that tick and hands them a free hit. magnitudes below the
// slider are only honored when the user explicitly asked for a small range.
// desync_type: 0 disabled, 1 static, 2 jitter, 3 spin, 4 random, 5 opposite, 6 safe
float c_anti_aim::get_lby_flick_delta( bool update_tick, float real_yaw )
{
  int type = g_cfg.antihit.desync_type;
  float range = ( float )g_cfg.antihit.desync_range;

  if( type <= 0 || range <= 0.f )
    return 0.f;

  // magnitude band with enforced min width (see above)
  auto flick_mag = [ & ]( )
  {
    float hi = std::clamp( range, 0.f, 180.f );
    float lo = std::min( hi, 70.f );
    if( hi <= lo )
      return hi;
    math::random_seed( interfaces::global_vars->tick_count * 131 + g_ctx.cmd->command_number );
    return math::random_float( lo, hi );
  };

  switch( type )
  {
  case 1: // static (inverter, exact slider value)
    return range * ( g_cfg.binds [ inv_b ].toggled ? -1.f : 1.f );
  case 2: // jitter (flip side every lby update, exact slider value)
    return range * ( flip_side ? 1.f : -1.f );
  case 3: // spin (alternate side + oscillating magnitude 70..range, never weak)
  {
    spin_angle += 0.9f;
    float cap = std::max( range, 70.f );
    float mag = 70.f + ( std::sinf( spin_angle ) * 0.5f + 0.5f ) * ( cap - 70.f );
    return mag * ( flip_side ? 1.f : -1.f );
  }
  case 4: // random (random side + random magnitude in enforced band)
  {
    math::random_seed( interfaces::global_vars->tick_count + ( int )( interfaces::global_vars->cur_time * 1000.f ) );
    float mag = flick_mag( );
    return ( math::random_float( 0.f, 1.f ) < 0.5f ? -mag : mag );
  }
  case 5: // opposite (full 180, ignore slider)
    return 180.f;
  case 6: // safe (Kaaba safe_break homage): flick TOWARD the freestanding side,
    // so the server lby hides behind the same wall as the body. falls back to
    // jitter side when freestanding agrees with real (no cover to hide behind).
  {
    float fs_yaw = 0.f;
    if( get_freestanding_yaw( fs_yaw ) )
    {
      float delta = math::normalize( fs_yaw - real_yaw );
      if( std::fabsf( delta ) >= 70.f )
        return delta;
    }
    return range * ( flip_side ? 1.f : -1.f );
  }
  default:
    return range * ( g_cfg.binds [ inv_b ].toggled ? -1.f : 1.f );
  }
}

// fake yaw offset from last real (called only on send).
// ALL modes are anchored at exactly opposite (real + 180, max desync width):
// static was removed (it was opposite with extra steps), the rest only move
// the exact value around the opposite point so bruteforcers can't pre-aim it.
// fake_type: 0 opposite, 1 jitter, 2 spin, 3 random
float c_anti_aim::get_fake_delta( )
{
  int type = std::clamp( g_cfg.antihit.fake_type, 0, 3 );

  switch( type )
  {
  case 0: // opposite: full 180, always. the hardest single angle to hit.
  default:
    return 180.f;
  case 1: // jitter: flip side every send + fresh random magnitude from YOUR band.
    // same side twice in a row never happens, magnitude never repeats.
    // band is fully yours (0..180) - wide bands are safer, narrow are sneakier.
  {
    int lo = std::clamp( g_cfg.antihit.fake_jitter_min, 0, 180 );
    int hi = std::clamp( g_cfg.antihit.fake_jitter_max, lo, 180 );
    math::random_seed( g_ctx.cmd->command_number * 7919 + interfaces::global_vars->tick_count );
    float mag = hi > lo ? math::random_float( ( float )lo, ( float )hi ) : ( float )hi;
    float delta = 180.f + ( flip_fake ? mag : -mag );
    flip_fake = !flip_fake;
    return math::normalize( delta );
  }
  case 2: // spin: continuous oscillation +-50 around opposite, never near real
  {
    float speed = std::clamp( ( float )g_cfg.antihit.fake_spin_speed, 10.f, 300.f ) * 0.02f;
    return 180.f + std::sinf( g_ctx.system_time( ) * speed ) * 50.f;
  }
  case 3: // random: anywhere in opposite hemisphere, width always >= 110
  {
    math::random_seed( interfaces::global_vars->tick_count * 911 + g_ctx.cmd->command_number );
    return 180.f + math::random_float( -70.f, 70.f );
  }
  }
}

void c_anti_aim::fake( )
{
  c_animstate* state = g_ctx.local->animstate( );
  if( !state )
    return;

  float base_real = has_last_real ? last_real_angle : g_ctx.cmd->viewangles.y;

  g_ctx.cmd->viewangles.y = math::normalize( base_real + get_fake_delta( ) );
  last_fake_angle = g_ctx.cmd->viewangles.y;
}

void c_anti_aim::manual_yaw( float& yaw, bool& overridden )
{
  overridden = false;

  // back has priority (like getze side logic), then left/right toggle off each other
  if( g_cfg.binds [ back_b ].toggled )
  {
    yaw = math::normalize( g_ctx.cmd->viewangles.y + 180.f );
    overridden = true;
    return;
  }

  if( g_cfg.binds [ left_b ].toggled )
  {
    yaw = math::normalize( g_ctx.cmd->viewangles.y - 90.f );
    overridden = true;
    return;
  }

  if( g_cfg.binds [ right_b ].toggled )
  {
    yaw = math::normalize( g_ctx.cmd->viewangles.y + 90.f );
    overridden = true;
    return;
  }
}

// ideal freestanding: 3 points (left/right/back) from best enemy eye to local offsets.
// damage first, wall fraction fallback, hysteresis anti-spin. returns absolute yaw.
bool c_anti_aim::get_freestanding_yaw( float& out_yaw )
{
  if( !g_cfg.binds [ edge_b ].toggled )
    return false;

  if( !g_utils->on_ground( ) )
    return false;

  auto player = get_closest_player( false, true );
  if( !player )
    return false;

  // target changed -> reset hysteresis so we snap fast to new enemy
  if( freestanding_target != player->index( ) )
  {
    freestanding_target = player->index( );
    freestanding_side = 0;
    freestanding_time = -1000.f;
  }

  vector3d local_eye = g_ctx.local->get_eye_position( );
  vector3d enemy_eye = player->get_eye_position( );

  // line local -> enemy
  float line = math::normalize( math::angle_from_vectors( g_ctx.local->origin( ), player->origin( ) ).y );

  vector3d dir_left, dir_right, dir_back;
  math::angle_to_vectors( vector3d( 0.f, line - 90.f, 0.f ), dir_left );
  math::angle_to_vectors( vector3d( 0.f, line + 90.f, 0.f ), dir_right );
  math::angle_to_vectors( vector3d( 0.f, line + 180.f, 0.f ), dir_back );

  const float height = 64.f;
  vector3d base = g_ctx.local->origin( ) + vector3d( 0.f, 0.f, height );

  vector3d left_pos = base + ( dir_left * 16.f );
  vector3d right_pos = base + ( dir_right * 16.f );
  vector3d back_pos = base + ( dir_back * 16.f );

  int mode = std::clamp( g_cfg.antihit.freestanding_mode, 0, 2 );
  bool use_damage = ( mode == 0 || mode == 2 );
  bool use_wall = ( mode == 0 || mode == 1 );

  int left_dmg = 0, right_dmg = 0, back_dmg = 0;
  float left_frac = 1.f, right_frac = 1.f, back_frac = 1.f;

  if( use_damage )
  {
    auto weapon = player->get_active_weapon( );
    auto weapon_info = weapon ? weapon->get_weapon_info( ) : nullptr;
    if( weapon && weapon_info )
    {
      left_dmg = g_auto_wall->fire_bullet( player, g_ctx.local, weapon_info, weapon->is_taser( ), enemy_eye, left_pos ).dmg;
      right_dmg = g_auto_wall->fire_bullet( player, g_ctx.local, weapon_info, weapon->is_taser( ), enemy_eye, right_pos ).dmg;
      back_dmg = g_auto_wall->fire_bullet( player, g_ctx.local, weapon_info, weapon->is_taser( ), enemy_eye, back_pos ).dmg;
    }
    else
      use_damage = false; // no gun to test with -> wall only
  }

  if( use_wall )
  {
    c_game_trace trace = { };
    c_trace_filter_world_only filter = { };

    interfaces::engine_trace->trace_ray( ray_t( left_pos, enemy_eye ), mask_all, &filter, &trace );
    left_frac = trace.fraction;

    interfaces::engine_trace->trace_ray( ray_t( right_pos, enemy_eye ), mask_all, &filter, &trace );
    right_frac = trace.fraction;

    interfaces::engine_trace->trace_ray( ray_t( back_pos, enemy_eye ), mask_all, &filter, &trace );
    back_frac = trace.fraction;
  }

  int best_side = 0; // -1 left, 1 right, 2 back(away)

  if( use_damage && ( left_dmg > 0 || right_dmg > 0 || back_dmg > 0 ) )
  {
    // both sides hittable well -> no cover, stay back (don't spin)
    if( left_dmg >= 20 && right_dmg >= 20 && back_dmg >= 20 )
    {
      out_yaw = math::normalize( line + 180.f );
      return true;
    }

    int best_dmg = INT_MAX;
    if( back_dmg < best_dmg ) { best_dmg = back_dmg; best_side = 2; }
    if( left_dmg < best_dmg - 2 ) { best_dmg = left_dmg; best_side = -1; }
    if( right_dmg < best_dmg - 2 ) { best_dmg = right_dmg; best_side = 1; }

    // close damage -> decide by wall
    if( use_wall && std::abs( left_dmg - right_dmg ) <= 5 && std::abs( left_dmg - back_dmg ) <= 8 )
    {
      float best_f = 2.f;
      if( back_frac < best_f ) { best_f = back_frac; best_side = 2; }
      if( left_frac < best_f - 0.03f ) { best_f = left_frac; best_side = -1; }
      if( right_frac < best_f - 0.03f ) { best_f = right_frac; best_side = 1; }
    }
  }
  else if( use_wall )
  {
    float best_f = 2.f;
    if( back_frac < best_f ) { best_f = back_frac; best_side = 2; }
    if( left_frac < best_f - 0.03f ) { best_f = left_frac; best_side = -1; }
    if( right_frac < best_f - 0.03f ) { best_f = right_frac; best_side = 1; }

    // open field (all clear) -> stay back, don't jitter
    if( left_frac > 0.97f && right_frac > 0.97f && back_frac > 0.97f )
    {
      out_yaw = math::normalize( line + 180.f );
      return true;
    }

    // full tie -> keep last side (THE anti-spin fix)
    if( std::fabsf( left_frac - right_frac ) <= 0.03f && std::fabsf( left_frac - back_frac ) <= 0.03f )
      best_side = freestanding_side;
  }
  else
    return false;

  // hysteresis: max one flip per 0.3s (fixes "крутит непонятно как")
  float curtime = interfaces::global_vars->cur_time;
  if( best_side != freestanding_side && freestanding_side != 0 )
  {
    if( curtime - freestanding_time < 0.3f )
      best_side = freestanding_side;
  }

  float yaw = line + 180.f;
  if( best_side == -1 )
    yaw = line - 90.f;
  else if( best_side == 1 )
    yaw = line + 90.f;

  out_yaw = math::normalize( yaw );

  if( best_side != freestanding_side )
  {
    freestanding_side = best_side;
    freestanding_time = curtime;
  }
  freestanding_yaw = out_yaw;

  return true;
}

void c_anti_aim::automatic_edge( float& yaw )
{
  float fs_yaw = 0.f;
  if( get_freestanding_yaw( fs_yaw ) )
    yaw = fs_yaw;
}

void c_anti_aim::at_targets( float& base )
{
  auto cfg = this->get_config( );
  if( !cfg )
    return;

  if( !cfg->at_targets )
    return;

  auto player = this->get_closest_player( false, true );
  if( !player )
    return;

  // aim base only (facing the enemy). yaw modes (backward/spin/...) apply
  // ON TOP of this base - previously this overwrote the final yaw and
  // backward+at_targets faced the enemy instead of away. THE backward fix.
  base = math::normalize( math::angle_from_vectors( g_ctx.local->get_abs_origin( ), player->get_abs_origin( ) ).y );
}

void c_anti_aim::on_pre_predict( )
{
  if( g_ctx.local->move_type( ) == movetype_noclip || g_ctx.local->move_type( ) == movetype_ladder )
    return;

  if( interfaces::game_rules->is_freeze_time( ) || g_ctx.local->flags( ) & fl_frozen || g_ctx.local->gun_game_immunity( ) )
    return;

  this->fake_duck( );
  this->slow_walk( );
}

void c_anti_aim::on_predict_start( )
{
  if( !g_cfg.antihit.enable )
  {
    g_exploits->stop_movement = false;
    return;
  }

  if( interfaces::game_rules->is_freeze_time( ) || g_ctx.local->flags( ) & fl_frozen || g_ctx.local->gun_game_immunity( ) )
  {
    g_exploits->stop_movement = false;
    return;
  }

  auto state = g_ctx.local->animstate( );
  if( !state )
  {
    g_exploits->stop_movement = false;
    return;
  }

  g_exploits->stop_movement = false;

  if( g_utils->is_firing( ) )
    aa_shot_cmd = g_ctx.cmd->command_number;

  if( g_ctx.local->move_type( ) == movetype_ladder || g_ctx.local->move_type( ) == movetype_noclip )
    return;

  if( g_ctx.cmd->buttons & in_use )
    return;

  if( aa_shot_cmd == g_ctx.cmd->command_number )
    return;

  bool moving = g_ctx.cmd->buttons & in_moveleft || g_ctx.cmd->buttons & in_moveright || g_ctx.cmd->buttons & in_forward || g_ctx.cmd->buttons & in_back;

  auto cfg = this->get_config( );

  if( !cfg )
    return;

  bool stand = g_cfg.binds [ sw_b ].toggled || g_rage_bot->should_slide || g_ctx.local->velocity( ).length( true ) < 10.f;

  switch( cfg->pitch )
  {
  case 1:
    g_ctx.cmd->viewangles.x = state->aim_pitch_max;
    break;
  case 2:
    g_ctx.cmd->viewangles.x = -state->aim_pitch_min;
    break;
  case 3:
  {
    if( stand )
      g_ctx.cmd->viewangles.x = state->aim_pitch_max - math::random_float( 0.f, 10.f );
    else
      g_ctx.cmd->viewangles.x = state->aim_pitch_max;
  }
  break;
  }

  // ideal real builder: manual/freestand absolute, otherwise aim base
  // (camera, or facing-enemy when at_targets) + relative yaw modes.
  // fixes: freestanding += spin (old), and at_targets swallowing backward
  // (backward+at_targets faced the enemy - now base+180 faces away, correct).
  auto real = [ & ]( float& out_yaw )
  {
    float camera = g_ctx.cmd->viewangles.y;

    float manual_yaw_val = camera;
    bool manual_over = false;
    this->manual_yaw( manual_yaw_val, manual_over );

    if( manual_over )
    {
      float yaw = manual_yaw_val;

      switch( cfg->jitter_mode )
      {
      case 1: // center
        yaw = math::normalize( yaw + ( flip_jitter ? ( float )cfg->jitter_range : -( float )cfg->jitter_range ) );
        break;
      case 2: // offset
        if( flip_jitter )
          yaw = math::normalize( yaw + ( float )cfg->jitter_range );
        break;
      case 3: // random
        yaw = math::normalize( yaw + math::random_float( -( float )cfg->jitter_range, ( float )cfg->jitter_range ) );
        break;
      default:
        break;
      }

      out_yaw = math::normalize( yaw );
      return;
    }

    float fs_yaw = 0.f;
    if( get_freestanding_yaw( fs_yaw ) )
    {
      float yaw = fs_yaw;

      switch( cfg->jitter_mode )
      {
      case 1: // center
        yaw = math::normalize( yaw + ( flip_jitter ? ( float )cfg->jitter_range : -( float )cfg->jitter_range ) );
        break;
      case 2: // offset
        if( flip_jitter )
          yaw = math::normalize( yaw + ( float )cfg->jitter_range );
        break;
      case 3: // random
        yaw = math::normalize( yaw + math::random_float( -( float )cfg->jitter_range, ( float )cfg->jitter_range ) );
        break;
      default:
        break;
      }

      if( !( g_cfg.binds [ left_b ].toggled || g_cfg.binds [ right_b ].toggled || g_cfg.binds [ back_b ].toggled ) )
        yaw = math::normalize( yaw + ( float )cfg->yaw_add );

      out_yaw = math::normalize( yaw );
      return;
    }

    float base = camera;
    this->at_targets( base );

    float yaw = base;
    switch( cfg->yaw )
    {
    case 1: // backward: away from aim base (away from enemy when at_targets)
      yaw = math::normalize( base + 180.f );
      break;
    case 2: // distortion / spin
    {
      if( cfg->random_speed )
      {
        float curtime = interfaces::global_vars->cur_time;
        if( curtime - last_spin_rand > 0.6f )
        {
          math::random_seed( interfaces::global_vars->tick_count );
          spin_rand_speed = math::random_float( 5.f, 70.f );
          last_spin_rand = curtime;
        }
      }
      float speed_preset = cfg->random_speed ? spin_rand_speed : ( float )cfg->distortion_speed;
      float speed = ( speed_preset * 3.f ) * 0.01f;
      yaw = math::normalize( base + 180.f + std::sinf( ( g_ctx.system_time( ) * speed ) * 3.14f ) * ( float )cfg->distortion_range );
    }
    break;
    case 3: // crooked (lby-based)
      yaw = math::normalize( g_ctx.local->lby( ) + ( float )cfg->crooked_offset );
      break;
    default:
      break;
    }

    // jitter (relative, always after base)
    switch( cfg->jitter_mode )
    {
    case 1: // center
      yaw = math::normalize( yaw + ( flip_jitter ? ( float )cfg->jitter_range : -( float )cfg->jitter_range ) );
      break;
    case 2: // offset
      if( flip_jitter )
        yaw = math::normalize( yaw + ( float )cfg->jitter_range );
      break;
    case 3: // random
      yaw = math::normalize( yaw + math::random_float( -( float )cfg->jitter_range, ( float )cfg->jitter_range ) );
      break;
    default:
      break;
    }

    // yaw add only when no manual (classic)
    if( !( g_cfg.binds [ left_b ].toggled || g_cfg.binds [ right_b ].toggled || g_cfg.binds [ back_b ].toggled ) )
      yaw = math::normalize( yaw + ( float )cfg->yaw_add );

    out_yaw = math::normalize( yaw );
  };

  if( *g_ctx.send_packet )
    flip_jitter = !flip_jitter;

  update_lby_predictor( );

  if( g_cfg.antihit.desync )
  {
    if( !*g_ctx.send_packet )
    {
      float real_yaw = g_ctx.cmd->viewangles.y;
      real( real_yaw );
      g_ctx.cmd->viewangles.y = real_yaw;

      // remember real for fake reference (THE fix: old code never stored it)
      last_real_angle = real_yaw;
      has_last_real = true;

      bool stand_local = g_cfg.binds [ sw_b ].toggled || g_rage_bot->should_slide || g_ctx.local->velocity( ).length( true ) < 10.f;
      bool ground = g_utils->on_ground( );

      // lby flick when standing on ground. PLUS Kaaba BreakLastBody homage: when
      // decelerating to a stop within ~4 ticks, flick EARLY (while still moving).
      // the server takes the flick (move-timer 0.22s) and every "last moving lby"
      // resolver on the enemy side records the BREAK angle as our moving lby -
      // their last-move logic then aims where we wanted, not where we are.
      float speed_2d = g_ctx.local->velocity( ).length( true );
      bool stopping_poison = ground && !stand_local && speed_2d < 120.f && this->get_ticks_to_stop( ) <= 4;

      if( ( stand_local || stopping_poison ) && ground && g_cfg.antihit.desync_type != 0 )
      {
        float curtime = interfaces::global_vars->cur_time;
        bool is_update = ( curtime >= next_lby_update - 0.02f ) || will_lby_update;

        // flick whenever the update is due and force the send: the flicked angle
        // must be what the server sees at expiry. the old choked==0 gate skipped
        // the flick mid-fakelag-cycle and handed them a free real-lby update.
        // not while landing (landing breaks the lby timer, flick next tick).
        // last_real_angle keeps the TRUE real so fake stays opposite-anchored.
        bool landing = g_ctx.local->fall_velocity( ) > 0.f;
        if( is_update && !landing )
        {
          float delta = get_lby_flick_delta( true, real_yaw );
          if( delta != 0.f )
          {
            g_ctx.cmd->viewangles.y = math::normalize( real_yaw + delta );

            *g_ctx.send_packet = true;

            next_lby_update = curtime + 1.1f;
            will_lby_update = false;
            flip_side = !flip_side;
          }
          else if( will_lby_update )
            will_lby_update = false;
        }
      }
    }
    else
      this->fake( );
  }
  else
  {
    float real_yaw = g_ctx.cmd->viewangles.y;
    real( real_yaw );
    g_ctx.cmd->viewangles.y = real_yaw;
    last_real_angle = real_yaw;
    has_last_real = true;
  }
}

void c_anti_aim::on_predict_end( )
{
  g_ctx.cmd->viewangles = math::normalize( g_ctx.cmd->viewangles, true );

  if( g_ctx.local->move_type( ) == movetype_ladder || g_ctx.local->move_type( ) == movetype_noclip )
    return;

  g_movement->fix_movement( g_ctx.cmd, g_ctx.base_angle );
}