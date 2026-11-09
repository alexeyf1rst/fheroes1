/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2019 - 2026                                             *
 *                                                                         *
 *   Free Heroes2 Engine: http://sourceforge.net/projects/fheroes2         *
 *   Copyright (C) 2009 by Andrey Afletdinov <fheroes2@gmail.com>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <list>
#include <stdexcept>
#include <utility>

#include "agg.h"
#include "agg_file.h"
#include "dir.h"
#include "settings.h"
#include "tools.h"

namespace
{
    fheroes2::AGGFile heroes_agg;
}

std::vector<uint8_t> AGG::getDataFromAggFile( const std::string & key, const bool /*ignoreExpansion*/ )
{
    return heroes_agg.read( key );
}

std::vector<std::string> AGG::getHoMM1MapNames()
{
    return heroes_agg.getFileNamesWithExtension( ".MAP" );
}

AGG::AGGInitializer::AGGInitializer()
{
    if ( init() ) {
        return;
    }

    throw std::logic_error( "No AGG data files found." );
}

bool AGG::AGGInitializer::init()
{
    const ListFiles aggFileNames = Settings::FindFiles( "data", ".agg", false );
    if ( aggFileNames.empty() ) {
        return false;
    }

    const std::string heroesAggFileName( "heroes.agg" );
    std::string heroesAggFilePath;

    for ( const std::string & path : aggFileNames ) {
        if ( path.size() < heroesAggFileName.size() ) {
            continue;
        }

        const std::string tempPath = StringLower( path );

        if ( tempPath.compare( tempPath.size() - heroesAggFileName.size(), heroesAggFileName.size(), heroesAggFileName ) == 0 ) {
            heroesAggFilePath = path;
            break;
        }
    }

    if ( heroesAggFilePath.empty() ) {
        // The main game resource file was not found.
        return false;
    }

    if ( !heroes_agg.open( heroesAggFilePath ) ) {
        return false;
    }

    _originalAGGFilePath = std::move( heroesAggFilePath );

    return true;
}

bool AGG::isPoLResourceFilePresent()
{
    return false;
}
