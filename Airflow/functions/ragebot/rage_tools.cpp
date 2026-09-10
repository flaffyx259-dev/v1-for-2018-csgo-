#include "rage_tools.h"
#include "ragebot.h"
#include "autowall.h"
#include "engine_prediction.h"

#include "../features.h"

namespace cheat_tools
{
  constexpr int max_traces = 48;

  weapon_config_t zeus_config = { true, false, false, 40, 100, 100, 80, 80, 0, 0, 0, 1, 0, { } };

  weapon_config_t get_weapon_config( )
  {
    if( !g_ctx.local || !g_ctx.local->is_alive( ) )
      return { };

    if( !g_ctx.weapon )
      return { };

    if( g_cfg.rage.weapon [ auto_snipers ].enable && g_ctx.weapon->is_auto_sniper( ) )
      return g_cfg.rage.weapon [ auto_snipers ];
    else if( g_cfg.rage.weapon [ heavy_pistols ].enable && g_ctx.weapon->is_heavy_pistols( ) )
      return g_cfg.rage.weapon [ heavy_pistols ];
    else if( g_cfg.rage.weapon [ pistols ].enable && g_ctx.weapon->is_pistols( ) )
      return g_cfg.rage.weapon [ pistols ];
    else if( g_cfg.rage.weapon [ scout ].enable && g_ctx.weapon->item_definition_index( ) == weapon_ssg08 )
      return g_cfg.rage.weapon [ scout ];
    else if( g_cfg.rage.weapon [ awp ].enable && g_ctx.weapon->item_definition_index( ) == weapon_awp )
      return g_cfg.rage.weapon [ awp ];
    else if( g_ctx.weapon->is_taser( ) )
      return zeus_config;

    return g_cfg.rage.weapon [ global ];
  }

  std::string hitbox_to_string( int id )
  {
    switch( id )
    {
    case 0:
      return xor_c( "head" );
      break;
    case 1:
      return xor_c( "neck" );
      break;
    case 2:
      return xor_c( "pelvis" );
      break;
    case 3:
      return xor_c( "stomach" );
      break;
    case 4:
      return xor_c( "lower chest" );
      break;
    case 5:
      return xor_c( "chest" );
      break;
    case 6:
      return xor_c( "upper chest" );
      break;
    case 7:
      return xor_c( "right thigh" );
      break;
    case 8:
      return xor_c( "left thigh" );
      break;
    case 9:
      return xor_c( "right leg" );
      break;
    case 10:
      return xor_c( "left leg" );
      break;
    case 11:
      return xor_c( "left foot" );
      break;
    case 12:
      return xor_c( "right foot" );
      break;
    case 13:
      return xor_c( "right hand" );
      break;
    case 14:
      return xor_c( "left hand" );
      break;
    case 15:
      return xor_c( "right upper arm" );
      break;
    case 16:
      return xor_c( "right lower arm" );
      break;
    case 17:
      return xor_c( "left upper arm" );
      break;
    case 18:
      return xor_c( "left lower arm" );
      break;
    }
  }

  std::string hitgroup_to_string( int hitgroup )
  {
    switch( hitgroup )
    {
    case hitgroup_generic:
      return xor_c( "generic" );
      break;
    case hitgroup_head:
      return xor_c( "head" );
      break;
    case hitgroup_chest:
      return xor_c( "chest" );
      break;
    case hitgroup_stomach:
      return xor_c( "body" );
      break;
    case hitgroup_leftarm:
      return xor_c( "leftarm" );
      break;
    case hitgroup_rightarm:
      return xor_c( "rightarm" );
      break;
    case hitgroup_leftleg:
      return xor_c( "leftleg" );
      break;
    case hitgroup_rightleg:
      return xor_c( "rightleg" );
      break;
    case hitgroup_gear:
      return xor_c( "gear" );
      break;
    case hitgroup_neck:
      return xor_c( "neck" );
      break;
    default:
      return xor_c( "unknown" );
    }
  }

  int hitbox_to_hitgroup( int hitbox )
  {
    switch( hitbox )
    {
    case hitbox_head:
    case hitbox_neck:
      return hitgroup_head;
      break;
    case hitbox_pelvis:
    case hitbox_stomach:
      return hitgroup_stomach;
      break;
    case hitbox_lower_chest:
    case hitbox_chest:
    case hitbox_upper_chest:
      return hitgroup_chest;
      break;
    case hitbox_left_thigh:
    case hitbox_left_calf:
    case hitbox_left_foot:
      return hitgroup_leftleg;
      break;
    case hitbox_right_thigh:
    case hitbox_right_calf:
    case hitbox_right_foot:
      return hitgroup_rightleg;
      break;
    case hitbox_left_hand:
    case hitbox_left_upper_arm:
    case hitbox_left_forearm:
      return hitgroup_leftarm;
      break;
    case hitbox_right_hand:
    case hitbox_right_upper_arm:
    case hitbox_right_forearm:
      return hitgroup_rightarm;
      break;
    default:
      return hitgroup_generic;
      break;
    }
  }

  constexpr int total_seeds = 255;

  vector2d calc_spread_angle( int bullets, float recoil_index, int i )
  {
    auto index = g_ctx.weapon->item_definition_index( );

    math::random_seed( i + 1u );

    auto v1 = math::random_float( 0.f, 1.f );
    auto v2 = math::random_float( 0.f, M_PI * 2.f );

    float v3{ }, v4{ };
    if( cvars::weapon_accuracy_shotgun_spread_patterns->get_int( ) > 0 )
      func_ptrs::calc_shotgun_spread( index, 0, static_cast< int >( bullets * recoil_index ), &v4, &v3 );
    else
    {
      v3 = math::random_float( 0.f, 1.f );
      v4 = math::random_float( 0.f, M_PI * 2.f );
    }

    if( recoil_index < 3.f && index == weapon_negev )
    {
      for( auto i = 3; i > recoil_index; --i )
      {
        v1 *= v1;
        v3 *= v3;
      }

      v1 = 1.f - v1;
      v3 = 1.f - v3;
    }

    const auto inaccuracy = v1 * g_engine_prediction->predicted_inaccuracy;
    const auto spread = v3 * g_engine_prediction->predicted_spread;

    return { std::cos( v2 ) * inaccuracy + std::cos( v4 ) * spread, std::sin( v2 ) * inaccuracy + std::sin( v4 ) * spread };
  }

  bool can_hit_hitbox_wrap( const vector3d& start, const vector3d& end, c_csplayer* player, int hitbox, records_t* record, matrix3x4_t* matrix )
  {
    auto current_bones = matrix ? matrix : record->sim_orig.bone;
    auto model = player->get_model( );
    if( !model )
      return false;

    auto studio_model = interfaces::model_info->get_studio_model( player->get_model( ) );
    auto set = studio_model->get_hitbox_set( 0 );

    if( !set )
      return false;

    auto studio_box = set->get_hitbox( hitbox );
    if( !studio_box )
      return false;

    vector3d min{ }, max{ };

    math::vector_transform( studio_box->bbmin, current_bones [ studio_box->bone ], min );
    math::vector_transform( studio_box->bbmax, current_bones [ studio_box->bone ], max );

    // box hitboxes (feet etc.) have radius -1/0, capsules have radius > 0.
    // (was `!= 1.f` which sent boxes down the capsule path with radius -1
    // and always returned false -> feet hitchance never passed, misses mislogged)
    if( studio_box->radius > 0.f )
      return math::segment_to_segment( start, end, min, max ) < studio_box->radius;

    math::vector_i_transform( start, current_bones [ studio_box->bone ], min );
    math::vector_i_transform( end, current_bones [ studio_box->bone ], max );
    return math::intersect_line_with_bb( min, max, studio_box->bbmin, studio_box->bbmax );
  }

  bool can_hit_hitbox( const vector3d& start, const vector3d& end, c_csplayer* player, int hitbox, records_t* record, matrix3x4_t* matrix )
  {
    return can_hit_hitbox_wrap( start, end, player, hitbox, record, matrix );
  }

  bool is_accuracy_valid( c_csplayer* player, point_t& point, float amount, float* out_chance )
  {
#ifdef _DEBUG
    spread_point.reset( );
    current_spread = 0.f;
    spread_points.clear( );
#endif

    if( cvars::weapon_accuracy_nospread->get_int( ) > 0 || amount <= 0.f )
      return true;

    if( !point.record )
      return false;

    auto weapon = g_ctx.weapon;
    if( !weapon )
      return false;

    auto weapon_info = weapon->get_weapon_info( );
    if( !weapon_info )
      return false;

    // resolve hitbox data ONCE (was re-resolved on all 255 seeds: model +
    // studio + set + box lookups per iteration). bones are frozen for this check.
    auto model = player->get_model( );
    if( !model )
      return false;

    auto studio_model = interfaces::model_info->get_studio_model( model );
    if( !studio_model )
      return false;

    auto set = studio_model->get_hitbox_set( player->hitbox_set( ) );
    if( !set )
      return false;

    auto studio_box = set->get_hitbox( point.hitbox );
    if( !studio_box )
      return false;

    matrix3x4_t* bones = point.record->sim_orig.bone;
    int bone = studio_box->bone;

    // world-space capsule segment, precomputed once
    vector3d cap_min{ }, cap_max{ };
    bool capsule = studio_box->radius > 0.f;
    if( capsule )
    {
      math::vector_transform( studio_box->bbmin, bones [ bone ], cap_min );
      math::vector_transform( studio_box->bbmax, bones [ bone ], cap_max );
    }

    auto hit_test = [ & ]( const vector3d& start, const vector3d& end ) -> bool
    {
      if( capsule )
        return math::segment_to_segment( start, end, cap_min, cap_max ) < studio_box->radius;

      vector3d a{ }, b{ };
      math::vector_i_transform( start, bones [ bone ], a );
      math::vector_i_transform( end, bones [ bone ], b );
      return math::intersect_line_with_bb( a, b, studio_box->bbmin, studio_box->bbmax );
    };

    float range = weapon_info->range;
    int bullets = weapon_info->bullets;
    float recoil = weapon->recoil_index( );

    vector3d forward, right, up;
    // same origin the real shot uses (was get_eye_position, off by view bob/proxy delta)
    vector3d start = g_ctx.eye_position;
    vector3d pos = math::angle_from_vectors( start, point.position );
    math::angle_to_vectors( pos, forward, right, up );

#ifdef _DEBUG
    if( debug_hitchance )
    {
      current_spread = g_ctx.spread;
      g_render->world_to_screen( point.position, spread_point );
    }
#endif

    float need = amount * ( float )total_seeds;

    int hits = 0;
    for( int i = 0; i < total_seeds; ++i )
    {
      auto spread_angle = calc_spread_angle( bullets, recoil, i );

      auto direction = forward + ( right * spread_angle.x ) + ( up * spread_angle.y );
      direction = direction.normalized( );

      auto end = start + direction * range;
#ifdef _DEBUG
      if( debug_hitchance )
      {
        vector2d scr_end;
        if( g_render->world_to_screen( end, scr_end ) )
          spread_points.emplace_back( scr_end );
      }
#endif

      if( hit_test( start, end ) )
        ++hits;

      // fail-early (fixed off-by-one: remaining seeds after this one are total-1-i)
      if( ( float )( hits + ( total_seeds - i - 1 ) ) < need )
        return false;

      // success-early: even losing the rest still passes
      if( ( float )hits >= need )
      {
        if( out_chance )
          *out_chance = ( float )hits / ( float )total_seeds;
        return true;
      }
    }

    if( out_chance )
      *out_chance = ( float )hits / ( float )total_seeds;
    return ( ( float )hits / ( float )total_seeds ) >= amount;
  }

  int get_legit_tab( c_basecombatweapon* temp_weapon )
  {
    auto weapon = temp_weapon ? temp_weapon : g_ctx.weapon;
    if( !weapon )
      return 0;

    if( weapon->is_knife( ) )
      return weapon_cfg_knife;

    auto idx = weapon->item_definition_index( );

    switch( idx )
    {
    case weapon_deagle:
      return weapon_cfg_deagle;
      break;
    case weapon_elite:
      return weapon_cfg_duals;
      break;
    case weapon_fiveseven:
      return weapon_cfg_fiveseven;
      break;
    case weapon_glock:
      return weapon_cfg_glock;
      break;
    case weapon_ak47:
      return weapon_cfg_ak47;
      break;
    case weapon_aug:
      return weapon_cfg_aug;
      break;
    case weapon_awp:
      return weapon_cfg_awp;
      break;
    case weapon_famas:
      return weapon_cfg_famas;
      break;
    case weapon_g3sg1:
      return weapon_cfg_g3sg1;
      break;
    case weapon_galilar:
      return weapon_cfg_galil;
      break;
    case weapon_m249:
      return weapon_cfg_m249;
      break;
    case weapon_m4a1:
      return weapon_cfg_m4a1;
      break;
    case weapon_mac10:
      return weapon_cfg_mac10;
      break;
    case weapon_p90:
      return weapon_cfg_p90;
      break;
    case weapon_mp5sd:
      return weapon_cfg_mp5sd;
      break;
    case weapon_ump45:
      return weapon_cfg_ump45;
      break;
    case weapon_xm1014:
      return weapon_cfg_xm1014;
      break;
    case weapon_bizon:
      return weapon_cfg_bizon;
      break;
    case weapon_mag7:
      return weapon_cfg_mag7;
      break;
    case weapon_negev:
      return weapon_cfg_negev;
      break;
    case weapon_sawedoff:
      return weapon_cfg_sawedoff;
      break;
    case weapon_tec9:
      return weapon_cfg_tec9;
      break;
    case weapon_hkp2000:
      return weapon_cfg_p2000;
      break;
    case weapon_mp7:
      return weapon_cfg_mp7;
      break;
    case weapon_mp9:
      return weapon_cfg_mp9;
      break;
    case weapon_nova:
      return weapon_cfg_nova;
      break;
    case weapon_p250:
      return weapon_cfg_p250;
      break;
    case weapon_scar20:
      return weapon_cfg_scar20;
      break;
    case weapon_sg556:
      return weapon_cfg_sg556;
      break;
    case weapon_ssg08:
      return weapon_cfg_ssg08;
      break;
    case weapon_m4a1_silencer:
      return weapon_cfg_m4a1s;
      break;
    case weapon_usp_silencer:
      return weapon_cfg_usps;
      break;
    case weapon_cz75a:
      return weapon_cfg_cz75;
      break;
    case weapon_revolver:
      return weapon_cfg_revolver;
      break;
    default:
      return 0;
      break;
    }
  }

  skin_weapon_t get_skin_weapon_config( )
  {
    if( !g_ctx.local || !g_ctx.local->is_alive( ) )
      return { };

    if( !g_ctx.weapon )
      return { };

    int tab = get_legit_tab( );
    return g_cfg.skins.skin_weapon [ tab ];
  }

  std::vector< std::pair< vector3d, bool > > get_multipoints( c_csplayer* player, int hitbox, matrix3x4_t* matrix )
  {
    std::vector< std::pair< vector3d, bool > > points = { };

    auto model = player->get_model( );
    if( !model )
      return points;

    auto hdr = interfaces::model_info->get_studio_model( model );
    if( !hdr )
      return points;

    auto set = hdr->get_hitbox_set( 0 );
    if( !set )
      return points;

    auto bbox = set->get_hitbox( hitbox );
    if( !bbox )
      return points;

    if( bbox->radius <= 0.f )
    {
      matrix3x4_t rot_matrix = { };
      rot_matrix.angle_matrix( bbox->rotation );

      matrix3x4_t mat = { };
      math::contact_transforms( matrix [ bbox->bone ], rot_matrix, mat );

      vector3d origin = mat.get_origin( );

      vector3d center = ( bbox->bbmin + bbox->bbmax ) * 0.5f;

      if( hitbox == hitbox_left_foot || hitbox == hitbox_right_foot )
        points.emplace_back( center, true );

      if( points.empty( ) )
        return points;

      for( auto& p : points )
      {
        p.first = { p.first.dot( mat.mat [ 0 ] ), p.first.dot( mat.mat [ 1 ] ), p.first.dot( mat.mat [ 2 ] ) };
        p.first += origin;
      }
    }
    else
    {
      vector3d max = bbox->bbmax;
      vector3d min = bbox->bbmin;
      vector3d center = ( bbox->bbmin + bbox->bbmax ) * 0.5f;

      float head_slider = get_weapon_config( ).scale_head / 100.f;
      float body_slider = get_weapon_config( ).scale_body / 100.f;

      float head_scale = bbox->radius * head_slider;
      float body_scale = bbox->radius * body_slider;

      constexpr float rotation = 0.70710678f;
      float near_center_scale = bbox->radius * ( head_slider / 2.f );

      if( hitbox == hitbox_head )
      {
        points.emplace_back( center, true );

        vector3d point{ };
        point = { max.x + 0.70710678f * head_scale, max.y - 0.70710678f * head_scale, max.z };
        points.emplace_back( point, false );

        point = { max.x, max.y, max.z + head_scale };
        points.emplace_back( point, false );

        point = { max.x, max.y, max.z - head_scale };
        points.emplace_back( point, false );

        point = { max.x, max.y - head_scale, max.z };
        points.emplace_back( point, false );
      }
      else
      {
        if( hitbox == hitbox_stomach )
        {
          points.emplace_back( center, true );
          points.emplace_back( vector3d( center.x, center.y, min.z + body_scale ), false );
          points.emplace_back( vector3d( center.x, center.y, max.z - body_scale ), false );
          points.emplace_back( vector3d{ center.x, max.y - body_scale, center.z }, false );
        }
        else if( hitbox == hitbox_pelvis || hitbox == hitbox_upper_chest )
        {
          points.emplace_back( center, true );
          points.emplace_back( vector3d( center.x, center.y, max.z + body_scale ), false );
          points.emplace_back( vector3d( center.x, center.y, min.z - body_scale ), false );
        }
        else if( hitbox == hitbox_lower_chest || hitbox == hitbox_chest )
        {
          points.emplace_back( center, true );
          points.emplace_back( vector3d( center.x, center.y, max.z + body_scale ), false );
          points.emplace_back( vector3d( center.x, center.y, min.z - body_scale ), false );

          points.emplace_back( vector3d{ center.x, max.y - body_scale, center.z }, false );
        }
        else if( hitbox == hitbox_right_calf || hitbox == hitbox_left_calf )
        {
          points.emplace_back( center, true );
          points.emplace_back( vector3d{ max.x - ( bbox->radius / 2.f ), max.y, max.z }, false );
        }
        else if( hitbox == hitbox_right_thigh || hitbox == hitbox_left_thigh )
        {
          points.emplace_back( center, true );
        }
        else if( hitbox == hitbox_right_upper_arm || hitbox == hitbox_left_upper_arm )
        {
          points.emplace_back( vector3d{ max.x + bbox->radius, center.y, center.z }, false );
        }
        else
          points.emplace_back( center, true );
      }

      if( points.empty( ) )
        return points;

      for( auto& p : points )
        math::vector_transform( p.first, matrix [ bbox->bone ], p.first );
    }

    return points;
  }
}

namespace resolver
{
  // backward = direction local -> enemy (enemy's "backward" AA).
  // forward  = direction enemy -> local (enemy facing us).
  __forceinline float calc_backward( c_csplayer* player )
  {
    return math::normalize( math::angle_from_vectors( g_ctx.local->origin( ), player->origin( ) ).y );
  }

  float get_backward( c_csplayer* player, records_t* record )
  {
    vector3d from = g_ctx.local->origin( );
    vector3d to = record ? record->origin : player->origin( );
    return math::normalize( math::angle_from_vectors( from, to ).y );
  }

  float get_away_angle( c_csplayer* player, records_t* record )
  {
    // supremacy-style "away": direction enemy -> local (forward).
    return math::normalize( get_backward( player, record ) + 180.f );
  }

  bool is_yaw_sideways( c_csplayer* player, records_t* record, float yaw )
  {
    float backward = get_backward( player, record );
    float delta = std::fabsf( math::normalize( backward - yaw ) );
    // sideways window, same as jre/supremacy (20..160)
    return delta > 20.f && delta < 160.f;
  }

  bool is_yaw_backward( c_csplayer* player, records_t* record, float yaw )
  {
    float backward = get_backward( player, record );
    float delta = std::fabsf( math::normalize( backward - yaw ) );
    return delta < 25.f;
  }

  __forceinline float get_lby_rotated_yaw( float lby, float yaw )
  {
    float delta = math::normalize( yaw - lby );
    if( std::fabsf( delta ) < 25.f )
      return lby;
    return delta > 0.f ? yaw + 25.f : yaw - 25.f;
  }

  // reference: bameware + supremacy freestanding, fixed:
  // - 3 points (left/right/back), damage first, fraction fallback
  // - hysteresis: keep last side on tie to stop spinning
  __forceinline void store_freestanding( c_csplayer* player, records_t* current )
  {
    auto& resolver_info = info [ player->index( ) ];

    auto& freestanding = resolver_info.freestanding;
    freestanding.available = false;

    if( !g_ctx.local || !g_ctx.local->is_alive( ) )
      return;

    if( current->velocity.length( true ) > 0.1f && !current->fake_walking )
      return;

    // backward base (local -> player)
    float backward = get_backward( player, current );

    const float height = 64.f;
    const float offset = 16.f;

    vector3d dir_left, dir_right, dir_back;
    math::angle_to_vectors( vector3d( 0.f, backward - 90.f, 0.f ), dir_left );
    math::angle_to_vectors( vector3d( 0.f, backward + 90.f, 0.f ), dir_right );
    math::angle_to_vectors( vector3d( 0.f, backward, 0.f ), dir_back );

    const auto base = current->origin + vector3d( 0.f, 0.f, height );
    const auto left_eye_pos = base + ( dir_left * offset );
    const auto right_eye_pos = base + ( dir_right * offset );
    const auto back_eye_pos = base + ( dir_back * offset );

    // damage test (autowall). if we have no weapon info (knife/nade) -> fraction only.
    bool can_damage = g_ctx.weapon_info && g_ctx.weapon && !g_ctx.weapon->is_knife( ) && !g_ctx.weapon->is_grenade( );
    if( can_damage )
    {
      freestanding.left_damage = g_auto_wall->fire_bullet( g_ctx.local, player, g_ctx.weapon_info, g_ctx.weapon->is_taser( ), g_ctx.eye_position, left_eye_pos ).dmg;
      freestanding.right_damage = g_auto_wall->fire_bullet( g_ctx.local, player, g_ctx.weapon_info, g_ctx.weapon->is_taser( ), g_ctx.eye_position, right_eye_pos ).dmg;
      freestanding.back_damage = g_auto_wall->fire_bullet( g_ctx.local, player, g_ctx.weapon_info, g_ctx.weapon->is_taser( ), g_ctx.eye_position, back_eye_pos ).dmg;
    }
    else
      freestanding.left_damage = freestanding.right_damage = freestanding.back_damage = 0;

    c_game_trace trace = { };
    c_trace_filter_world_only filter = { };

    interfaces::engine_trace->trace_ray( ray_t( left_eye_pos, g_ctx.eye_position ), mask_all, &filter, &trace );
    freestanding.left_fraction = trace.fraction;

    interfaces::engine_trace->trace_ray( ray_t( right_eye_pos, g_ctx.eye_position ), mask_all, &filter, &trace );
    freestanding.right_fraction = trace.fraction;

    interfaces::engine_trace->trace_ray( ray_t( back_eye_pos, g_ctx.eye_position ), mask_all, &filter, &trace );
    freestanding.back_fraction = trace.fraction;

    freestanding.available = true;
  }

  // pick freestanding yaw from stored data.
  // lowest damage = most cover. tie -> smallest fraction. full tie -> keep last side.
  void anti_freestand( c_csplayer* player, records_t* record, float& out_yaw )
  {
    auto& resolver_info = info [ player->index( ) ];
    auto& fs = resolver_info.freestanding;

    float backward = get_backward( player, record );

    if( !fs.available )
    {
      out_yaw = resolver_info.has_walk ? resolver_info.walk_record.lby : ( resolver_info.last_moving_lby != 0.f ? resolver_info.last_moving_lby : backward );
      return;
    }

    // all three hittable with good damage -> no cover, fall back to backward (don't spin)
    if( fs.left_damage >= 20 && fs.right_damage >= 20 && fs.back_damage >= 20 )
    {
      out_yaw = backward;
      return;
    }

    bool damage_valid = ( fs.left_damage > 0 || fs.right_damage > 0 || fs.back_damage > 0 );

    int best_side = 0; // -1 left, 1 right, 2 back
    if( damage_valid )
    {
      int best_dmg = INT_MAX;
      // order matters for stability: prefer back on tie, then keep last side
      if( fs.back_damage < best_dmg ) { best_dmg = fs.back_damage; best_side = 2; }
      if( fs.left_damage < best_dmg - 2 ) { best_dmg = fs.left_damage; best_side = -1; }
      if( fs.right_damage < best_dmg - 2 ) { best_dmg = fs.right_damage; best_side = 1; }

      // damage tie (within 2) -> use fraction
      if( std::abs( fs.left_damage - fs.right_damage ) <= 2 && std::abs( fs.left_damage - fs.back_damage ) <= 5 )
      {
        float best_frac = 2.f;
        if( fs.back_fraction < best_frac ) { best_frac = fs.back_fraction; best_side = 2; }
        if( fs.left_fraction < best_frac - 0.02f ) { best_frac = fs.left_fraction; best_side = -1; }
        if( fs.right_fraction < best_frac - 0.02f ) { best_frac = fs.right_fraction; best_side = 1; }

        // full tie -> keep last side (anti-spin)
        if( std::fabsf( fs.left_fraction - fs.right_fraction ) <= 0.02f && std::fabsf( fs.left_fraction - fs.back_fraction ) <= 0.02f )
        {
          if( resolver_info.freestanding_side == -1 ) best_side = -1;
          else if( resolver_info.freestanding_side == 1 ) best_side = 1;
          else best_side = 2;
        }
      }
    }
    else
    {
      // no damage info (all 0) -> pure wall fraction
      float best_frac = 2.f;
      if( fs.back_fraction < best_frac ) { best_frac = fs.back_fraction; best_side = 2; }
      if( fs.left_fraction < best_frac - 0.02f ) { best_frac = fs.left_fraction; best_side = -1; }
      if( fs.right_fraction < best_frac - 0.02f ) { best_frac = fs.right_fraction; best_side = 1; }

      if( std::fabsf( fs.left_fraction - fs.right_fraction ) <= 0.02f && std::fabsf( fs.left_fraction - fs.back_fraction ) <= 0.02f )
      {
        if( resolver_info.freestanding_side == -1 ) best_side = -1;
        else if( resolver_info.freestanding_side == 1 ) best_side = 1;
        else best_side = 2;
      }
    }

    // hysteresis: don't flip side on tiny diffs more often than 0.25s (fixes "крутит")
    float cur_time = interfaces::global_vars->cur_time;
    if( best_side != resolver_info.freestanding_side && resolver_info.freestanding_side != 0 )
    {
      if( cur_time - resolver_info.freestanding_time < 0.25f )
        best_side = resolver_info.freestanding_side;
    }

    if( best_side == -1 )
      out_yaw = backward - 90.f;
    else if( best_side == 1 )
      out_yaw = backward + 90.f;
    else
      out_yaw = backward;

    out_yaw = math::normalize( out_yaw );

    if( best_side != resolver_info.freestanding_side )
    {
      resolver_info.freestanding_side = best_side;
      resolver_info.freestanding_time = cur_time;
    }

    fs.yaw = out_yaw;
  }

  void resolve_walk( c_csplayer* player, records_t* current, resolver_info_t& resolver_info, float backward )
  {
    float lby = player->lby( );

    player->eye_angles( ).y = lby;

    // 2018 lby timer: moving -> next update in 0.22s
    resolver_info.body = lby;
    resolver_info.old_body = lby;
    resolver_info.body_update = current->anim_time + 0.22f;

    resolver_info.lby.lby_time = current->sim_time + 0.22f;
    resolver_info.lby.old_lby = lby;

    resolver_info.last_moving_lby = lby;
    resolver_info.last_move_time = current->anim_time;
    resolver_info.moved = false;
    resolver_info.has_walk = true;
    resolver_info.walk_record = *current;

    resolver_info.stand_index = 0;
    resolver_info.stand_index2 = 0;
    resolver_info.body_index = 0;

    resolver_info.mode = xor_str( "moving" );
    resolver_info.mode_id = resolver_mode_id_t::moving;
  }

  void resolve_stand( c_csplayer* player, records_t* current, resolver_info_t& resolver_info, float backward, int misses )
  {
    float lby = player->lby( );

    // confirmed desync flicker (see start()): forcing lby is exactly what he
    // wants us to do. skip both lby branches, go logic/freestand/brute.
    // (Kaaba UpdateResolverStage: desync flicking -> RESOLVE_MODE_LOGIC)
    bool allow_lby_force = !resolver_info.desync_flicking;

    // ---- lby proxy tracking (2018 era) ----
    // real flick: lby value changed since last record
    if( allow_lby_force && resolver_info.body != 0.f && lby != resolver_info.body && resolver_info.old_body != lby )
    {
      resolver_info.old_body = resolver_info.body;
      resolver_info.body = lby;
      resolver_info.body_update = current->anim_time + 1.1f;
      resolver_info.body_index = 0;

      player->eye_angles( ).y = lby;

      resolver_info.lby.old_lby = lby;
      resolver_info.lby.lby_time = current->sim_time + 1.1f;

      resolver_info.mode = xor_str( "lby flick" );
      resolver_info.mode_id = resolver_mode_id_t::lby_flick;
      return;
    }

    if( resolver_info.body == 0.f )
    {
      resolver_info.body = lby;
      resolver_info.old_body = lby;
      resolver_info.body_update = current->anim_time + 1.1f;
    }

    // predicted flick: we reached predicted update time, shoot it up to 2 times
    // (Kaaba: stop predicting after 2 lby misses - body_index handles that)
    if( allow_lby_force && current->anim_time >= resolver_info.body_update && resolver_info.body_index < 2 )
    {
      player->eye_angles( ).y = lby;
      resolver_info.body_update = current->anim_time + 1.1f;

      resolver_info.lby.lby_time = current->sim_time + 1.1f;

      resolver_info.mode = xor_str( "lby update" );
      resolver_info.mode_id = resolver_mode_id_t::lby_flick;
      return;
    }

    // ---- moved context (last moving lby within 128u) ----
    bool has_move = false;
    if( resolver_info.has_walk )
    {
      vector3d delta = resolver_info.walk_record.origin - current->origin;
      if( delta.length( true ) <= 128.f && ( current->anim_time - resolver_info.last_move_time ) < 5.f )
      {
        has_move = true;
        resolver_info.moved = true;
      }
    }

    if( has_move )
    {
      // plausibility (Kaaba plausible edge/last-moving): if neither freestand nor
      // last-move differs from current lby, no break is happening - take lby
      // directly instead of burning bruteforce steps on a fair target.
      {
        float fs_yaw = backward;
        anti_freestand( player, current, fs_yaw );

        float fs_diff = std::fabsf( math::normalize( fs_yaw - lby ) );
        float move_diff = std::fabsf( math::normalize( resolver_info.walk_record.lby - lby ) );

        if( fs_diff < 30.f && move_diff < 30.f )
        {
          player->eye_angles( ).y = lby;
          resolver_info.mode = xor_str( "lby (no break)" );
          resolver_info.mode_id = resolver_mode_id_t::lby_flick;
          resolver_info.last_stand_angle = lby;
          return;
        }
      }

      // first misses: freestanding -> last moving lby mix (supremacy style)
      int idx = resolver_info.stand_index;

      if( idx == 0 )
      {
        float fs_yaw = backward;
        anti_freestand( player, current, fs_yaw );

        // if freestanding gives sideways and lby is sideways too, prefer lby (less risky)
        float lby_diff = std::fabsf( math::normalize( resolver_info.walk_record.lby - fs_yaw ) );
        if( is_yaw_sideways( player, current, resolver_info.walk_record.lby ) && lby_diff < 35.f )
        {
          player->eye_angles( ).y = resolver_info.walk_record.lby;
          resolver_info.mode = xor_str( "last move" );
          resolver_info.mode_id = resolver_mode_id_t::last_move;
        }
        else
        {
          player->eye_angles( ).y = fs_yaw;
          resolver_info.mode = xor_str( "freestand" );
          resolver_info.mode_id = resolver_mode_id_t::freestand;
        }
      }
      else
      {
        // side-aware bruteforce: shooting order follows the freestanding side.
        // jitter/spin AAs alternate sides, so the OPPOSITE of the resolved side
        // comes right after last-move (not 3 shots later like a fixed table).
        float fs_yaw = backward;
        anti_freestand( player, current, fs_yaw );

        float fs_rel = math::normalize( fs_yaw - backward );
        int fs_side = ( fs_rel > 45.f ) ? 1 : ( fs_rel < -45.f ? -1 : 0 ); // +90 left, -90 right, 0 back

        auto yaw_side = [ & ]( int s )
        {
          if( s == 1 )
            return math::normalize( backward + 90.f );
          if( s == -1 )
            return math::normalize( backward - 90.f );
          if( s == 2 )
            return math::normalize( backward + 180.f ); // forward (facing us)
          return backward;
        };

        switch( idx % 5 )
        {
        case 1:
        {
          float diff = std::fabsf( math::normalize( resolver_info.walk_record.lby - current->lby ) );
          if( diff > 35.f )
          {
            player->eye_angles( ).y = resolver_info.walk_record.lby;
            resolver_info.mode = xor_str( "last move" );
            resolver_info.mode_id = resolver_mode_id_t::last_move;
          }
          else
          {
            // reversed side on second shot to cover jitter aa
            float rev = math::normalize( backward - ( fs_yaw - backward ) );
            player->eye_angles( ).y = rev;
            resolver_info.mode = xor_str( "freestand inv" );
            resolver_info.mode_id = resolver_mode_id_t::freestand;
          }
        }
        break;
        case 2:
          // opposite of resolved side first (fs back -> forward/facing us)
          player->eye_angles( ).y = yaw_side( fs_side == 1 ? -1 : ( fs_side == -1 ? 1 : 2 ) );
          resolver_info.mode = xor_str( "brute opposite" );
          resolver_info.mode_id = resolver_mode_id_t::brute;
          break;
        case 3:
          player->eye_angles( ).y = backward;
          resolver_info.mode = xor_str( "brute back" );
          resolver_info.mode_id = resolver_mode_id_t::brute;
          break;
        case 4:
          // resolved side retry (desync often flips back), back-side -> try left
          player->eye_angles( ).y = fs_side == 0 ? yaw_side( 1 ) : fs_yaw;
          resolver_info.mode = xor_str( "brute fs" );
          resolver_info.mode_id = resolver_mode_id_t::brute;
          break;
        case 0:
        default:
          // whatever is left uncovered: fs side -> forward, fs back -> right
          player->eye_angles( ).y = fs_side == 0 ? yaw_side( -1 ) : yaw_side( 2 );
          resolver_info.mode = xor_str( "brute fwd" );
          resolver_info.mode_id = resolver_mode_id_t::brute;
          break;
        }
      }

      resolver_info.last_stand_angle = player->eye_angles( ).y;
      return;
    }

    // ---- stand2: no move history -> lby-centered bruteforce ----
    {
      int idx2 = resolver_info.stand_index2;
      switch( idx2 % 6 )
      {
      case 0:
      {
        float fs_yaw = backward;
        anti_freestand( player, current, fs_yaw );
        player->eye_angles( ).y = fs_yaw;
        resolver_info.mode = xor_str( "freestand" );
        resolver_info.mode_id = resolver_mode_id_t::freestand;
      }
      break;
      case 1:
        player->eye_angles( ).y = lby;
        resolver_info.mode = xor_str( "lby" );
        resolver_info.mode_id = resolver_mode_id_t::lby_flick;
        break;
      case 2:
        player->eye_angles( ).y = math::normalize( lby + 180.f );
        resolver_info.mode = xor_str( "lby inv" );
        resolver_info.mode_id = resolver_mode_id_t::brute;
        break;
      case 3:
        player->eye_angles( ).y = math::normalize( lby + 110.f );
        resolver_info.mode = xor_str( "lby +110" );
        resolver_info.mode_id = resolver_mode_id_t::brute;
        break;
      case 4:
        player->eye_angles( ).y = math::normalize( lby - 110.f );
        resolver_info.mode = xor_str( "lby -110" );
        resolver_info.mode_id = resolver_mode_id_t::brute;
        break;
      case 5:
      default:
        player->eye_angles( ).y = backward;
        resolver_info.mode = xor_str( "brute back" );
        resolver_info.mode_id = resolver_mode_id_t::brute;
        break;
      }

      resolver_info.last_stand_angle = player->eye_angles( ).y;
    }
  }

  void resolve_air( c_csplayer* player, records_t* current, resolver_info_t& resolver_info, float backward, int misses )
  {
    // low speed air (jumped in place / landing) -> use stand logic, don't pollute air brute
    if( current->velocity.length( true ) < 50.f )
    {
      // keep lby timer fresh so landing flick is caught
      resolver_info.lby.old_lby = player->lby( );
      resolver_info.lby.lby_time = current->sim_time;
      resolve_stand( player, current, resolver_info, backward, misses );
      return;
    }

    float velyaw = math::rad_to_deg( std::atan2( current->velocity.y, current->velocity.x ) );
    velyaw = math::normalize( velyaw );

    int idx = resolver_info.air_index;
    switch( idx % 4 )
    {
    case 0:
      player->eye_angles( ).y = backward;
      resolver_info.mode = xor_str( "air" );
      resolver_info.mode_id = resolver_mode_id_t::air;
      break;
    case 1:
      player->eye_angles( ).y = math::normalize( velyaw + 180.f );
      resolver_info.mode = xor_str( "air vel" );
      resolver_info.mode_id = resolver_mode_id_t::air;
      break;
    case 2:
      player->eye_angles( ).y = math::normalize( velyaw - 90.f );
      resolver_info.mode = xor_str( "air brute" );
      resolver_info.mode_id = resolver_mode_id_t::air;
      break;
    case 3:
    default:
      player->eye_angles( ).y = math::normalize( velyaw + 90.f );
      resolver_info.mode = xor_str( "air brute" );
      resolver_info.mode_id = resolver_mode_id_t::air;
      break;
    }
  }

  void start( c_csplayer* player, records_t* current )
  {
    auto& resolver_info = info [ player->index( ) ];
    resolver_info.valid = false;

    if( !g_cfg.rage.enable || !g_ctx.local || !g_ctx.local->is_alive( ) )
    {
      if( resolver_info.valid )
        resolver_info.reset( );

      return;
    }

    // NOTE: bots included on purpose. the resolver only ever uses networked data
    // (origin, lby, velocity, layers) - never server-side reads - so running it on
    // bots (local server practice) is fair and needed for correct bones/aimpoints.
    // bots don't desync anyway: moving -> lby, standing -> backward/freestand just works.

    auto state = player->animstate( );
    if( !state )
      return;

    store_freestanding( player, current );

    float backward = get_backward( player, current );
    int misses = g_rage_bot->missed_shots [ player->index( ) ];

    // ---- enemy desync-flick detection (Kaaba layer-6 pattern + rhythm) ----
    // a breaker restarting his move layer shows as playback_rate crossing up
    // through ~1.0 across updates. two+ crossings on a constant tick rhythm
    // (or 4+ total) = confirmed desync flicker -> stop forcing lby below.
    {
      int cur_tick = interfaces::global_vars->tick_count;

      if( resolver_info.flick_count > 0 && cur_tick - resolver_info.last_flick_tick > 140 )
      {
        resolver_info.flick_count = 0;
        resolver_info.desync_flicking = false;
        resolver_info.body = 0.f; // force lby re-init, stale body would misfire once
      }

      auto ap = g_animation_fix->get_animation_player( player->index( ) );
      records_t* d_prev = ap ? ap->last_record : nullptr;
      records_t* d_pre2 = ap ? ap->old_record : nullptr;

      if( current->on_ground && d_prev && !d_prev->dormant && d_pre2 && !d_pre2->dormant )
      {
        float r0 = current->sim_orig.layers [ 6 ].playback_rate;
        float r1 = d_prev->sim_orig.layers [ 6 ].playback_rate;

        if( r0 >= 1.f && r1 < 1.f )
        {
          resolver_info.flick_freq_prev = resolver_info.flick_freq;
          resolver_info.flick_freq = cur_tick - resolver_info.last_flick_tick;
          resolver_info.last_flick_tick = cur_tick;
          resolver_info.flick_count++;

          if( resolver_info.flick_count >= 2
            && std::abs( resolver_info.flick_freq - resolver_info.flick_freq_prev ) <= 3 )
            resolver_info.desync_flicking = true;

          if( resolver_info.flick_count >= 4 )
            resolver_info.desync_flicking = true;
        }
      }
    }

    // shot record with tiny choke: eye is closest to real, keep it but mark
    // (prevents resolver from overriding legit flick shots)
    if( current->shooting && current->choke <= 2 )
    {
      resolver_info.mode = xor_str( "shot" );
      resolver_info.mode_id = resolver_mode_id_t::shot;
      player->eye_angles( ).y = math::normalize( player->eye_angles( ).y );
      resolver_info.valid = true;
      return;
    }

    if( !current->on_ground )
      resolve_air( player, current, resolver_info, backward, misses );
    else
    {
      if( current->velocity.length( true ) > 0.1f && !current->fake_walking )
        resolve_walk( player, current, resolver_info, backward );
      else
        resolve_stand( player, current, resolver_info, backward, misses );
    }

    player->eye_angles( ).y = math::normalize( player->eye_angles( ).y );

    resolver_info.valid = true;
  }

  void on_fsn( )
  {
    static bool should_clear = true;
    if( g_ctx.in_game )
    {
      should_clear = true;
      return;
    }

    if( should_clear )
    {
      for( auto& i : info )
        i.reset( );

      should_clear = false;
    }
  }
}