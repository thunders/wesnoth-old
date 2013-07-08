/*
   Copyright (C) 2013 by Andrius Silinskas <silinskas.andrius@gmail.com>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/
#include "multiplayer_create_engine.hpp"

#include "game_config_manager.hpp"
#include "game_display.hpp"
#include "game_preferences.hpp"
#include "filesystem.hpp"
#include "formula_string_utils.hpp"
#include "log.hpp"
#include "map_create.hpp"
#include "map_exception.hpp"
#include "minimap.hpp"
#include "wml_separators.hpp"
#include "wml_exception.hpp"

#include <boost/foreach.hpp>
#include <sstream>

static lg::log_domain log_config("config");
#define ERR_CF LOG_STREAM(err, log_config)

static lg::log_domain log_mp_create_engine("mp/create/engine");
#define DBG_MP LOG_STREAM(debug, log_mp_create_engine)

namespace mp {

level::level(const config& data) :
	data_(data)
{
}

std::string level::description() const
{
	return data_["description"];
}

std::string level::name() const
{
	return data_["name"];
}

std::string level::dependency_id() const
{
	return data_["id"];
}

void level::set_data(const config& data)
{
	data_ = data;
}

const config& level::data() const
{
	return data_;
}

scenario::scenario(const config& data) :
	level(data),
	map_(),
	num_players_(0)
{
}

scenario::~scenario()
{
}

bool scenario::can_launch_game() const
{
	return map_.get() != NULL;
}

surface* scenario::create_image_surface(const SDL_Rect& image_rect)
{
	surface* minimap = NULL;

	if (map_.get() != NULL) {
		minimap = new surface(image::getMinimap(image_rect.w,
			image_rect.h, *map_, 0));
	}

	return minimap;
}

void scenario::set_metadata()
{
	const std::string& map_data = data_["map_data"];

	try {
		map_.reset(new gamemap(resources::config_manager->game_config(),
			map_data));
	} catch(incorrect_map_format_error& e) {
		ERR_CF << "map could not be loaded: " << e.message << '\n';
	} catch(twml_exception& e) {
		ERR_CF << "map could not be loaded: " << e.dev_message << '\n';
	}

	set_sides();
}

int scenario::num_players() const
{
	return num_players_;
}

std::string scenario::map_size() const
{
	std::stringstream map_size;
	map_size << map_.get()->w();
	map_size << utils::unicode_multiplication_sign;
	map_size << map_.get()->h();

	return map_size.str();
}

void scenario::set_sides()
{
	if (map_.get() != NULL) {
		// If there are less sides in the configuration than there are
		// starting positions, then generate the additional sides
		const int map_positions = map_->num_valid_starting_positions();

		for (int pos = data_.child_count("side");
			pos < map_positions; ++pos) {
			config& side = data_.add_child("side");
			side["side"] = pos + 1;
			side["team_name"] = pos + 1;
			side["canrecruit"] = true;
			side["controller"] = "human";
		}

		num_players_ = 0;
		BOOST_FOREACH(const config &scenario, data_.child_range("side")) {
			if (scenario["allow_player"].to_bool(true)) {
				++num_players_;
			}
		}
	}
}

user_map::user_map(const config& data, const std::string& name) :
	scenario(data),
	name_(name)
{
}

user_map::~user_map()
{
}

std::string user_map::description() const
{
	return _("User made map");
}

std::string user_map::name() const
{
	return name_;
}

std::string user_map::dependency_id() const
{
	return name_;
}

random_map::random_map(const config& generator_data) :
	scenario(config()),
	generator_data_(generator_data)
{
}

random_map::~random_map()
{
}

const config& random_map::generator_data() const
{
	return generator_data_;
}

std::string random_map::name() const
{
	return generator_data_["name"];
}

std::string random_map::description() const
{
	return generator_data_["description"];
}

std::string random_map::dependency_id() const
{
	return generator_data_["id"];
}

campaign::campaign(const config& data) :
	level(data),
	image_label_(),
	min_players_(2),
	max_players_(2)
{
}

campaign::~campaign()
{
}

bool campaign::can_launch_game() const
{
	return !data_.empty();
}

surface* campaign::create_image_surface(const SDL_Rect& image_rect)
{
	surface temp_image(
		image::get_image(image::locator(image_label_)));

	surface* campaign_image = new surface(scale_surface(temp_image,
		image_rect.w, image_rect.h));

	return campaign_image;
}

void campaign::set_metadata()
{
	image_label_ = data_["image"].str();

	int min = data_["min_players"].to_int(2);
	int max = data_["max_players"].to_int(2);

	min_players_ = max_players_ =  min;

	if (max > min) {
		max_players_ = max;
	}
}

int campaign::min_players() const
{
	return min_players_;
}

int campaign::max_players() const
{
	return max_players_;
}

create_engine::create_engine(game_display& disp) :
	current_level_type_(),
	current_level_index_(0),
	current_era_index_(0),
	current_mod_index_(0),
	scenarios_(),
	user_maps_(),
	campaigns_(),
	sp_campaigns_(),
	random_maps_(),
	user_map_names_(),
	eras_(),
	mods_(),
	parameters_(),
	dependency_manager_(resources::config_manager->game_config(), disp.video()),
	generator_(NULL)
{
	DBG_MP << "restoring game config\n";

	// Restore game config for multiplayer.
	game_state state = game_state();
	state.classification().campaign_type = "multiplayer";
	resources::config_manager->
		load_game_config_for_game(state.classification());

	get_files_in_dir(get_user_data_dir() + "/editor/maps", &user_map_names_,
		NULL, FILE_NAME_ONLY);

	DBG_MP << "initializing all levels, eras and mods\n";

	init_all_levels();
	init_extras(ERA);
	init_extras(MOD);

	parameters_.saved_game = false;

	BOOST_FOREACH (const std::string& str, preferences::modifications()) {
		if (resources::config_manager->
				game_config().find_child("modification", "id", str))
			parameters_.active_mods.push_back(str);
	}

	dependency_manager_.try_modifications(parameters_.active_mods, true);
}

create_engine::~create_engine()
{
}

void create_engine::init_generated_level_data()
{
	DBG_MP << "initializing generated level data\n";

	config data = generator_->create_scenario(std::vector<std::string>());

	// Set the scenario to have placing of sides
	// based on the terrain they prefer
	data["modify_placing"] = "true";

	const std::string& description = current_level().data()["description"];
	data["description"] = description;

	current_level().set_data(data);
}

void create_engine::prepare_for_new_level()
{
	DBG_MP << "preparing mp_game_settings for new level\n";

	parameters_.scenario_data = current_level().data();
	parameters_.hash = parameters_.scenario_data.hash();
}

void create_engine::prepare_for_campaign(const std::string& difficulty)
{
	DBG_MP << "preparing data for campaign by reloading game config\n";

	game_state state = game_state();
	state.classification().campaign_type = "multiplayer";

	if (difficulty != "") {
		state.classification().difficulty = difficulty;
	}

	state.classification().campaign_define =
		current_level().data()["define"].str();
	state.classification().campaign_xtra_defines =
		utils::split(current_level().data()["extra_defines"]);

	resources::config_manager->
		load_game_config_for_game(state.classification());

	current_level().set_data(
		resources::config_manager->game_config().find_child("multiplayer",
		"id", current_level().data()["first_scenario"]));
}

void create_engine::prepare_for_saved_game()
{
	DBG_MP << "preparing mp_game_settings for saved game\n";

	parameters_.saved_game = true;
	parameters_.scenario_data.clear();
}

std::vector<std::string> create_engine::levels_menu_item_names() const
{
	std::vector<std::string> menu_names;

	BOOST_FOREACH(level_ptr level, get_levels_by_type(current_level_type_)) {
		menu_names.push_back(level->name() + HELP_STRING_SEPARATOR +
			level->name());
	}

	return menu_names;
}

std::vector<std::string> create_engine::extras_menu_item_names(
	const MP_EXTRA extra_type) const
{
	const std::vector<extras_metadata>& extras =
		get_const_extras_by_type(extra_type);

	std::vector<std::string> names;

	BOOST_FOREACH(const extras_metadata& extra, extras) {
		names.push_back(extra.first);
	}

	return names;
}

level& create_engine::current_level() const
{
	switch (current_level_type_) {
	case level::SCENARIO: {
		return *scenarios_[current_level_index_];
	}
	case level::USER_MAP: {
		return *user_maps_[current_level_index_];
	}
	case level::RANDOM_MAP: {
		return *random_maps_[current_level_index_];
	}
	case level::CAMPAIGN: {
		return *campaigns_[current_level_index_];
	}
	case level::SP_CAMPAIGN:
	default: {
		return *sp_campaigns_[current_level_index_];
	}
	} // end switch
}

std::string create_engine::current_extra_description(const MP_EXTRA extra_type) const
{
	const std::vector<extras_metadata>& extras =
		get_const_extras_by_type(extra_type);
	const size_t& index = (extra_type == ERA) ?
		current_era_index_ : current_mod_index_;

	return extras[index].second;
}

void create_engine::set_current_level_type(const level::TYPE type)
{
	current_level_type_ = type;
}

level::TYPE create_engine::current_level_type() const
{
	return current_level_type_;
}

void create_engine::set_current_level(const size_t index)
{
	current_level_index_ = index;

	if (current_level_type_ == level::RANDOM_MAP) {
		random_map* current_random_map =
			dynamic_cast<random_map*>(&current_level());

		generator_.assign(create_map_generator(
			current_random_map->generator_data()["map_generation"],
			current_random_map->generator_data().child("generator")));
	} else {
		generator_.assign(NULL);
	}

	if (current_level_type_ != level::CAMPAIGN &&
		current_level_type_ != level::SP_CAMPAIGN) {

		dependency_manager_.try_scenario(current_level().dependency_id());
	}
}

void create_engine::set_current_era_index(const size_t index)
{
	current_era_index_ = index;

	dependency_manager_.try_era_by_index(index);
}

void create_engine::set_current_mod_index(const size_t index)
{
	current_mod_index_ = index;
}

bool create_engine::generator_assigned() const
{
	return generator_ != NULL;
}

void create_engine::generator_user_config(display& disp)
{
	generator_->user_config(disp);
}

int create_engine::find_scenario_by_id(const std::string& id) const
{
	int i = 0;
	BOOST_FOREACH(const user_map_ptr& user_map, user_maps_) {
		if (user_map->dependency_id() == id) {
			return i;
		}
		i++;
	}

	i = 0;
	BOOST_FOREACH(const random_map_ptr& random_map, random_maps_) {
		if (random_map->dependency_id() == id) {
			return i;
		}
		i++;
	}

	i = 0;
	BOOST_FOREACH(const scenario_ptr& scenario, scenarios_) {
		if (scenario->dependency_id() == id) {
			return i;
		}
		i++;
	}

	return -1;
}

const depcheck::manager& create_engine::dependency_manager() const
{
	return dependency_manager_;
}

void create_engine::init_active_mods()
{
	parameters_.active_mods = dependency_manager_.get_modifications();
}

mp_game_settings& create_engine::get_parameters()
{
	DBG_MP << "getting parameter values" << std::endl;

	config::const_child_itors era_list = resources::config_manager->
		game_config().child_range("era");
	for (int num = current_era_index_; num > 0; --num) {
		if (era_list.first == era_list.second) {
			throw config::error(_("Invalid era selected"));
		}
		++era_list.first;
	}

	parameters_.mp_era = (*era_list.first)["id"].str();

	return parameters_;
}

void create_engine::init_all_levels()
{
	if (const config &generic_multiplayer =
		resources::config_manager->game_config().child(
			"generic_multiplayer")) {
		config gen_mp_data = generic_multiplayer;

		// User maps.
		for(size_t i = 0; i < user_map_names_.size(); i++)
		{
			config user_map_data = gen_mp_data;
			user_map_data["map_data"] = read_map(user_map_names_[i]);

			user_map_ptr new_user_map(new user_map(user_map_data,
				user_map_names_[i]));
			user_maps_.push_back(new_user_map);

			// Since user maps are treated as scenarios,
			// some dependency info is required
			config depinfo;
			depinfo["id"] = user_map_names_[i];
			depinfo["name"] = user_map_names_[i];
			dependency_manager_.insert_element(depcheck::SCENARIO, depinfo, i);
		}
	}

	// Stand-alone scenarios.
	BOOST_FOREACH(const config &data,
		resources::config_manager->game_config().child_range("multiplayer"))
	{
		if (!data["map_generation"].empty()) {
			random_map_ptr new_random_map(new random_map(data));
			random_maps_.push_back(new_random_map);
		} else if (data["allow_new_game"].to_bool(true)) {
			scenario_ptr new_scenario(new scenario(data));
			scenarios_.push_back(new_scenario);
		}
	}

	// Campaigns.
	BOOST_FOREACH(const config &data,
		resources::config_manager->game_config().child_range("campaign"))
	{
		const std::string& type = data["type"];

		if (type == "mp" || type == "hybrid") {
			campaign_ptr new_campaign(new campaign(data));
			campaigns_.push_back(new_campaign);
		} else {
			campaign_ptr new_sp_campaign(new campaign(data));
			sp_campaigns_.push_back(new_sp_campaign);
		}
	}
}

void create_engine::init_extras(const MP_EXTRA extra_type)
{
	std::vector<extras_metadata>& extras = get_extras_by_type(extra_type);
	const std::string extra_name = (extra_type == ERA) ? "era" : "modification";

	BOOST_FOREACH(const config &extra,
		resources::config_manager->game_config().child_range(extra_name)) {
		extras.push_back(std::make_pair(extra["name"], extra["description"]));
	}
}

std::vector<create_engine::level_ptr>
	create_engine::get_levels_by_type(level::TYPE type) const
{
	std::vector<level_ptr> levels;
	switch (type) {
	case level::SCENARIO:
		BOOST_FOREACH(level_ptr level, scenarios_) {
			levels.push_back(level);
		}
		break;
	case level::USER_MAP:
		BOOST_FOREACH(level_ptr level, user_maps_) {
			levels.push_back(level);
		}
		break;
	case level::RANDOM_MAP:
		BOOST_FOREACH(level_ptr level, random_maps_) {
			levels.push_back(level);
		}
		break;
	case level::CAMPAIGN:
		BOOST_FOREACH(level_ptr level, campaigns_) {
			levels.push_back(level);
		}
		break;
	case level::SP_CAMPAIGN:
		BOOST_FOREACH(level_ptr level, sp_campaigns_) {
			levels.push_back(level);
		}
		break;
	} // end switch

	return levels;
}

const std::vector<create_engine::extras_metadata>&
	create_engine::get_const_extras_by_type(const MP_EXTRA extra_type) const
{
	return (extra_type == ERA) ? eras_ : mods_;
}

std::vector<create_engine::extras_metadata>&
	create_engine::get_extras_by_type(const MP_EXTRA extra_type)
{
	return (extra_type == ERA) ? eras_ : mods_;
}

} // end namespace mp
