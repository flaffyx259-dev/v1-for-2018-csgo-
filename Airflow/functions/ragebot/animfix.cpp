#include "animfix.h"
#include "../features.h"
#include "../../base/tools/threads.h"

void c_animation_fix::anim_player_t::simulate_animation_side( records_t* record )
{
  auto state = ptr->animstate( );
  if( !state )
    return;

  if( !last_record )
    state->last_update_time = ( record->sim_time - interfaces::global_vars->interval_per_tick );

  restore_anims_t restore{ };
  restore.store( ptr );

  if( !teammate )
    resolver::start( ptr, record );
  else
  {
    // teammates: no resolver, just face backward for stable bones
    float backward = math::normalize( math::angle_from_vectors( g_ctx.local->origin( ), ptr->origin( ) ).y );
    if( record->velocity.length( true ) > 0.1f && !record->fake_walking )
      ptr->eye_angles( ).y = record->lby;
    else
      ptr->eye_angles( ).y = backward;
  }

  // pitch fix (2018): choked pitch is unreliable, revert spikes
  if( last_record && record->choke > 1 && !record->shooting )
  {
    float pitch = ptr->eye_angles( ).x;
    float last_pitch = last_record->eye_angles.x;
    if( std::fabsf( pitch - last_pitch ) > 15.f && std::fabsf( pitch - last_record->eye_angles.x ) > 10.f )
      ptr->eye_angles( ).x = last_pitch;
  }
  ptr->eye_angles( ).x = std::clamp( math::normalize( ptr->eye_angles( ).x ), -89.f, 89.f );

  if( last_record && record->choke >= 1 )
  {
    if( record->on_ground )
      ptr->flags( ) |= fl_onground;
    else
      ptr->flags( ) &= ~fl_onground;

    // spinning model fix: abs yaw spinning faster than 120 deg/s = adjust layer glitch
    if( state->on_ground && state->velocity_length_xy <= 0.1f && !state->landing && state->last_update_increment > 0.f )
    {
      float delta = std::fabsf( math::normalize( state->abs_yaw - state->abs_yaw_last ) );
      if( ( delta / state->last_update_increment ) > 120.f )
      {
        record->sim_orig.layers [ 3 ].cycle = record->sim_orig.layers [ 3 ].weight = 0.f;
        record->sim_orig.layers [ 3 ].sequence = ptr->get_sequence_activity( 979 );
      }
    }
  }

  // fakelag refine (Kaaba "Refine shot", fixed): re-simulate the choked ticks so the
  // animstate is fed the true velocity instead of the stale networked one.
  // multi-tick from the last record, collide+slide+friction+ground every tick.
  // origin/bones are never touched (networked fact), only the velocity input.
  vector3d saved_abs_velocity{ };
  int saved_eflags = 0;
  bool refined_velocity = false;

  if( ( g_cfg.rage.fakelag_correction == 2 || g_cfg.rage.fakelag_correction == 3 )
    && last_record && !record->dormant && !last_record->dormant
    && !record->broke_lc && !record->fake_walking
    && record->choke >= 2 && record->choke <= 15 )
  {
    vector3d sim_vel = record->anim_velocity.length( true ) > 0.1f ? record->anim_velocity : record->velocity;

    float max_spd = record->max_speed > 1.f ? record->max_speed : 260.f;

    // sanity: teleport-speed velocity = direction changed mid-choke, don't feed garbage
    if( sim_vel.length( true ) <= max_spd * 1.5f )
    {
      saved_abs_velocity = ptr->abs_velocity( );
      saved_eflags = ptr->e_flags( );

      float interval = interfaces::global_vars->interval_per_tick;
      float gravity = cvars::sv_gravity ? cvars::sv_gravity->get_float( ) : 800.f;
      float jump_impulse = cvars::sv_jump_impulse ? cvars::sv_jump_impulse->get_float( ) : 268.f;
      float friction = cvars::sv_friction ? cvars::sv_friction->get_float( ) : 5.2f;
      float stop_speed = cvars::sv_stopspeed ? cvars::sv_stopspeed->get_float( ) : 80.f;

      vector3d origin = last_record->origin;
      int flags = last_record->flags;
      bool was_ground = flags & fl_onground;

      c_trace_filter filter{ };
      filter.skip = ptr;

      for( int t = 0; t < record->choke; ++t )
      {
        bool ground = flags & fl_onground;

        if( !ground )
          sim_vel.z -= gravity * interval;
        else if( !was_ground )
          sim_vel.z = jump_impulse; // left ground inside choke

        was_ground = ground;

        if( ground )
        {
          float speed_2d = sim_vel.length( true );
          if( speed_2d > 0.1f )
          {
            float control = std::max< float >( speed_2d, stop_speed );
            float drop = control * friction * interval;
            float scale = std::max< float >( speed_2d - drop, 0.f ) / speed_2d;
            sim_vel.x *= scale;
            sim_vel.y *= scale;
          }
          else
          {
            sim_vel.x = 0.f;
            sim_vel.y = 0.f;
          }

          float hspeed = sim_vel.length( true );
          float cap = max_spd * 1.1f;
          if( cap > 0.f && hspeed > cap && hspeed > 0.01f )
          {
            float s = cap / hspeed;
            sim_vel.x *= s;
            sim_vel.y *= s;
          }
        }

        vector3d start = origin;
        vector3d end = start + sim_vel * interval;

        c_game_trace trace{ };
        interfaces::engine_trace->trace_ray( ray_t( start, end, record->mins, record->maxs ), mask_playersolid, &filter, &trace );

        if( trace.fraction != 1.f )
        {
          for( int j = 0; j < 2; ++j )
          {
            if( sim_vel.length( false ) < 0.01f )
              break;

            sim_vel -= trace.plane.normal * sim_vel.dot( trace.plane.normal );

            float adjust = sim_vel.dot( trace.plane.normal );
            if( adjust < 0.f )
              sim_vel -= trace.plane.normal * adjust;

            start = trace.end;
            end = start + sim_vel * ( interval * ( 1.f - trace.fraction ) );

            interfaces::engine_trace->trace_ray( ray_t( start, end, record->mins, record->maxs ), mask_playersolid, &filter, &trace );

            if( trace.fraction == 1.f )
              break;
          }
        }

        origin = trace.end;

        vector3d down = origin;
        down.z -= 2.f;

        c_game_trace ground_trace{ };
        interfaces::engine_trace->trace_ray( ray_t( origin, down, record->mins, record->maxs ), mask_playersolid, &filter, &ground_trace );

        flags &= ~fl_onground;
        if( ground_trace.fraction < 1.f && ground_trace.plane.normal.z > 0.7f )
        {
          flags |= fl_onground;
          if( sim_vel.z < 0.f )
            sim_vel.z = 0.f;
        }
      }

      ptr->velocity( ) = sim_vel;
      ptr->abs_velocity( ) = sim_vel;
      ptr->e_flags( ) &= ~0x1000; // EFL_DIRTY_ABSVELOCITY: use our velocity, skip CalcAbsoluteVelocity

      refined_velocity = true;
    }
  }

  this->force_update( );

  ptr->store_poses( record->poses );

  auto old = ptr->get_abs_origin( );
  ptr->set_abs_origin( record->origin );

  // lean fix: layer 12 weight jitters on choke, keep last stable
  if( last_record )
    record->sim_orig.layers [ 12 ].weight = last_record->sim_orig.layers [ 12 ].weight;
  else
    record->sim_orig.layers [ 12 ].weight = 0.f;

  ptr->set_layer( record->sim_orig.layers );
  this->build_bones( record, &record->sim_orig );

  ptr->set_abs_origin( old );

  if( refined_velocity )
  {
    ptr->abs_velocity( ) = saved_abs_velocity;
    ptr->e_flags( ) = saved_eflags;
  }

  restore.restore( ptr );
}

bool records_t::is_valid( )
{
  auto netchan = interfaces::engine->get_net_channel_info( );
  if( !netchan )
    return false;

  if( !valid )
    return false;

  // teammates / no lagcomp: any non-dormant record is usable
  if( !g_ctx.lagcomp )
    return !dormant;

  if( dormant )
    return false;

  if( sim_time <= 0.f )
    return false;

  float time = ( g_ctx.local && g_ctx.local->is_alive( ) ) ? g_ctx.predicted_curtime : interfaces::global_vars->cur_time;
  if( time <= 0.f )
    time = interfaces::global_vars->cur_time;

  float correct = 0.f;
  correct += netchan->get_latency( flow_outgoing );
  correct += netchan->get_latency( flow_incoming );
  correct += g_ctx.lerp_time;

  float max_unlag = 1.f;
  if( cvars::sv_maxunlag )
    max_unlag = cvars::sv_maxunlag->get_float( );
  if( max_unlag <= 0.f )
    max_unlag = 1.f;

  correct = std::clamp< float >( correct, 0.f, max_unlag );

  float delta = correct - ( time - sim_time );

  // 0.2s window is server lagcomp limit (sv_maxunlag). broke-lc teleports stay valid
  // time-wise; extrapolation decides whether to use them directly.
  return std::fabsf( delta ) < 0.2f;
}

void c_animation_fix::anim_player_t::build_bones( records_t* record, records_t::simulated_data_t* sim )
{
  ptr->setup_uninterpolated_bones( sim->bone );
}

void c_animation_fix::anim_player_t::update_animations( )
{
  backup_record.update_record( ptr );

  if( records.size( ) > 0 )
  {
    last_record = &records.front( );

    if( records.size( ) >= 3 )
      old_record = &records [ 2 ];
  }

  auto& record = records.emplace_front( );
  record.update_record( ptr );
  record.update_dormant( dormant_ticks );
  record.update_shot( last_record );

  // choke-cycle tracking for cycle-aware delay shot (EMA of server update gaps)
  if( last_record && !last_record->dormant && record.sim_time > last_record->sim_time )
  {
    float gap = record.sim_time - last_record->sim_time;
    if( gap > 0.f && gap < 1.f )
    {
      if( update_gap_ema <= 0.f )
        update_gap_ema = gap;
      else
        update_gap_ema = update_gap_ema * 0.7f + gap * 0.3f;
    }
  }
  last_recv_time = interfaces::global_vars->cur_time;

  if( dormant_ticks < 1 )
    dormant_ticks++;

  this->update_land( &record );
  this->update_velocity( &record );

  // lby flick flag for resolver (changed since last record)
  if( last_record && !last_record->dormant )
  {
    if( record.lby != last_record->lby )
      record.lby_update = true;
  }

  // extra broke-lc: simtime went backwards or choke exploded (fakelag switch 2<->14)
  if( last_record && !last_record->dormant )
  {
    if( record.sim_time < last_record->sim_time )
      record.broke_lc = true;
    // teleport check already in update_velocity; also flag huge origin jumps as invalid for lagcomp visuals
    vector3d delta = record.origin - last_record->origin;
    if( delta.length( true ) > 128.f && record.choke <= 5 )
      record.broke_lc = true;
  }

  this->simulate_animation_side( &record );

  backup_record.restore( ptr );

  if( g_ctx.lagcomp && last_record && !last_record->dormant )
  {
    // server dropped a tick (simtime backwards) -> this record is out of order, invalidate
    if( last_record->sim_time > record.sim_time )
    {
      next_update_time = record.sim_time + std::fabsf( last_record->sim_time - record.sim_time ) + math::ticks_to_time( 1 );
      record.valid = false;
    }
    else
    {
      // next-update prediction: if server should have sent an update but didn't,
      // this record is extrapolated (fakelag), keep valid but remember
      if( math::time_to_ticks( std::fabsf( next_update_time - record.sim_time ) ) > 17 )
        next_update_time = -1.f;

      if( next_update_time > record.sim_time )
        record.valid = false;
      else
        next_update_time = record.sim_time + math::ticks_to_time( 1 ) * interfaces::global_vars->interval_per_tick;
    }
  }

  const auto records_size = teammate ? 3 : g_ctx.tick_rate;
  while( records.size( ) > records_size )
    records.pop_back( );
}

void thread_anim_update( c_animation_fix::anim_player_t* player )
{
  player->update_animations( );
}

void c_animation_fix::on_net_update_and_render_after( int stage )
{
  if( !g_ctx.in_game || !g_ctx.local || g_ctx.uninject )
    return;

  const std::unique_lock< std::mutex > lock( mutexes::animfix );

  if( !g_ctx.in_game || !g_ctx.local || g_ctx.uninject )
    return;

  auto& players = g_listener_entity->get_entity( ent_player );
  if( players.empty( ) )
    return;

  switch( stage )
  {
  case frame_net_update_postdataupdate_end:
  {
    g_rage_bot->on_pre_predict( );

    for( auto& player : players )
    {
      auto ptr = ( c_csplayer* )player.m_entity;
      if( !ptr )
        continue;

      if( ptr == g_ctx.local )
        continue;

      auto anim_player = this->get_animation_player( ptr->index( ) );
      if( anim_player->ptr != ptr )
      {
        anim_player->reset_data( );
        anim_player->ptr = ptr;
        continue;
      }

      if( !ptr->is_alive( ) )
      {
        if( !anim_player->teammate )
        {
          resolver::reset_info( ptr );
          g_rage_bot->missed_shots [ ptr->index( ) ] = 0;
        }

        anim_player->ptr = nullptr;
        continue;
      }

      if( g_cfg.misc.force_radar && ptr->team( ) != g_ctx.local->team( ) )
        ptr->target_spotted( ) = true;

      if( ptr->dormant( ) )
      {
        anim_player->dormant_ticks = 0;

        if( !anim_player->teammate )
          resolver::reset_info( ptr );
        continue;
      }

      if( ptr->simulation_time( ) == ptr->old_simtime( ) )
        continue;

      auto& layer = ptr->anim_overlay( ) [ 11 ];
      if( layer.cycle == anim_player->old_aliveloop_cycle )
        continue;

      anim_player->old_aliveloop_cycle = layer.cycle;

      auto state = ptr->animstate( );
      if( !state )
        continue;

      if( anim_player->old_spawn_time != ptr->spawn_time( ) )
      {
        state->player = ptr;
        state->reset( );

        anim_player->old_spawn_time = ptr->spawn_time( );
        continue;
      }

#ifdef _DEBUG
      anim_player->update_animations( );
#else
      g_thread_pool->enqueue( thread_anim_update, anim_player );
#endif
    }

#ifndef _DEBUG
    g_thread_pool->wait( );
#endif
  }
  break;
  case frame_render_start:
  {
    for( auto& player : players )
    {
      auto entity = ( c_csplayer* )player.m_entity;
      if( !entity || !entity->is_alive( ) || entity == g_ctx.local )
        continue;

      auto animation_player = this->get_animation_player( entity->index( ) );
      if( !animation_player || animation_player->records.empty( ) || animation_player->dormant_ticks < 1 )
      {
        g_ctx.setup_bones [ entity->index( ) ] = true;
        continue;
      }

      auto first_record = &animation_player->records.front( );
      if( !first_record || !first_record->sim_orig.bone )
        continue;

      std::memcpy( first_record->render_bones, first_record->sim_orig.bone, sizeof( first_record->render_bones ) );
      math::change_matrix_position( first_record->render_bones, 128, first_record->origin, entity->get_render_origin( ) );

      entity->interpolate_moveparent_pos( );

      entity->set_bone_cache( first_record->render_bones );
      entity->attachments_helper( );
    }
  }
  break;
  }
}
