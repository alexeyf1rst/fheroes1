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

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "agg_image.h"
#include "army.h"
#include "artifact.h"
#include "buildinginfo.h"
#include "castle.h" // IWYU pragma: associated
#include "cursor.h"
#include "dialog.h"
#include "game_hotkeys.h"
#include "heroes.h"
#include "heroes_base.h"
#include "heroes_recruits.h"
#include "icn.h"
#include "image.h"
#include "kingdom.h"
#include "localevent.h"
#include "maps_fileinfo.h"
#include "math_base.h"
#include "payment.h"
#include "race.h"
#include "resource.h"
#include "screen.h"
#include "settings.h"
#include "statusbar.h"
#include "tools.h"
#include "translations.h"
#include "ui_button.h"
#include "ui_castle.h"
#include "ui_constants.h"
#include "ui_dialog.h"
#include "ui_kingdom.h"
#include "ui_language.h"
#include "ui_text.h"
#include "ui_tool.h"
#include "world.h"

int Castle::DialogBuyHero( const Heroes * hero ) const
{
    if ( !hero ) {
        return Dialog::CANCEL;
    }

    fheroes2::Display & display = fheroes2::Display::instance();

    // setup cursor
    const CursorRestorer cursorRestorer( true, Cursor::POINTER );

    const int32_t spacer = 10;
    const fheroes2::Sprite & portrait_frame = fheroes2::AGG::GetICN( ICN::SURRENDR, 4 );

    const fheroes2::Text recruitHeroText( _( "Recruit Hero" ), fheroes2::FontType::normalYellow() );

    uint32_t count = hero->GetCountArtifacts();
    if ( hero->hasArtifact( Artifact::MAGIC_BOOK ) ) {
        --count;
    }

    std::string str = _( "%{name} is a level %{value} %{race} " );

    if ( count ) {
        str += count > 1 ? _( "with %{count} artifacts." ) : _( "with 1 artifact." );
    }
    else {
        str += _( "without artifacts." );
    }

    StringReplace( str, "%{name}", hero->GetName() );
    StringReplace( str, "%{value}", hero->GetLevel() );
    StringReplace( str, "%{race}", Race::String( hero->GetRace() ) );
    StringReplace( str, "%{count}", count );

    const fheroes2::Text heroDescriptionText( std::move( str ), fheroes2::FontType::normalWhite() );

    Resource::BoxSprite rbs( PaymentConditions::RecruitHero(), fheroes2::boxAreaWidthPx );

    const int32_t dialogHeight = recruitHeroText.height( fheroes2::boxAreaWidthPx ) + spacer + portrait_frame.height() + spacer
                                 + heroDescriptionText.height( fheroes2::boxAreaWidthPx ) + spacer + rbs.GetArea().height;

    const Dialog::FrameBox box( dialogHeight, true );
    const fheroes2::Rect & dialogRoi = box.GetArea();

    recruitHeroText.draw( dialogRoi.x, dialogRoi.y + 2, fheroes2::boxAreaWidthPx, display );

    // portrait and frame
    fheroes2::Point pos{ dialogRoi.x + ( dialogRoi.width - portrait_frame.width() ) / 2, dialogRoi.y + recruitHeroText.height( fheroes2::boxAreaWidthPx ) + spacer };
    fheroes2::Blit( portrait_frame, display, pos.x, pos.y );

    const fheroes2::Rect heroPortraitArea( pos.x, pos.y, portrait_frame.width(), portrait_frame.height() );
    pos.x = pos.x + 5;
    pos.y = pos.y + 6;
    hero->PortraitRedraw( pos.x, pos.y, PORT_BIG, display );

    pos.y += portrait_frame.height() + spacer;
    heroDescriptionText.draw( dialogRoi.x, pos.y + 2, fheroes2::boxAreaWidthPx, display );

    rbs.SetPos( dialogRoi.x, pos.y + heroDescriptionText.height( fheroes2::boxAreaWidthPx ) + spacer );
    rbs.Redraw();

    fheroes2::ButtonGroup buttonGroup( dialogRoi, Dialog::OK | Dialog::CANCEL );
    fheroes2::ButtonBase & buttonOkay = buttonGroup.button( 0 );
    const fheroes2::ButtonBase & buttonCancel = buttonGroup.button( 1 );

    if ( !AllowBuyHero() ) {
        buttonOkay.disable();
    }

    buttonGroup.draw();
    display.render();

    LocalEvent & le = LocalEvent::Get();
    while ( le.HandleEvents() ) {
        const int result = buttonGroup.processEvents();
        if ( result != Dialog::ZERO ) {
            return result;
        }

        if ( le.isMouseRightButtonPressedInArea( heroPortraitArea ) ) {
            Dialog::QuickInfo( *hero );
        }
        else if ( le.isMouseRightButtonPressedInArea( buttonOkay.area() ) ) {
            std::string recruitHero = _( "Recruit %{name} the %{race}" );
            StringReplace( recruitHero, "%{name}", hero->GetName() );
            StringReplace( recruitHero, "%{race}", Race::String( hero->GetRace() ) );
            recruitHero += '.';
            fheroes2::showStandardTextMessage( _( "Okay" ), std::move( recruitHero ), Dialog::ZERO );
        }
        else if ( le.isMouseRightButtonPressedInArea( buttonCancel.area() ) ) {
            fheroes2::showStandardTextMessage( _( "Cancel" ), _( "Exit this menu without doing anything." ), Dialog::ZERO );
        }
    }

    return Dialog::CANCEL;
}

int Castle::DialogBuyCastle( const bool hasButtons /* = true */ ) const
{
    const BuildingInfo info( *this, BUILD_CASTLE );
    return info.DialogBuyBuilding( hasButtons ) ? Dialog::OK : Dialog::CANCEL;
}

Castle::ConstructionDialogResult Castle::_openConstructionDialog( uint32_t & dwellingTobuild )
{
    if ( !isBuild( BUILD_CASTLE ) ) {
        // It is not possible to open this dialog without a built castle!
        assert( 0 );
        return ConstructionDialogResult::DoNothing;
    }

    dwellingTobuild = BUILD_NOTHING;

    // setup cursor
    const CursorRestorer cursorRestorer( true, Cursor::POINTER );

    fheroes2::Display & display = fheroes2::Display::instance();
    const fheroes2::ImageRestorer restorer( display, ( display.width() - fheroes2::Display::DEFAULT_WIDTH ) / 2,
                                            ( display.height() - fheroes2::Display::DEFAULT_HEIGHT ) / 2, fheroes2::Display::DEFAULT_WIDTH,
                                            fheroes2::Display::DEFAULT_HEIGHT );

    const fheroes2::Point cur_pt( restorer.x(), restorer.y() );
    fheroes2::Point dst_pt( cur_pt );

    const Settings & conf = Settings::Get();
    const bool isEvilInterface = conf.isEvilInterfaceEnabled();

    fheroes2::Blit( fheroes2::AGG::GetICN( isEvilInterface ? ICN::CASLWIND_EVIL : ICN::CASLWIND, 0 ), display, dst_pt.x, dst_pt.y );

    // draw castle sprite
    dst_pt.x = cur_pt.x + 459;
    dst_pt.y = cur_pt.y + 6;
    DrawImageCastle( dst_pt );

    // castle name
    const fheroes2::Text castleName( GetName(), fheroes2::FontType::smallWhite() );
    castleName.draw( cur_pt.x + 538 - castleName.width() / 2, cur_pt.y + 2, display );

    constexpr std::array<BuildingType, 4> mageGuildLevels = { BUILD_MAGEGUILD1, BUILD_MAGEGUILD2, BUILD_MAGEGUILD3, BUILD_MAGEGUILD4 };
    const BuildingType mageGuild = mageGuildLevels[std::min<int>( GetLevelMageGuild(), static_cast<int>( mageGuildLevels.size() ) - 1 )];
    const std::array<BuildingType, 11> buildingTypes = {
        DWELLING_MONSTER1, DWELLING_MONSTER2, DWELLING_MONSTER3, DWELLING_MONSTER4, DWELLING_MONSTER5, DWELLING_MONSTER6,
        mageGuild, BUILD_TAVERN, BUILD_THIEVESGUILD, BUILD_SHIPYARD, BUILD_WELL
    };
    std::vector<BuildingInfo> buildings;
    buildings.reserve( buildingTypes.size() );
    for ( size_t i = 0; i < buildingTypes.size(); ++i ) {
        BuildingInfo & building = buildings.emplace_back( *this, buildingTypes[i] );
        building.SetPos( cur_pt.x + 5 + 144 * static_cast<int32_t>( i % 3 ), cur_pt.y + 2 + 77 * static_cast<int32_t>( i / 3 ) );
        building.Redraw();
    }

    Kingdom & kingdom = GetKingdom();

    Heroes * hero1 = kingdom.GetRecruits().GetHero1();
    Heroes * hero2 = kingdom.GetRecruits().GetHero2();

    std::string not_allow1_msg;
    std::string not_allow2_msg;
    const bool allow_buy_hero1 = hero1 ? AllowBuyHero( &not_allow1_msg ) : false;
    const bool allow_buy_hero2 = hero2 ? AllowBuyHero( &not_allow2_msg ) : false;

    // first hero
    dst_pt.x = cur_pt.x + 443;
    dst_pt.y = cur_pt.y + 260;
    const fheroes2::Rect rectHero1( dst_pt.x, dst_pt.y, 102, 93 );

    if ( hero1 ) {
        hero1->PortraitRedraw( dst_pt.x, dst_pt.y, PORT_BIG, display );
    }
    else {
        const fheroes2::Sprite & noHeroBackgound = fheroes2::AGG::GetICN( ICN::STRIP, 3 );
        fheroes2::Copy( noHeroBackgound, 0, 0, display, rectHero1.x, rectHero1.y, rectHero1.width, rectHero1.height );
    }

    // indicator
    if ( !allow_buy_hero1 ) {
        const fheroes2::Sprite & spriteDeny = fheroes2::AGG::GetICN( ICN::TOWNWIND, 12 );
        fheroes2::Blit( spriteDeny, display, dst_pt.x + 102 - 4 + 1 - spriteDeny.width(), dst_pt.y + 93 - 2 - spriteDeny.height() );
    }

    // second hero
    dst_pt.x = cur_pt.x + 443;
    dst_pt.y = cur_pt.y + 363;
    const fheroes2::Rect rectHero2( dst_pt.x, dst_pt.y, 102, 94 );
    if ( hero2 ) {
        hero2->PortraitRedraw( dst_pt.x, dst_pt.y, PORT_BIG, display );
    }
    else {
        const fheroes2::Sprite & noHeroBackgound = fheroes2::AGG::GetICN( ICN::STRIP, 3 );
        fheroes2::Copy( noHeroBackgound, 0, 0, display, rectHero2.x, rectHero2.y, rectHero2.width, rectHero2.height );
    }

    // indicator
    if ( !allow_buy_hero2 ) {
        const fheroes2::Sprite & spriteDeny = fheroes2::AGG::GetICN( ICN::TOWNWIND, 12 );
        fheroes2::Blit( spriteDeny, display, dst_pt.x + 102 - 4 + 1 - spriteDeny.width(), dst_pt.y + 93 - 2 - spriteDeny.height() );
    }

    dst_pt.y = cur_pt.y + 480 - 19;
    fheroes2::Button buttonPrevCastle( cur_pt.x, dst_pt.y, ICN::SMALLBAR, 1, 2 );
    fheroes2::TimedEventValidator timedButtonPrevCastle( [&buttonPrevCastle]() { return buttonPrevCastle.isPressed(); } );
    buttonPrevCastle.subscribe( &timedButtonPrevCastle );

    // bottom small bar
    const fheroes2::Sprite & bar = fheroes2::AGG::GetICN( ICN::SMALLBAR, 0 );
    const int32_t statusBarWidth = bar.width();
    dst_pt.x = cur_pt.x + buttonPrevCastle.area().width;
    fheroes2::Copy( bar, 0, 0, display, dst_pt.x, dst_pt.y, statusBarWidth, bar.height() );

    StatusBar statusBar;
    // Status bar must be smaller due to extra art on both sides.
    statusBar.setRoi( { dst_pt.x + 16, dst_pt.y, statusBarWidth - 16 * 2, 0 } );

    // button next castle
    fheroes2::Button buttonNextCastle( dst_pt.x + statusBarWidth, dst_pt.y, ICN::SMALLBAR, 3, 4 );
    fheroes2::TimedEventValidator timedButtonNextCastle( [&buttonNextCastle]() { return buttonNextCastle.isPressed(); } );
    buttonNextCastle.subscribe( &timedButtonNextCastle );

    // button exit
    dst_pt.x = cur_pt.x + 553;
    dst_pt.y = cur_pt.y + 428;
    fheroes2::Button buttonExit( dst_pt.x, dst_pt.y, ICN::BUTTON_EXIT_TOWN, 0, 1 );

    if ( GetKingdom().GetCastles().size() < 2 ) {
        buttonPrevCastle.disable();
        buttonNextCastle.disable();
    }

    buttonPrevCastle.draw();
    buttonNextCastle.draw();
    buttonExit.draw();

    // redraw resource panel
    const fheroes2::Rect & rectResource = fheroes2::drawResourcePanel( GetKingdom().GetFunds(), display, cur_pt );
    const fheroes2::Rect resActiveArea( rectResource.x, rectResource.y, rectResource.width, buttonExit.area().y - rectResource.y - 3 );

    auto recruitHeroDialog = [this, &buttonExit]( Heroes * hero ) {
        const fheroes2::ButtonRestorer exitRestorer( buttonExit );
        if ( Dialog::OK == DialogBuyHero( hero ) ) {
            RecruitHero( hero );
            return true;
        }
        return false;
    };

    std::string defaultTitle( _( "Castle Options. Month: %{month}, Week: %{week}, Day: %{day}" ) );
    StringReplace( defaultTitle, "%{month}", world.GetMonth() );
    StringReplace( defaultTitle, "%{week}", world.GetWeek() );
    StringReplace( defaultTitle, "%{day}", world.GetDay() );

    LocalEvent & le = LocalEvent::Get();

    auto updateStatusBar = [&]() {
        for ( const BuildingInfo & building : buildings ) {
            if ( le.isMouseCursorPosInArea( building.GetArea() ) ) {
                building.SetStatusMessage( statusBar );
                return;
            }
        }

        if ( hero1 && le.isMouseCursorPosInArea( rectHero1 ) ) {
            if ( !allow_buy_hero1 ) {
                statusBar.ShowMessage( not_allow1_msg );
            }
            else {
                std::string str = _( "Recruit %{name} the %{race}" );
                StringReplace( str, "%{name}", hero1->GetName() );
                StringReplace( str, "%{race}", Race::String( hero1->GetRace() ) );
                statusBar.ShowMessage( str );
            }
        }
        else if ( hero2 && le.isMouseCursorPosInArea( rectHero2 ) ) {
            if ( !allow_buy_hero2 ) {
                statusBar.ShowMessage( not_allow2_msg );
            }
            else {
                std::string str = _( "Recruit %{name} the %{race}" );
                StringReplace( str, "%{name}", hero2->GetName() );
                StringReplace( str, "%{race}", Race::String( hero2->GetRace() ) );
                statusBar.ShowMessage( str );
            }
        }
        else if ( le.isMouseCursorPosInArea( buttonExit.area() ) ) {
            statusBar.ShowMessage( _( "Exit Castle Options" ) );
        }
        else if ( le.isMouseCursorPosInArea( resActiveArea ) ) {
            statusBar.ShowMessage( _( "Show Income" ) );
        }
        else if ( buttonPrevCastle.isEnabled() && le.isMouseCursorPosInArea( buttonPrevCastle.area() ) ) {
            statusBar.ShowMessage( _( "Show previous town" ) );
        }
        else if ( buttonNextCastle.isEnabled() && le.isMouseCursorPosInArea( buttonNextCastle.area() ) ) {
            statusBar.ShowMessage( _( "Show next town" ) );
        }
        else {
            statusBar.ShowMessage( defaultTitle );
        }
    };

    updateStatusBar();

    display.render( restorer.rect() );

    while ( le.HandleEvents() ) {
        buttonExit.drawOnState( le.isMouseLeftButtonPressedAndHeldInArea( buttonExit.area() ) );

        if ( le.MouseClickLeft( buttonExit.area() ) || Game::HotKeyCloseWindow() ) {
            break;
        }

        if ( buttonPrevCastle.isEnabled() ) {
            buttonPrevCastle.drawOnState( le.isMouseLeftButtonPressedAndHeldInArea( buttonPrevCastle.area() ) );

            if ( le.MouseClickLeft( buttonPrevCastle.area() ) || HotKeyPressEvent( Game::HotKeyEvent::DEFAULT_LEFT ) || timedButtonPrevCastle.isDelayPassed() ) {
                return ConstructionDialogResult::PrevConstructionWindow;
            }
        }
        if ( buttonNextCastle.isEnabled() ) {
            buttonNextCastle.drawOnState( le.isMouseLeftButtonPressedAndHeldInArea( buttonNextCastle.area() ) );

            if ( le.MouseClickLeft( buttonNextCastle.area() ) || HotKeyPressEvent( Game::HotKeyEvent::DEFAULT_RIGHT ) || timedButtonNextCastle.isDelayPassed() ) {
                return ConstructionDialogResult::NextConstructionWindow;
            }
        }

        if ( le.MouseClickLeft( resActiveArea ) ) {
            const fheroes2::ButtonRestorer exitRestorer( buttonExit );
            fheroes2::showKingdomIncome( world.GetKingdom( GetColor() ), Dialog::OK );
        }
        else if ( le.isMouseRightButtonPressedInArea( resActiveArea ) ) {
            fheroes2::showKingdomIncome( world.GetKingdom( GetColor() ), 0 );
        }
        else if ( le.isMouseRightButtonPressedInArea( buttonExit.area() ) ) {
            fheroes2::showStandardTextMessage( _( "Exit" ), _( "Exit this menu." ), Dialog::ZERO );
        }
        for ( BuildingInfo & building : buildings ) {
            if ( le.isMouseCursorPosInArea( building.GetArea() ) && building.QueueEventProcessing( buttonExit ) ) {
                dwellingTobuild = building.getBuilding();
                return ConstructionDialogResult::Build;
            }
        }
        if ( hero1 && le.MouseClickLeft( rectHero1 ) && recruitHeroDialog( hero1 ) ) {
            return ConstructionDialogResult::RecruitHero;
        }
        if ( hero2 && le.MouseClickLeft( rectHero2 ) && recruitHeroDialog( hero2 ) ) {
            return ConstructionDialogResult::RecruitHero;
        }

        // Right click
        if ( hero1 && le.isMouseRightButtonPressedInArea( rectHero1 ) ) {
            LocalEvent::Get().reset();
            hero1->OpenDialog( true, true, false, false, false, false, fheroes2::getLanguageFromAbbreviation( conf.getGameLanguage() ) );

            // Use half fade if game resolution is not 640x480.
            fheroes2::fadeInDisplay( restorer.rect(), !display.isDefaultSize() );
        }
        else if ( hero2 && le.isMouseRightButtonPressedInArea( rectHero2 ) ) {
            LocalEvent::Get().reset();
            hero2->OpenDialog( true, true, false, false, false, false, fheroes2::getLanguageFromAbbreviation( conf.getGameLanguage() ) );

            // Use half fade if game resolution is not 640x480.
            fheroes2::fadeInDisplay( restorer.rect(), !display.isDefaultSize() );
        }
        else if ( le.isMouseRightButtonPressedInArea( buttonNextCastle.area() ) ) {
            fheroes2::showStandardTextMessage( _( "Show next town" ), _( "Click to show the next town." ), Dialog::ZERO );
        }
        else if ( le.isMouseRightButtonPressedInArea( buttonPrevCastle.area() ) ) {
            fheroes2::showStandardTextMessage( _( "Show previous town" ), _( "Click to show the previous town." ), Dialog::ZERO );
        }

        updateStatusBar();
    }

    return ConstructionDialogResult::DoNothing;
}
