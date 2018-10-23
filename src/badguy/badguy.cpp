//  SuperTux
//  Copyright (C) 2006 Matthias Braun <matze@braunis.de>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "badguy/badguy.hpp"

#include "audio/sound_manager.hpp"
#include "badguy/dispenser.hpp"
#include "editor/editor.hpp"
#include "math/random_generator.hpp"
#include "object/bullet.hpp"
#include "object/camera.hpp"
#include "object/player.hpp"
#include "object/sprite_particle.hpp"
#include "object/water_drop.hpp"
#include "sprite/sprite.hpp"
#include "sprite/sprite_manager.hpp"
#include "supertux/level.hpp"
#include "supertux/sector.hpp"
#include "supertux/tile.hpp"
#include "util/reader_mapping.hpp"
#include "util/writer.hpp"

static const float SQUISH_TIME = 2;
static const float GEAR_TIME = 2;
static const float BURN_TIME = 1;

static const float X_OFFSCREEN_DISTANCE = 1280;
static const float Y_OFFSCREEN_DISTANCE = 800;

BadGuy::BadGuy(const Vector& pos, const std::string& sprite_name_, int layer_,
               const std::string& light_sprite_name) :
  BadGuy(pos, LEFT, sprite_name_, layer_, light_sprite_name)
{
}

BadGuy::BadGuy(const Vector& pos, Direction direction, const std::string& sprite_name_, int layer_,
               const std::string& light_sprite_name) :
  MovingSprite(pos, sprite_name_, layer_, COLGROUP_DISABLED),
  m_physic(),
  m_countMe(true),
  m_is_initialized(false),
  m_start_position(m_bbox.p1),
  m_dir(direction),
  m_start_dir(direction),
  m_frozen(false),
  m_ignited(false),
  m_in_water(false),
  m_dead_script(),
  m_melting_time(0),
  m_lightsprite(SpriteManager::current()->create(light_sprite_name)),
  m_glowing(false),
  m_state(STATE_INIT),
  m_is_active_flag(),
  m_state_timer(),
  m_on_ground_flag(false),
  m_floor_normal(),
  m_colgroup_active(COLGROUP_MOVING),
  m_parent_dispenser()
{
  SoundManager::current()->preload("sounds/squish.wav");
  SoundManager::current()->preload("sounds/fall.wav");
  SoundManager::current()->preload("sounds/splash.ogg");
  SoundManager::current()->preload("sounds/fire.ogg");

  m_dir = (m_start_dir == AUTO) ? LEFT : m_start_dir;
  m_lightsprite->set_blend(Blend::ADD);
}

BadGuy::BadGuy(const ReaderMapping& reader, const std::string& sprite_name_, int layer_,
               const std::string& light_sprite_name) :
  MovingSprite(reader, sprite_name_, layer_, COLGROUP_DISABLED),
  m_physic(),
  m_countMe(true),
  m_is_initialized(false),
  m_start_position(m_bbox.p1),
  m_dir(LEFT),
  m_start_dir(AUTO),
  m_frozen(false),
  m_ignited(false),
  m_in_water(false),
  m_dead_script(),
  m_melting_time(0),
  m_lightsprite(SpriteManager::current()->create(light_sprite_name)),
  m_glowing(false),
  m_state(STATE_INIT),
  m_is_active_flag(),
  m_state_timer(),
  m_on_ground_flag(false),
  m_floor_normal(),
  m_colgroup_active(COLGROUP_MOVING),
  m_parent_dispenser()
{
  std::string dir_str = "auto";
  reader.get("direction", dir_str);
  m_start_dir = str2dir( dir_str );
  m_dir = m_start_dir;

  reader.get("dead-script", m_dead_script);

  SoundManager::current()->preload("sounds/squish.wav");
  SoundManager::current()->preload("sounds/fall.wav");
  SoundManager::current()->preload("sounds/splash.ogg");
  SoundManager::current()->preload("sounds/fire.ogg");

  m_dir = (m_start_dir == AUTO) ? LEFT : m_start_dir;
  m_lightsprite->set_blend(Blend::ADD);
}

void
BadGuy::draw(DrawingContext& context)
{
  if(!m_sprite.get())
    return;
  if(m_state == STATE_INIT || m_state == STATE_INACTIVE)
    return;
  if(m_state == STATE_FALLING) {
    context.push_transform();
    context.set_flip(context.get_flip() ^ VERTICAL_FLIP);
    m_sprite->draw(context.color(), get_pos(), m_layer);
    context.pop_transform();
  } else {
    m_sprite->draw(context.color(), get_pos(), m_layer);
  }

  if (m_glowing) {
    m_lightsprite->draw(context.light(), m_bbox.get_middle(), 0);
  }
}

void
BadGuy::update(float elapsed_time)
{
  if(!Sector::get().inside(m_bbox)) {
    run_dead_script();
    m_is_active_flag = false;
    remove_me();
    // This was removed due to fixing a bug. If is it needed somewhere, then I'm sorry. --Hume2
    /**if(countMe) {
      // get badguy name from sprite_name ignoring path and extension
      std::string badguy = sprite_name.substr(0, sprite_name.length() - 7);
      int path_chars = badguy.rfind("/",badguy.length());
      badguy = badguy.substr(path_chars + 1, badguy.length() - path_chars);
      // log warning since badguys_killed can no longer reach total_badguys
      std::string current_level = "[" + Sector::get().get_level()->filename + "] ";
      log_warning << current_level << "Counted badguy " << badguy << " starting at " << start_position << " has left the sector" <<std::endl;;
    }*/
    return;
  }
  if ((m_state != STATE_INACTIVE) && is_offscreen()) {
    if (m_state == STATE_ACTIVE) deactivate();
    set_state(STATE_INACTIVE);
  }

  switch(m_state) {
    case STATE_ACTIVE:
      m_is_active_flag = true;
      if (Editor::is_active()) {
        break;
      }
      active_update(elapsed_time);
      break;
    case STATE_INIT:
    case STATE_INACTIVE:
      m_is_active_flag = false;
      inactive_update(elapsed_time);
      try_activate();
      break;
    case STATE_BURNING: {
      m_is_active_flag = false;
      m_movement = m_physic.get_movement(elapsed_time);
      if ( m_sprite->animation_done() ) {
        remove_me();
      }
    } break;
    case STATE_GEAR:
    case STATE_SQUISHED:
      m_is_active_flag = false;
      if(m_state_timer.check()) {
        remove_me();
        break;
      }
      m_movement = m_physic.get_movement(elapsed_time);
      break;
    case STATE_MELTING: {
      m_is_active_flag = false;
      m_movement = m_physic.get_movement(elapsed_time);
      if ( m_sprite->animation_done() || on_ground() ) {
        Sector::get().add<WaterDrop>(m_bbox.p1, get_water_sprite(), m_physic.get_velocity());
        remove_me();
        break;
      }
    } break;
    case STATE_GROUND_MELTING:
      m_is_active_flag = false;
      m_movement = m_physic.get_movement(elapsed_time);
      if ( m_sprite->animation_done() ) {
        remove_me();
      }
      break;
    case STATE_INSIDE_MELTING: {
      m_is_active_flag = false;
      m_movement = m_physic.get_movement(elapsed_time);
      if ( on_ground() && m_sprite->animation_done() ) {
        m_sprite->set_action(m_dir == LEFT ? "gear-left" : "gear-right", 1);
        set_state(STATE_GEAR);
      }
      int pa = graphicsRandom.rand(0,3);
      float px = graphicsRandom.randf(m_bbox.p1.x, m_bbox.p2.x);
      float py = graphicsRandom.randf(m_bbox.p1.y, m_bbox.p2.y);
      Vector ppos = Vector(px, py);
      Sector::get().add<SpriteParticle>(get_water_sprite(), "particle_" + std::to_string(pa),
                                             ppos, ANCHOR_MIDDLE,
                                             Vector(0, 0), Vector(0, 100 * Sector::get().get_gravity()),
                                             LAYER_OBJECTS-1);
    } break;
    case STATE_FALLING:
      m_is_active_flag = false;
      m_movement = m_physic.get_movement(elapsed_time);
      break;
  }

  m_on_ground_flag = false;
}

void
BadGuy::save(Writer& writer) {
  MovingSprite::save(writer);
  writer.write("direction", dir_to_string(m_dir), false);
  if(!m_dead_script.empty()) {
    writer.write("dead-script", m_dead_script, false);
  }
}

Direction
BadGuy::str2dir(const std::string& dir_str) const
{
  if( dir_str == "auto" )
    return AUTO;
  if( dir_str == "left" )
    return LEFT;
  if( dir_str == "right" )
    return RIGHT;

  //default to "auto"
  log_warning << "Badguy::str2dir: unknown direction \"" << dir_str << "\"" << std::endl;;
  return AUTO;
}

void
BadGuy::initialize()
{
}

void
BadGuy::activate()
{
}

void
BadGuy::deactivate()
{
}

void
BadGuy::active_update(float elapsed_time)
{
  m_movement = m_physic.get_movement(elapsed_time);
  if(m_frozen)
    m_sprite->stop_animation();
}

void
BadGuy::inactive_update(float )
{
}

void
BadGuy::collision_tile(uint32_t tile_attributes)
{
  // Don't kill badguys that have already been killed
  if (!is_active()) return;

  if(tile_attributes & Tile::WATER && !is_in_water())
  {
    m_in_water = true;
    SoundManager::current()->play("sounds/splash.ogg", get_pos());
  }
  if(!(tile_attributes & Tile::WATER) && is_in_water())
  {
    m_in_water = false;
  }

  if(tile_attributes & Tile::HURTS && is_hurtable()) {    
    if (tile_attributes & Tile::FIRE) {
      if (is_flammable()) ignite();
    }
    else if (tile_attributes & Tile::ICE) {
      if (is_freezable()) freeze();
    }
    else {
      kill_fall();
    }
  }
}

HitResponse
BadGuy::collision(GameObject& other, const CollisionHit& hit)
{
  if (!is_active()) return ABORT_MOVE;

  auto badguy = dynamic_cast<BadGuy*> (&other);
  if(badguy && badguy->is_active() && badguy->get_group() == COLGROUP_MOVING) {

    /* Badguys don't let badguys squish other badguys. It's bad. */
#if 0
    // hit from above?
    if (badguy->get_bbox().p2.y < (bbox.p1.y + 16)) {
      if(collision_squished(*badguy)) {
        return ABORT_MOVE;
      }
    }
#endif

    return collision_badguy(*badguy, hit);
  }

  auto player = dynamic_cast<Player*> (&other);
  if(player) {

    // hit from above?
    if (player->get_bbox().p2.y < (m_bbox.p1.y + 16)) {
      if(player->is_stone()) {
        kill_fall();
        return FORCE_MOVE;
      }
      if(collision_squished(*player)) {
        return FORCE_MOVE;
      }
    }

    if(player->is_stone()) {
      collision_solid(hit);
      return FORCE_MOVE;
    }

    return collision_player(*player, hit);
  }

  auto bullet = dynamic_cast<Bullet*> (&other);
  if(bullet)
    return collision_bullet(*bullet, hit);

  return FORCE_MOVE;
}

void
BadGuy::collision_solid(const CollisionHit& hit)
{
  m_physic.set_velocity_x(0);
  m_physic.set_velocity_y(0);
  update_on_ground_flag(hit);
}

HitResponse
BadGuy::collision_player(Player& player, const CollisionHit& )
{
  if(player.is_invincible()) {
    kill_fall();
    return ABORT_MOVE;
  }

  //TODO: unfreeze timer
  if(m_frozen)
    //unfreeze();
    return FORCE_MOVE;

  player.kill(false);
  return FORCE_MOVE;
}

HitResponse
BadGuy::collision_badguy(BadGuy& , const CollisionHit& )
{
  return FORCE_MOVE;
}

bool
BadGuy::collision_squished(GameObject& object)
{
  // frozen badguys can be killed with butt-jump
  if(m_frozen)
  {
    auto player = dynamic_cast<Player*>(&object);
    if(player && (player->m_does_buttjump)) {
      player->bounce(*this);
      kill_fall();
      return true;
    }
  }
    return false;
}

HitResponse
BadGuy::collision_bullet(Bullet& bullet, const CollisionHit& hit)
{
  if (is_frozen()) {
    if(bullet.get_type() == FIRE_BONUS) {
      // fire bullet thaws frozen badguys
      unfreeze();
      bullet.remove_me();
      return ABORT_MOVE;
    } else {
      // other bullets ricochet
      bullet.ricochet(*this, hit);
      return FORCE_MOVE;
    }
  }
  else if (is_ignited()) {
    if(bullet.get_type() == ICE_BONUS) {
      // ice bullets extinguish ignited badguys
      extinguish();
      bullet.remove_me();
      return ABORT_MOVE;
    } else {
      // other bullets are absorbed by ignited badguys
      bullet.remove_me();
      return FORCE_MOVE;
    }
  }
  else if(bullet.get_type() == FIRE_BONUS && is_flammable()) {
    // fire bullets ignite flammable badguys
    ignite();
    bullet.remove_me();
    return ABORT_MOVE;
  }
  else if(bullet.get_type() == ICE_BONUS && is_freezable()) {
    // ice bullets freeze freezable badguys
    freeze();
    bullet.remove_me();
    return ABORT_MOVE;
  }
  else {
    // in all other cases, bullets ricochet
    bullet.ricochet(*this, hit);
    return FORCE_MOVE;
  }
}

void
BadGuy::kill_squished(GameObject& object)
{
  if (!is_active()) return;

  SoundManager::current()->play("sounds/squish.wav", get_pos());
  m_physic.enable_gravity(true);
  m_physic.set_velocity_x(0);
  m_physic.set_velocity_y(0);
  set_state(STATE_SQUISHED);
  set_group(COLGROUP_MOVING_ONLY_STATIC);
  auto player = dynamic_cast<Player*>(&object);
  if (player) {
    player->bounce(*this);
  }

  // start dead-script
  run_dead_script();
}

void
BadGuy::kill_fall()
{
  if (!is_active()) return;

  if (m_frozen) {
    SoundManager::current()->play("sounds/brick.wav");
    Vector pr_pos;
    float cx = m_bbox.get_width() / 2;
    float cy = m_bbox.get_height() / 2;
    for (pr_pos.x = 0; pr_pos.x < m_bbox.get_width(); pr_pos.x += 16) {
      for (pr_pos.y = 0; pr_pos.y < m_bbox.get_height(); pr_pos.y += 16) {
        Vector speed = Vector((pr_pos.x - cx) * 8, (pr_pos.y - cy) * 8 + 100);
        Sector::get().add<SpriteParticle>(
            "images/particles/ice_piece1.sprite", "default",
            m_bbox.p1 + pr_pos, ANCHOR_MIDDLE,
            speed,
            Vector(0, Sector::get().get_gravity() * 100.0f));
      }
    }
    // start dead-script
    run_dead_script();
    remove_me();
  } else {
    SoundManager::current()->play("sounds/fall.wav", get_pos());
    m_physic.set_velocity_y(0);
    m_physic.set_acceleration_y(0);
    m_physic.enable_gravity(true);
    set_state(STATE_FALLING);

    // Set the badguy layer to be the foremost, so that
    // this does not reveal secret tilemaps:
    m_layer = Sector::get().get_foremost_layer() + 1;
    // start dead-script
    run_dead_script();
  }

}

void
BadGuy::run_dead_script()
{
  if (m_countMe)
    Sector::get().get_level().m_stats.m_badguys++;

  m_countMe = false;

  if(m_parent_dispenser != nullptr)
  {
    m_parent_dispenser->notify_dead();
  }

  // start dead-script
  if(!m_dead_script.empty()) {
    Sector::get().run_script(m_dead_script, "dead-script");
  }
}

void
BadGuy::set_state(State state_)
{
  if(m_state == state_)
    return;

  State laststate = m_state;
  m_state = state_;
  switch(state_) {
    case STATE_BURNING:
      m_state_timer.start(BURN_TIME);
      break;
    case STATE_SQUISHED:
      m_state_timer.start(SQUISH_TIME);
      break;
    case STATE_GEAR:
      m_state_timer.start(GEAR_TIME);
      break;
    case STATE_ACTIVE:
      set_group(m_colgroup_active);
      //bbox.set_pos(start_position);
      break;
    case STATE_INACTIVE:
      // was the badguy dead anyway?
      if(laststate == STATE_SQUISHED || laststate == STATE_FALLING) {
        remove_me();
      }
      set_group(COLGROUP_DISABLED);
      break;
    case STATE_FALLING:
      set_group(COLGROUP_DISABLED);
      break;
    default:
      break;
  }
}

bool
BadGuy::is_offscreen() const
{
  Vector dist;
  if (Editor::is_active()) {
    auto cam = Sector::get().m_camera;
    dist = cam->get_center() - m_bbox.get_middle();
  }
  auto player = get_nearest_player();
  if (!player)
    return false;
  if(!Editor::is_active()) {
    dist = player->get_bbox().get_middle() - m_bbox.get_middle();
  }
  // In SuperTux 0.1.x, Badguys were activated when Tux<->Badguy center distance was approx. <= ~668px
  // This doesn't work for wide-screen monitors which give us a virt. res. of approx. 1066px x 600px
  if ((fabsf(dist.x) <= X_OFFSCREEN_DISTANCE) && (fabsf(dist.y) <= Y_OFFSCREEN_DISTANCE)) {
    return false;
  }
  return true;
}

void
BadGuy::try_activate()
{
  // Don't activate if player is dying
  auto player = get_nearest_player();
  if (!player) return;

  if (!is_offscreen()) {
    set_state(STATE_ACTIVE);
    if (!m_is_initialized) {

      // if starting direction was set to AUTO, this is our chance to re-orient the badguy
      if (m_start_dir == AUTO) {
        auto player_ = get_nearest_player();
        if (player_ && (player_->get_bbox().p1.x > m_bbox.p2.x)) {
          m_dir = RIGHT;
        } else {
          m_dir = LEFT;
        }
      }

      initialize();
      m_is_initialized = true;
    }
    activate();
  }
}

bool
BadGuy::might_fall(int height) const
{
  // make sure we check for at least a 1-pixel fall
  assert(height > 0);

  float x1;
  float x2;
  float y1 = m_bbox.p2.y + 1;
  float y2 = m_bbox.p2.y + 1 + static_cast<float>(height);
  if (m_dir == LEFT) {
    x1 = m_bbox.p1.x - 1;
    x2 = m_bbox.p1.x;
  } else {
    x1 = m_bbox.p2.x;
    x2 = m_bbox.p2.x + 1;
  }
  return Sector::get().is_free_of_statics(Rectf(x1, y1, x2, y2));
}

Player*
BadGuy::get_nearest_player() const
{
  return Sector::get().get_nearest_player(m_bbox);
}

void
BadGuy::update_on_ground_flag(const CollisionHit& hit)
{
  if (hit.bottom) {
    m_on_ground_flag = true;
    m_floor_normal = hit.slope_normal;
  }
}

bool
BadGuy::on_ground() const
{
  return m_on_ground_flag;
}

bool
BadGuy::is_active() const
{
  return m_is_active_flag;
}

Vector
BadGuy::get_floor_normal() const
{
  return m_floor_normal;
}

void
BadGuy::freeze()
{
  set_group(COLGROUP_MOVING_STATIC);
  m_frozen = true;

  if(m_sprite->has_action("iced-left"))
    m_sprite->set_action(m_dir == LEFT ? "iced-left" : "iced-right", 1);
  // when the sprite doesn't have separate actions for left and right, it tries to use an universal one.
  else
  {
    if(m_sprite->has_action("iced"))
      m_sprite->set_action("iced", 1);
      // when no iced action exists, default to shading badguy blue
    else
    {
      m_sprite->set_color(Color(0.60f, 0.72f, 0.88f));
      m_sprite->stop_animation();
    }
  }
}

void
BadGuy::unfreeze()
{
  set_group(m_colgroup_active);
  m_frozen = false;

  // restore original color if needed
  if((!m_sprite->has_action("iced-left")) && (!m_sprite->has_action("iced")) )
  {
    m_sprite->set_color(Color(1.f, 1.f, 1.f));
    m_sprite->set_animation_loops();
  }
}

bool
BadGuy::is_freezable() const
{
  return false;
}

bool
BadGuy::is_frozen() const
{
  return m_frozen;
}

bool
BadGuy::is_in_water() const
{
  return m_in_water;
}

void
BadGuy::ignite()
{
  if (!is_flammable() || m_ignited) {
    return;
  }

  m_physic.enable_gravity(true);
  m_physic.set_velocity_x(0);
  m_physic.set_velocity_y(0);
  set_group(COLGROUP_MOVING_ONLY_STATIC);
  m_sprite->stop_animation();
  m_ignited = true;

  if (m_sprite->has_action("melting-left")) {

    // melt it!
    if (m_sprite->has_action("ground-melting-left") && on_ground()) {
      m_sprite->set_action(m_dir == LEFT ? "ground-melting-left" : "ground-melting-right", 1);
      SoundManager::current()->play("sounds/splash.ogg", get_pos());
      set_state(STATE_GROUND_MELTING);
    } else {
      m_sprite->set_action(m_dir == LEFT ? "melting-left" : "melting-right", 1);
      SoundManager::current()->play("sounds/sizzle.ogg", get_pos());
      set_state(STATE_MELTING);
    }

    run_dead_script();

  } else if (m_sprite->has_action("burning-left")) {
    // burn it!
    m_glowing = true;
    SoundManager::current()->play("sounds/fire.ogg", get_pos());
    m_sprite->set_action(m_dir == LEFT ? "burning-left" : "burning-right", 1);
    set_state(STATE_BURNING);
    run_dead_script();
  } else if (m_sprite->has_action("inside-melting-left")) {
    // melt it inside!
    SoundManager::current()->play("sounds/splash.ogg", get_pos());
    m_sprite->set_action(m_dir == LEFT ? "inside-melting-left" : "inside-melting-right", 1);
    set_state(STATE_INSIDE_MELTING);
    run_dead_script();
  } else {
    // Let it fall off the screen then.
    kill_fall();
  }
}

void
BadGuy::extinguish()
{
}

bool
BadGuy::is_flammable() const
{
  return true;
}

bool
BadGuy::is_ignited() const
{
  return m_ignited;
}

void
BadGuy::set_colgroup_active(CollisionGroup group_)
{
  m_colgroup_active = group_;
  if (m_state == STATE_ACTIVE) set_group(group_);
}

/* EOF */
