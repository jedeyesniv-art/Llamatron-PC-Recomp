// Adding the program to Steam as a non-Steam game, with its library art (res/steam, made by
// Tools/make_steam_art.py; shipped in Extras\Steam art). The settings panel's ADD TO STEAM.
#pragma once

#include <string>

namespace steam {

// Started from Steam (SteamGameId: a non-Steam shortcut - the Steam Deck, or Windows once added): the art goes into
// that shortcut's grid images. Otherwise (Windows): the logged-in user's shortcut to this exe gets the art, and is
// added first if there isn't one (Steam closed: it rewrites shortcuts.vdf on exit). Returns what happened, short,
// for the panel ("ADDED", "ART ADDED", "CLOSE STEAM FIRST"...: at most 18 letters); the details go to the log.
std::string AddToSteam();

}  // namespace steam
