#include "ragebot.h"
#include "../features.h"

#include "../../base/tools/threads.h"

#ifdef _DEBUG
#define DEBUG_LC 0
#define DEBUG_SP 0
void draw_hitbox( c_csplayer* player, matrix3x4_t* bones, int idx, int idx2, bool dur = false )
{
  studio_hdr_t* studio_model = interfaces::model_info->get_studio_model( player->get_model( ) );
  if( !studio_model )
    return;

  mstudio_hitbox_set_t* hitbox_set = studio_model->get_hitbox_set( 0 );
  if( !hitbox_set )
    return;

  for( int i = 0; i < hitbox_set->hitboxes; i++ )
  {
    mstudio_bbox_t* hitbox = hitbox_set->get_hitbox( i );
    if( !hitbox )
      continue;

    vector3d vMin, vMax;
    math::vector_transform( hitbox->bbmin, bones [ hitbox->bone ], vMin );
    math::vector_transform( hitbox->bbmax, bones [ hitbox->bone ], vMax );

    if( hitbox->radius != -1.f )
      interfaces::debug_overlay->add_capsule_overlay( vMin, vMax, hitbox->radius, 255, 255 * idx, 255 * idx2, 150, dur ? interfaces::global_vars->interval_per_tick * 2 : 5.f, 0, 1 );
  }
}
#endif

void c_rage_bot::store( c_csplayer* player )
{
  auto& backup = this->backup [ player->index( ) ];
  backup.duck = player->duck_amount( );
  backup.lby = player->lby( );
  backup.angles = player->eye_angles( );
  backup.origin = player->origin( );
  backup.absorigin = player->get_abs_origin( );
  backup.bbmin = player->bb_mins( );
  backup.bbmax = player->bb_maxs( );
  backup.velocity = player->velocity( );
  player->store_bone_cache( backup.bonecache );
  player->store_poses( backup.poses );

  backup.filled = true;
}

void c_rage_bot::set_record( c_csplayer* player, records_t* record, matrix3x4_t* matrix )
{
  player->invalidate_bone_cache( );

  player->eye_angles( ) = record->eye_angles;

  auto collideable = player->get_collideable( );
  func_ptrs::set_collision_bounds( collideable, &record->mins, &record->maxs );

  player->velocity( ) = record->anim_velocity;

  player->set_poses( record->poses );

  player->origin( ) = record->origin;
  player->set_abs_origin( record->origin );

  player->set_bone_cache( matrix ? matrix : record->sim_orig.bone );
}

void c_rage_bot::restore( c_csplayer* player )
{
  auto& backup = this->backup [ player->index( ) ];
  if( !backup.filled )
    return;

  player->invalidate_bone_cache( );

  player->eye_angles( ) = backup.angles;

  auto collideable = player->get_collideable( );
  func_ptrs::set_collision_bounds( collideable, &backup.bbmin, &backup.bbmax );

  player->set_poses( backup.poses );

  player->velocity( ) = backup.velocity;

  player->origin( ) = backup.origin;
  player->set_abs_origin( backup.origin );

  player->set_bone_cache( backup.bonecache );
}

std::vector< int > c_rage_bot::get_hitboxes( )
{
  std::vector< int > hitboxes{ };

  if( g_ctx.weapon->is_taser( ) )
  {
    hitboxes.emplace_back( ( int )hitbox_stomach );
    hitboxes.emplace_back( ( int )hitbox_pelvis );
    return hitboxes;
  }

  if( cheat_tools::get_weapon_config( ).hitboxes & head )
    hitboxes.emplace_back( ( int )hitbox_head );

  if( cheat_tools::get_weapon_config( ).hitboxes & chest )
  {
    hitboxes.emplace_back( ( int )hitbox_chest );
    hitboxes.emplace_back( ( int )hitbox_lower_chest );
  }

  if( cheat_tools::get_weapon_config( ).hitboxes & stomach )
    hitboxes.emplace_back( ( int )hitbox_stomach );

  if( cheat_tools::get_weapon_config( ).hitboxes & pelvis )
    hitboxes.emplace_back( ( int )hitbox_pelvis );

  if( cheat_tools::get_weapon_config( ).hitboxes & arms_ )
  {
    hitboxes.emplace_back( ( int )hitbox_left_upper_arm );
    hitboxes.emplace_back( ( int )hitbox_right_upper_arm );
  }

  if( cheat_tools::get_weapon_config( ).hitboxes & legs )
  {
    hitboxes.emplace_back( ( int )hitbox_left_thigh );
    hitboxes.emplace_back( ( int )hitbox_right_thigh );

    hitboxes.emplace_back( ( int )hitbox_left_calf );
    hitboxes.emplace_back( ( int )hitbox_right_calf );

    hitboxes.emplace_back( ( int )hitbox_left_foot );
    hitboxes.emplace_back( ( int )hitbox_right_foot );
  }

  return hitboxes;
}

int c_rage_bot::get_min_damage( c_csplayer* player )
{
  const auto& weapon_cfg = cheat_tools::get_weapon_config( );

  int health = player->health( );

  int menu_damage = g_cfg.binds [ override_dmg_b ].toggled ? weapon_cfg.damage_override : weapon_cfg.mindamage;

  // hp + 1 slider
  if( menu_damage >= 100 )
    return health + ( menu_damage - 100 );

  return menu_damage;
}

bool c_rage_bot::should_stop( bool shoot_check )
{
  if( !( cheat_tools::get_weapon_config( ).quick_stop ) )
    return false;

  if( g_ctx.weapon->is_misc_weapon( ) )
    return false;

  if( !g_utils->on_ground( ) )
    return false;

  if( g_ctx.local->velocity( ).length( true ) < 1.f )
    return false;

  if( g_cfg.binds [ sw_b ].toggled )
    return false;

  bool able_to_shoot = g_utils->is_able_to_shoot( true );

  if( shoot_check )
  {
    bool between_shots_ = cheat_tools::get_weapon_config( ).quick_stop_options & between_shots;
    if( !able_to_shoot )
      return between_shots_;
  }

  return able_to_shoot;
}

void c_rage_bot::start_stop( )
{
  if( !g_ctx.weapon || !stopping )
    return;

  bool weapon_for_tp = g_ctx.weapon->is_pistols( ) && !g_ctx.weapon->is_heavy_pistols( ) && g_ctx.weapon->item_definition_index( ) == weapon_awp || g_ctx.weapon->item_definition_index( ) == weapon_ssg08;

  if( weapon_for_tp && g_exploits->cl_move.shift && !g_utils->is_able_to_shoot( true ) )
    return;

  if( this->should_stop( ) )
    force_accuracy = this->auto_stop( );
}

bool c_rage_bot::auto_stop( )
{
  auto velocity = g_ctx.local->velocity( );
  float raw_speed = velocity.length( true );

  int max_speed = ( int )g_movement->get_max_speed( ) * 0.34f;
  int speed = ( int )raw_speed;

  if( speed <= max_speed )
  {
    g_movement->force_speed( g_movement->get_max_speed( ) * 0.34f );
    return true;
  }

  g_movement->force_stop( );

  return !( cheat_tools::get_weapon_config( ).quick_stop_options & 4 );
}

bool c_rage_bot::knife_is_behind( records_t* record )
{
  vector3d delta{ record->origin - g_ctx.eye_position };
  delta.z = 0.f;
  delta = delta.normalized( );

  vector3d target;
  math::angle_to_vectors( record->abs_angles, target );
  target.z = 0.f;

  return delta.dot( target ) > 0.475f;
}

void c_rage_bot::knife_bot( )
{
  if( !g_cfg.rage.enable )
    return;

  if( !g_ctx.weapon->is_knife( ) )
    return;

  if( g_ctx.predicted_curtime < g_ctx.weapon->next_primary_attack( ) || g_ctx.predicted_curtime < g_ctx.weapon->next_secondary_attack( ) )
    return;

  auto nearest_target = g_anti_aim->get_closest_player( true );
  if( !nearest_target )
    return;

  auto last = g_animation_fix->get_latest_record( nearest_target );
  if( !last )
    return;

  auto position = nearest_target->get_hitbox_position( hitbox_stomach );
  if( g_ctx.eye_position.dist_to( position ) > 65.f )
    return;

  knife_point_t best{ };

  auto get_record_damage = [ & ]( records_t* record )
  {
    this->store( nearest_target );
    this->set_record( nearest_target, record );

    auto awall = g_auto_wall->fire_bullet( g_ctx.local, nearest_target, g_ctx.weapon_info, g_ctx.weapon->is_taser( ), g_ctx.eye_position, nearest_target->get_hitbox_position( hitbox_stomach ) );

    this->restore( nearest_target );

    if( awall.remaining_pen < 4 )
      return 0;

    return awall.dmg;
  };

  auto last_damage = get_record_damage( last );
  best.point = nearest_target->get_hitbox_position( hitbox_stomach );
  best.record = last;
  best.damage = last_damage;

  if( g_ctx.lagcomp )
  {
    auto old = g_animation_fix->get_oldest_record( nearest_target );
    if( old && old != last )
    {
      auto old_damage = get_record_damage( old );
      if( old_damage > last_damage )
      {
        best.point = nearest_target->get_hitbox_position( hitbox_stomach );
        best.record = old;
        best.damage = old_damage;
      }
    }
  }

  if( best.record )
  {
    bool stab = false;
    bool armor = best.record->ptr->armor_value( ) > 0;
    bool first = g_ctx.weapon->next_primary_attack( ) + 0.4f < g_ctx.predicted_curtime;
    bool back = this->knife_is_behind( best.record );

    int stab_dmg = knife_dmg.stab [ armor ][ back ];
    int slash_dmg = knife_dmg.swing [ first ][ armor ][ back ];
    int swing_dmg = knife_dmg.swing [ false ][ armor ][ back ];

    int health = best.record->ptr->health( );
    if( health <= slash_dmg )
      stab = false;
    else if( health <= stab_dmg )
      stab = true;
    else if( health > ( slash_dmg + swing_dmg + stab_dmg ) )
      stab = true;
    else
      stab = false;

    g_ctx.cmd->viewangles = math::normalize( math::angle_from_vectors( g_ctx.eye_position, best.point ), true );

    if( g_ctx.lagcomp )
      g_ctx.cmd->tickcount = math::time_to_ticks( best.record->sim_time + g_ctx.lerp_time );

    g_ctx.cmd->buttons |= stab ? in_attack2 : in_attack;
  }
}

std::vector< int > backtrack_hitboxes{
  hitbox_head,
  hitbox_chest,
  hitbox_pelvis,
  hitbox_stomach,
  hitbox_right_foot,
  hitbox_left_foot,
};

int get_record_damage( c_csplayer* player, records_t* record )
{
  int total_dmg = 0;

  g_rage_bot->store( player );
  g_rage_bot->set_record( player, record );

  for( auto& hitbox : backtrack_hitboxes )
  {
    auto position = player->get_hitbox_position( hitbox, record->sim_orig.bone );

    auto awall = g_auto_wall->fire_bullet( g_ctx.local, player, g_ctx.weapon_info, g_ctx.weapon->is_taser( ), g_ctx.eye_position, position );

    total_dmg += awall.dmg;
  }

  g_rage_bot->restore( player );

  return total_dmg;
}

// full lagcomp scan: all valid records, score = damage + mode bonuses.
// prefers lby flick / moving / shot records (2018 jre/supremacy FindIdealRecord idea),
// falls back to highest damage. broke-lc records are penalized unless nothing else.
static int score_record( c_csplayer* player, records_t* record )
{
  if( !record || record->dormant )
    return INT_MIN;

  int dmg = get_record_damage( player, record );
  if( dmg <= 0 )
    return INT_MIN;

  int score = dmg;

  // ideal-record bonuses (like jre FindIdealRecord)
  if( record->lby_update )
    score += 25;
  if( record->shooting && record->choke <= 2 )
    score += 20;
  if( record->velocity.length( true ) > 0.1f && !record->fake_walking )
    score += 10;

  // enemy duck transition: hitbox bounds mid-lerp, server/client disagree
  if( record->duck_amount > 0.05f && record->duck_amount < 0.95f )
    score -= 8;

  // resolver mode bonus: if this record matches current resolved mode, prefer it
  // (e.g. we resolved lby flick -> records with lby flick are ideal)
  auto& res_info = resolver::info [ player->index( ) ];
  if( res_info.valid )
  {
    if( res_info.mode_id == resolver_mode_id_t::lby_flick && record->lby_update )
      score += 15;
    else if( res_info.mode_id == resolver_mode_id_t::moving && record->velocity.length( true ) > 0.1f )
      score += 10;
  }

  // broke lc penalty: teleport records are lagcomp-dead, only use if desperate
  if( record->broke_lc )
    score -= 50;

  return score;
}

// top-n records by score, newest wins ties (less extrapolation error).
// multipoint-scanning 2 records instead of 1 is the biggest hitrate gain
// vs movers/fakelaggers: the newest record may hold a turned head while
// the previous one still has the hittable angle (or vice versa).
std::vector< records_t* > get_top_records( c_csplayer* player, int n )
{
  auto all = g_animation_fix->get_all_records( player );
  if( all.empty( ) )
  {
    auto latest = g_animation_fix->get_latest_record( player );
    return latest ? std::vector< records_t* >{ latest } : std::vector< records_t* >{ };
  }

  if( !g_ctx.lagcomp )
  {
    for( auto* r : all )
      if( r && !r->dormant )
        return std::vector< records_t* >{ r };
    return { };
  }

  // newest-first order kept for tiebreaks via stable sort
  std::vector< std::pair< records_t*, int > > scored{ };
  scored.reserve( all.size( ) );

  for( auto* r : all )
  {
    int s = score_record( player, r );
    if( s != INT_MIN )
      scored.emplace_back( r, s );
  }

  // nothing damageable (all 0 dmg) -> fall back to latest valid for scan
  if( scored.empty( ) )
  {
    for( auto* r : all )
      if( r && !r->dormant )
        return std::vector< records_t* >{ r };
    return { };
  }

  std::stable_sort( scored.begin( ), scored.end( ),
    [ ]( const auto& a, const auto& b ) { return a.second > b.second; } );

  std::vector< records_t* > out{ };
  for( int i = 0; i < ( int )scored.size( ) && ( int )out.size( ) < n; ++i )
    out.emplace_back( scored [ i ].first );

  return out;
}

records_t* get_best_record( c_csplayer* player )
{
  auto top = get_top_records( player, 1 );
  return top.empty( ) ? nullptr : top [ 0 ];
}

bool is_point_predictive( c_csplayer* player, point_t& point )
{
  if( !g_rage_bot->should_stop( ) || !( cheat_tools::get_weapon_config( ).quick_stop_options & early ) )
    return false;

  int dmg = g_rage_bot->get_min_damage( player );
  auto& esp_info = g_esp_store->playerinfo [ player->index( ) ];
  if( !esp_info.valid )
    return false;

  float speed = std::max< float >( g_engine_prediction->unprediced_velocity.length( true ), 1.f );

  int max_stop_ticks = std::max< int >( ( ( speed / g_movement->get_max_speed( ) ) * 7.f ) - 1, 0 );
  if( max_stop_ticks == 0 )
    return false;

  vector3d last_predicted_velocity = g_engine_prediction->unprediced_velocity;
  for( int i = 0; i < max_stop_ticks; ++i )
  {
    auto pred_velocity = g_engine_prediction->unprediced_velocity * math::ticks_to_time( i + 1 );

    vector3d origin = g_ctx.eye_position + pred_velocity;
    int flags = g_ctx.local->flags( );

    g_utils->extrapolate( g_ctx.local, origin, pred_velocity, flags, flags & fl_onground );

    last_predicted_velocity = pred_velocity;
  }

  auto predicted_eye_pos = g_ctx.eye_position + last_predicted_velocity;

  if( player->dormant( ) )
  {
    vector3d poses [ 3 ]{ player->get_abs_origin( ), player->get_abs_origin( ) + player->view_offset( ), player->get_abs_origin( ) + vector3d( 0.f, 0.f, player->view_offset( ).z / 2.f ) };

    for( int i = 0; i < 3; ++i )
    {
      c_trace_filter filter{ };
      filter.skip = g_ctx.local;

      c_game_trace out{ };
      interfaces::engine_trace->trace_ray( ray_t( predicted_eye_pos, poses [ i ] ), mask_shot_hull | contents_hitbox, &filter, &out );

      if( out.fraction >= 0.97f )
        return true;
      else
        continue;
    }
  }
  else
    return g_auto_wall->can_hit_point( player, point.position, predicted_eye_pos, dmg );

  return false;
}

void force_scope( )
{
  bool able_to_zoom = g_ctx.predicted_curtime >= g_ctx.weapon->next_secondary_attack( );

  if( able_to_zoom && cheat_tools::get_weapon_config( ).auto_scope && g_ctx.weapon->zoom_level( ) < 1 && g_utils->on_ground( ) && g_ctx.weapon->is_sniper( ) )
    g_ctx.cmd->buttons |= in_attack2;
}

void thread_build_points( aim_cache_t* aim_cache )
{
  aim_cache->points.clear( );

  if( !aim_cache->player )
    return;

  // scan the 2 best records, not 1: newest may hold a turned head while the
  // previous one still has the hittable angle (movers, fakelaggers, flickers).
  // each point keeps its own record, fire/backtrack stay per-point correct.
  auto top_records = get_top_records( aim_cache->player, 2 );
  if( top_records.empty( ) )
    return;

  for( auto* best_record : top_records )
  {
    if( !best_record )
      continue;

    g_rage_bot->store( aim_cache->player );
    g_rage_bot->set_record( aim_cache->player, best_record );

    for( auto& hitbox : g_rage_bot->get_hitboxes( ) )
    {
      const auto& pts = cheat_tools::get_multipoints( aim_cache->player, hitbox, best_record->sim_orig.bone );
      for( auto& p : pts )
      {
        auto awall = g_auto_wall->fire_bullet( g_ctx.local, aim_cache->player, g_ctx.weapon_info, g_ctx.weapon->is_taser( ), g_ctx.eye_position, p.first );

        // interfaces::debug_overlay->add_text_overlay(p.first, interfaces::global_vars->interval_per_tick * 2.f, "%d", awall.dmg);

        auto new_point = point_t( hitbox, p.second, awall.dmg, best_record, p.first );

        if( p.second )
          new_point.predictive = is_point_predictive( aim_cache->player, new_point );

#ifdef _DEBUG
        if( g_rage_bot->debug_aimbot )
        {
          interfaces::debug_overlay->add_box_overlay(
            new_point.position, vector3d( -1, -1, -1 ), vector3d( 1, 1, 1 ), { }, 255, new_point.center ? 255 : 0, new_point.center ? 255 : 0, 200, interfaces::global_vars->interval_per_tick * 2.f );

          interfaces::debug_overlay->add_text_overlay( new_point.position, interfaces::global_vars->interval_per_tick * 2.f, "%d", new_point.damage );
        }
#endif

        aim_cache->points.emplace_back( new_point );
      }
    }

    g_rage_bot->restore( aim_cache->player );
  }
}

void thread_get_best_point( aim_cache_t* aim_cache )
{
  if( !aim_cache->player )
    return;

  int health = aim_cache->player->health( );
  int lethal_dmg = g_rage_bot->can_dt( ) ? health / 2 : health;

  aim_cache->best_point.reset( );

  if( aim_cache->points.empty( ) )
    return;

  int dmg = g_rage_bot->get_min_damage( aim_cache->player );

  // prepare points in right order
  // because we need to prefer the best points to tap enemy
  std::sort( aim_cache->points.begin( ), aim_cache->points.end( ), [ & ]( point_t& a, point_t& b ) { return a.center > b.center; } );
  std::sort( aim_cache->points.begin( ), aim_cache->points.end( ), [ & ]( point_t& a, point_t& b ) { return a.damage > b.damage; } );

  // auto bodyaim: after N resolver misses the head is a lottery ticket.
  // big center-mass points don't care about exact yaw - trade headshots for hits.
  // two passes so a bad body never blocks a good head when nothing else qualifies.
  int baim_after = g_cfg.rage.auto_baim_misses;
  int misses = ( baim_after > 0 && aim_cache->player ) ? g_rage_bot->missed_shots [ aim_cache->player->index( ) ] : 0;
  bool force_baim = baim_after > 0 && misses >= baim_after;

  auto select_prefered_point = [ & ]( bool skip_head ) -> point_t
  {
    point_t best{ };
    for( auto& point : aim_cache->points )
    {
      // force stop before peek
      if( point.predictive )
      {
        force_scope( );

        if( g_rage_bot->should_stop( ) )
        {
          g_rage_bot->auto_stop( );
        }

        point.predictive = false;
      }

      if( skip_head && point.hitbox == hitbox_head )
        continue;

      if( point.damage < dmg || !point.body && g_cfg.binds [ force_body_b ].toggled )
        continue;

      // prefer lethal enemies
      else if( point.body && point.center && point.damage >= lethal_dmg )
        return point;

      // choose by best dmg
      else
      {
        if( point.damage > best.damage )
          best = point;
      }
    }

    return best;
  };

  aim_cache->best_point = select_prefered_point( force_baim );
  if( force_baim && !aim_cache->best_point.filled )
    aim_cache->best_point = select_prefered_point( false );
}

void c_rage_bot::proceed_aimbot( )
{
  const std::unique_lock< std::mutex > lock( mutexes::rage );

#ifdef _DEBUG
  if( cheat_tools::debug_hitchance )
  {
    cheat_tools::spread_point.reset( );
    cheat_tools::current_spread = 0.f;
    cheat_tools::spread_points.clear( );
  }
#endif

  target = nullptr;
  working = false;
  stopping = false;
  reset_data = false;
  force_accuracy = true;

  if( !g_ctx.weapon || interfaces::game_rules->is_freeze_time( ) || g_ctx.local->flags( ) & fl_frozen || g_ctx.local->gun_game_immunity( ) )
  {
    return;
  }

  this->knife_bot( );

  bool invalid_weapon = g_ctx.weapon->is_misc_weapon( ) && !g_ctx.weapon->is_taser( );

  if( !g_cfg.rage.enable || invalid_weapon )
  {
    if( reset_scan_data )
    {
      for( auto& b : backup )
        b.reset( );

      target = nullptr;
      reset_scan_data = false;
    }

    return;
  }

  reset_scan_data = true;

  float hitchance = std::clamp( cheat_tools::get_weapon_config( ).hitchance / 100.f, 0.f, 1.f );

  auto& players = g_listener_entity->get_entity( ent_player );
  if( players.empty( ) )
    return;

  point_t best_point{ };

  int index_iter = 0;
  for( auto& player : players )
  {
    auto entity = ( c_csplayer* )player.m_entity;
    if( !entity )
      continue;

    if( entity == g_ctx.local || entity->team( ) == g_ctx.local->team( ) )
      continue;

    auto& cache = aim_cache [ entity->index( ) ];

    if( !entity->is_alive( ) || entity->dormant( ) || entity->gun_game_immunity( ) )
    {
      target = nullptr;

      if( !cache.points.empty( ) )
        cache.points.clear( );

      if( cache.best_point.filled )
        cache.best_point.reset( );

      if( cache.player )
        cache.player = nullptr;

      continue;
    }

    ++index_iter;

    cache.player = entity;

#ifdef _DEBUG
    thread_build_points( &cache );
#else
    g_thread_pool->enqueue( thread_build_points, &cache );
#endif
  }

  if( index_iter < 1 )
    return;

#ifndef _DEBUG
  g_thread_pool->wait( );
#endif

  for( auto& player : players )
  {
    auto entity = ( c_csplayer* )player.m_entity;
    if( !entity || entity == g_ctx.local )
      continue;

    if( entity->team( ) == g_ctx.local->team( ) || !entity->is_alive( ) || entity->dormant( ) || entity->gun_game_immunity( ) )
      continue;

    auto& cache = aim_cache [ entity->index( ) ];
    if( !cache.player || cache.player != entity )
      continue;

    thread_get_best_point( &cache );
  }

  int highest_damage = INT_MIN;

  should_slide = false;

  for( auto& player : players )
  {
    auto entity = ( c_csplayer* )player.m_entity;
    if( !entity || entity == g_ctx.local )
      continue;

    if( entity->team( ) == g_ctx.local->team( ) || !entity->is_alive( ) || entity->dormant( ) || entity->gun_game_immunity( ) )
      continue;

    auto cache = &aim_cache [ entity->index( ) ];
    if( !cache || !cache->player || cache->player != entity )
      continue;

    if( !cache->best_point.filled )
    {
      this->restore( entity );
      continue;
    }

    if( highest_damage < cache->best_point.damage )
    {
      target            = entity;
      highest_damage    = cache->best_point.damage;
    }
  }

  if( target )
    best_point = aim_cache [ target->index( ) ].best_point;

  if( best_point.filled )
  {
    working = true;
    stopping = true;

    force_scope( );

    // Delay untill unlag (reviewed: KEEP, it's high-EV): hold fire while OUR choke > 2
    // so the shot leaves on a fresh tick and the server processes it immediately -
    // a choked shot arrives up to a full fakelag cycle late and movers walk away from it.
    // refined: standing targets don't run away, no reason to wait for unlag (saves DPS).
    bool shoot_on_unlag = true;
    if( !g_cfg.binds [ sw_b ].toggled && g_cfg.rage.delay_shot && interfaces::client_state->choked_commands > 2 )
    {
      bool target_moving = best_point.record && best_point.record->velocity.length( true ) > 10.f && !best_point.record->fake_walking;
      if( target_moving )
        shoot_on_unlag = false;
    }

    // fakelag correction (Kaaba idea, fixed): broke_lc = teleport >64u between updates,
    // the server can't lagcomp it (see server player_lagcompensation.cpp), so backtracking
    // to it is a guaranteed miss. delay waits/switches, refine shoots extrapolated server pos.
    bool hold_shot = false;
    bool skip_backtrack = false;
    records_t ex_rec{ };
    bool use_ex = false;

    int corr = std::clamp( g_cfg.rage.fakelag_correction, 0, 3 );
    if( corr > 0 && best_point.record && best_point.record->broke_lc )
    {
      bool use_delay = ( corr == 1 || corr == 3 );
      bool use_refine = ( corr == 2 || corr == 3 );

      if( use_refine )
        skip_backtrack = true;

      if( use_delay )
      {
        // lethal justifies the risk, shoot anyway
        if( best_point.damage < target->health( ) )
        {
          // newest clean record that still meets min damage wins over a broke one
          int mindmg = this->get_min_damage( target );
          records_t* clean = nullptr;
          vector3d clean_pos{ };
          int clean_dmg = 0;

          auto all = g_animation_fix->get_all_records( target );
          for( auto* r : all )
          {
            if( !r || r == best_point.record || r->dormant || r->broke_lc || !r->valid )
              continue;

            this->store( target );
            this->set_record( target, r );

            vector3d pos = target->get_hitbox_position( best_point.hitbox, r->sim_orig.bone );
            int dmg = g_auto_wall->fire_bullet( g_ctx.local, target, g_ctx.weapon_info, g_ctx.weapon->is_taser( ), g_ctx.eye_position, pos ).dmg;

            this->restore( target );

            if( dmg >= mindmg )
            {
              clean = r;
              clean_pos = pos;
              clean_dmg = dmg;
              break; // newest-first
            }
          }

          if( clean )
          {
            best_point.record = clean;
            best_point.position = clean_pos;
            best_point.damage = clean_dmg;
            skip_backtrack = false;
            broke_holds [ target->index( ) ] = 0;
          }
          else
          {
            // no clean option: hold only while the next update is actually imminent
            // (cycle-aware, UC-validated). stale/jitter cycles: shooting now is as good as it gets.
            // holds are capped so a permanent breaker can't mute us forever.
            int idx = target->index( );
            int ticks_to_update = g_animation_fix->predict_ticks_to_update( target );

            if( ticks_to_update <= 4 && broke_holds [ idx ] < 6 )
            {
              hold_shot = true;
              broke_holds [ idx ]++;
            }
            else
              broke_holds [ idx ] = 0;
          }
        }
      }
    }
    else if( corr > 0 && best_point.record )
      broke_holds [ target->index( ) ] = 0;

    // refine: broke record + no backtrack = server uses current pos, but our shot lands
    // ping later. shift resolved bones forward by round-trip ticks (Gladiatorcheatz-style
    // FullWalkMove-on-break). velocity here is the sane carried one, not teleport garbage.
    if( skip_backtrack && best_point.record && best_point.record->broke_lc && g_target_extrapolation )
    {
      int ping_ticks = 2;
      auto netchan = interfaces::engine->get_net_channel_info( );
      if( netchan )
      {
        float ping = netchan->get_latency( flow_outgoing ) + netchan->get_latency( flow_incoming );
        ping_ticks = std::clamp( math::time_to_ticks( ping ) + 1, 1, 6 );
      }

      records_t tmp = *best_point.record;
      if( g_target_extrapolation->extrapolate_record( target, best_point.record, &tmp, 0, false, ping_ticks ) )
      {
        this->store( target );
        this->set_record( target, &tmp );

        vector3d pos = target->get_hitbox_position( best_point.hitbox, tmp.sim_orig.bone );
        int dmg = g_auto_wall->fire_bullet( g_ctx.local, target, g_ctx.weapon_info, g_ctx.weapon->is_taser( ), g_ctx.eye_position, pos ).dmg;

        this->restore( target );

        // shifted pos must still be worth it, otherwise hold instead of wasting
        if( dmg >= this->get_min_damage( target ) && dmg > 0 )
        {
          ex_rec = tmp;
          use_ex = true;
          best_point.record = &ex_rec;
          best_point.position = pos;
          best_point.damage = dmg;
        }
        else
          hold_shot = true;
      }
    }

    // delay shot on unduck (Kaaba idea, fixed state): hitboxes + spread lie mid-transition
    if( g_cfg.rage.delay_unduck && g_ctx.local )
    {
      float duck = g_ctx.local->duck_amount( );
      if( prev_duck_amount >= 0.f && duck < prev_duck_amount - 0.01f && duck > 0.05f && duck < 0.95f )
        hold_shot = true;
      prev_duck_amount = duck;
    }

    // broke/no-backtrack shots aim at an estimated pos: demand extra hitchance (UC: subtick
    // bone error from a wrong lerp/time base alone moves legs enough to miss)
    float hc_needed = hitchance + ( skip_backtrack ? 0.08f : 0.f );
    if( hc_needed > 1.f )
      hc_needed = 1.f;

    if( shoot_on_unlag && !hold_shot && force_accuracy && cheat_tools::is_accuracy_valid( target, best_point, hc_needed, &best_point.hitchance ) )
    {
      if( g_utils->is_able_to_shoot( true ) )
      {
        if( g_cfg.rage.auto_fire )
        {
          if( !g_anti_aim->is_fake_ducking( ) )
          {
            if( g_cfg.binds [ hs_b ].toggled )
              *g_ctx.send_packet = true;
            else
            {
              if( !interfaces::client_state->choked_commands )
                *g_ctx.send_packet = false;
            }
          }

          g_ctx.cmd->buttons |= in_attack;
        }

        // interfaces::engine->set_view_angles(g_ctx.cmd->viewangles);

        if( g_ctx.cmd->buttons & in_attack )
        {
          firing = true;
          broke_holds [ target->index( ) ] = 0;

          if( g_ctx.lagcomp && !skip_backtrack )
            g_ctx.cmd->tickcount = math::time_to_ticks( best_point.record->sim_time + g_ctx.lerp_time );

          g_ctx.cmd->viewangles = math::normalize( math::angle_from_vectors( g_ctx.eye_position, best_point.position ), true );
          g_ctx.cmd->viewangles -= g_ctx.local->aim_punch_angle( ) * cvars::weapon_recoil_scale->get_float( );

          g_ctx.cmd->viewangles = math::normalize( g_ctx.cmd->viewangles, true );

          g_ctx.shot_cmd = g_ctx.cmd->command_number;
          g_ctx.last_shoot_position = g_ctx.eye_position;

          if( g_cfg.visuals.chams [ c_onshot ].enable )
            g_chams->add_shot_record( target, best_point.record->sim_orig.bone );
#ifdef _DEBUG
#if DEBUG_LC
          draw_hitbox( target, best_point.record->sim_orig.bone, 0, 0, false );
#endif

#if DEBUG_SP
          draw_hitbox( target, best_point.record->sim_left.bone, 0, 0, false );
          draw_hitbox( target, best_point.record->sim_right.bone, 1, 0, false );
          draw_hitbox( target, best_point.record->sim_zero.bone, 0, 1, false );
#endif
#endif

          this->add_shot_record( target, best_point );

          this->restore( target );
        }
      }
    }
  }
}

void c_rage_bot::on_predict_start( )
{
  auto& players = g_listener_entity->get_entity( ent_player );
  if( players.empty( ) )
    return;

  for( auto& player : players )
  {
    auto entity = ( c_csplayer* )player.m_entity;
    if( !entity )
      continue;

    if( entity == g_ctx.local || entity->team( ) == g_ctx.local->team( ) )
      continue;

    if( !entity->is_alive( ) || entity->dormant( ) )
      continue;

    this->store( entity );
  }

  this->proceed_aimbot( );

  for( auto& player : players )
  {
    auto entity = ( c_csplayer* )player.m_entity;
    if( !entity )
      continue;

    if( entity == g_ctx.local || entity->team( ) == g_ctx.local->team( ) )
      continue;

    if( !entity->is_alive( ) || entity->dormant( ) )
      continue;

    this->restore( entity );
  }
}

void c_rage_bot::on_local_death( )
{
  if( reset_data )
    return;

  for( auto& m : missed_shots )
    m = 0;

  prev_duck_amount = -1.f;
  broke_holds.fill( 0 );

  reset_data = true;
}

void c_rage_bot::on_changed_map( )
{
  for( auto& b : backup )
    b.reset( );

  prev_duck_amount = -1.f;
  broke_holds.fill( 0 );
}