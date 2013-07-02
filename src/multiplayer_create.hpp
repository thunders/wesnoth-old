/*
   Copyright (C) 2007 - 2013
   Part of the Battle for Wesnoth Project http://www.wesnoth.org

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

/** @file */

#ifndef MULTIPLAYER_CREATE_HPP_INCLUDED
#define MULTIPLAYER_CREATE_HPP_INCLUDED

#include "mp_depcheck.hpp"
#include "mp_game_settings.hpp"
#include "multiplayer_create_engine.hpp"
#include "multiplayer_ui.hpp"
#include "widgets/slider.hpp"
#include "widgets/combo.hpp"
#include "mapgen.hpp"
#include "tooltips.hpp"

namespace mp {

class create : public mp::ui
{
public:
	create(game_display& dist, const config& game_config, chat& c, config& gamelist, bool local_players_only);
	~create();

	mp_game_settings& get_parameters();

protected:
	virtual void layout_children(const SDL_Rect& rect);
	virtual void process_event();
	virtual void hide_children(bool hide=true);

private:
	void sync_current_level_with_engine();

	void synchronize_selections();

	std::vector<std::string> levels_menu_item_names() const;

	void draw_level_image() const;

	std::string select_campaign_difficulty();

	bool local_players_only_;

	tooltips::manager tooltip_manager_;
	int era_selection_;
	int mod_selection_;
	int level_selection_;

	std::vector<std::string> mod_options_;
	std::vector<std::string> mod_descriptions_;

	gui::menu eras_menu_;
	gui::menu levels_menu_;
	gui::menu mods_menu_;

	gui::label filter_name_label_;
	gui::label filter_num_players_label_;
	gui::label map_generator_label_;
	gui::label era_label_;
	gui::label level_label_;
	gui::label mod_label_;
	gui::label map_size_label_;
	gui::label num_players_label_;

	gui::button launch_game_;
	gui::button cancel_game_;
	gui::button regenerate_map_;
	gui::button generator_settings_;
	gui::button load_game_;
	gui::button switch_levels_menu_;

	gui::slider filter_num_players_slider_;

	gui::textbox description_;
	gui::textbox filter_name_;

	util::scoped_ptr<surface_restorer> image_restorer_;
	SDL_Rect image_rect_;

	mp_game_settings parameters_;

	depcheck::manager dependency_manager_;

	create_engine engine_;
};

} // end namespace mp

#endif

