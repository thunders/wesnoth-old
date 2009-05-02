/* $Id$ */
/*
   Copyright (C) 2009 by Yurii Chernyi <terraninfo@terraninfo.net>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2
   or at your option any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

/**
 * Base class for the AI and AI-ai_manager contract.
 * @file ai/ai_interface.cpp
 */

#include "ai_interface.hpp"

// =======================================================================
//
// =======================================================================
std::string ai_interface::describe_self() const
{
	return "? [ai]";
}

