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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "agg.h"
#include "agg_image.h"
#include "audio.h"
#include "audio_manager.h"
#include "cursor.h"
#include "dialog.h"
#include "dialog_selectscenario.h"
#include "difficulty.h"
#include "game.h" // IWYU pragma: associated
#include "game_exit.h"
#include "game_hotkeys.h"
#include "game_mainmenu_ui.h"
#include "game_mode.h"
#include "icn.h"
#include "image.h"
#include "localevent.h"
#include "logging.h"
#include "maps_fileinfo.h"
#include "math_base.h"
#include "math_tools.h"
#include "mus.h"
#include "player_info.h"
#include "players.h"
#include "screen.h"
#include "serialize.h"
#include "settings.h"
#include "system.h"
#include "tools.h"
#include "translations.h"
#include "ui_button.h"
#include "ui_dialog.h"
#include "ui_text.h"
#include "ui_tool.h"
#include "ui_window.h"
#include "world.h"

namespace
{
    void outputNewGameInTextSupportMode()
    {
        START_TEXT_SUPPORT_MODE
        COUT( "Select Map for New Game\n" )

        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::MAIN_MENU_SELECT_MAP ) << " to select a map." )
        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::DEFAULT_CANCEL ) << " to close the dialog and return to the Main Menu." )
        COUT( "Press " << Game::getHotKeyNameByEventId( Game::HotKeyEvent::DEFAULT_OKAY ) << " to start the chosen map." )
    }

    void showCurrentlySelectedMapInfoInTextSupportMode( const Maps::FileInfo & mapInfo )
    {
        START_TEXT_SUPPORT_MODE
        COUT( "Currently selected map:\n" )
        COUT( mapInfo.getSummary() )
    }

    void updatePlayers( Players & players, const int humanPlayerCount )
    {
        if ( humanPlayerCount < 2 )
            return;

        int foundHumans = 0;

        for ( size_t i = 0; i < players.size(); ++i ) {
            if ( players[i]->isControlHuman() ) {
                ++foundHumans;
                if ( players[i]->isControlAI() )
                    players[i]->SetControl( CONTROL_HUMAN );
            }

            if ( foundHumans == humanPlayerCount )
                break;
        }
    }

    void DrawScenarioStaticInfo( const fheroes2::Rect & rt )
    {
        fheroes2::Display & display = fheroes2::Display::instance();

        const fheroes2::FontType normalWhiteFont = fheroes2::FontType::normalWhite();

        // text scenario
        fheroes2::Text text( _( "Scenario:" ), normalWhiteFont );
        text.draw( rt.x, rt.y + 9, rt.width, display );

        // text game difficulty
        text.set( _( "Game Difficulty:" ), normalWhiteFont );
        text.draw( rt.x, rt.y + 59, rt.width, display );

        // text opponents
        text.set( _( "Opponents:" ), normalWhiteFont );
        text.draw( rt.x, rt.y + 164, rt.width, display );

        // text class
        text.set( _( "Class:" ), normalWhiteFont );
        text.draw( rt.x, rt.y + 248, rt.width, display );
    }

    void RedrawMapTitle( const Settings & conf, const fheroes2::Rect & maxRoi, const fheroes2::Rect & centeredRoi )
    {
        const auto & info = conf.getCurrentMapInfo();
        fheroes2::Text text{ info.name, fheroes2::FontType::normalWhite(), info.getSupportedLanguage() };

        if ( maxRoi.width <= 0 ) {
            return;
        }

        if ( centeredRoi.width > 0 && text.width() > centeredRoi.width ) {
            text.fitToOneRow( maxRoi.width );
            text.draw( maxRoi.x + ( maxRoi.width - text.width() ), maxRoi.y + 3, text.width(), fheroes2::Display::instance() );
        }
        else if ( centeredRoi.width > 0 ) {
            text.draw( centeredRoi.x, centeredRoi.y + 3, centeredRoi.width, fheroes2::Display::instance() );
        }
        else {
            text.fitToOneRow( maxRoi.width );
            text.draw( maxRoi.x, maxRoi.y + 3, maxRoi.width, fheroes2::Display::instance() );
        }
    }

    void RedrawDifficultyInfo( const fheroes2::Point & dst )
    {
        const int32_t width = 77;
        const int32_t height = 69;

        for ( int32_t current = Difficulty::EASY; current <= Difficulty::IMPOSSIBLE; ++current ) {
            const int32_t offset = width * current;
            int32_t normalSpecificOffset = 0;
            // Add offset shift because the original difficulty icons have irregular spacing.
            if ( current == Difficulty::NORMAL ) {
                normalSpecificOffset = 1;
            }

            const fheroes2::Text text( Difficulty::String( current ), fheroes2::FontType::smallWhite() );
            text.draw( dst.x + 31 + offset + normalSpecificOffset - ( text.width() / 2 ), dst.y + height, fheroes2::Display::instance() );
        }
    }

    fheroes2::Rect RedrawRatingInfo( const fheroes2::Point & offset, int32_t width_ )
    {
        std::string str( _( "Rating %{rating}%" ) );
        StringReplace( str, "%{rating}", Game::GetRating() );

        const fheroes2::Text text( str, fheroes2::FontType::normalWhite() );
        const int32_t y = offset.y + 372;
        text.draw( offset.x, y, width_, fheroes2::Display::instance() );

        const int32_t textX = ( width_ > text.width() ) ? offset.x + ( width_ - text.width() ) / 2 : 0;

        return { textX, y, text.width(), text.height() };
    }

    // Opens the HoMM1 map-file selection dialog (REQUEST.BMP + REQEXTRA.BMP).
    // Returns the new selected map index; returns currentIndex unchanged if CANCEL is pressed.
    //
    // REQUEST.BMP:  320×331 — list panel (title, 10 visible rows, scrollbar, name bar, OK/Cancel)
    // REQEXTRA.BMP: 320×141 — info panel (SIZE / DIFFICULTY / description)
    // REQUEST.ICN:  5 frames
    //   0 : 225×19  name bar
    //   1/2: 96×25  OKAY  released/pressed
    //   3/4: 96×25  CANCEL released/pressed
    int showHoMM1MapSelectDialog( const MapsFileInfoList & maps, int currentIndex )
    {
        fheroes2::Display & display = fheroes2::Display::instance();

        const fheroes2::Sprite & reqBmp = fheroes2::AGG::GetICN( ICN::H1REQUEST_BMP,  0 );
        const fheroes2::Sprite & extBmp = fheroes2::AGG::GetICN( ICN::H1REQEXTRA_BMP, 0 );

        if ( reqBmp.empty() )
            return currentIndex;

        // Position dialog flush against the right edge, vertically centred.
        const int32_t reqW  = reqBmp.width();
        const int32_t reqH  = reqBmp.height();
        const int32_t extH  = extBmp.empty() ? 0 : extBmp.height();
        const int32_t reqX  = fheroes2::Display::DEFAULT_WIDTH - reqW;
        const int32_t reqY  = ( fheroes2::Display::DEFAULT_HEIGHT - reqH - extH ) / 2;
        const int32_t extY  = reqY + reqH;

        // Layout constants (relative to reqX / reqY).
        constexpr int32_t kListX   = 8;
        constexpr int32_t kListY   = 55;
        constexpr int32_t kListW   = 278;
        constexpr int32_t kRowH    = 20;
        constexpr int32_t kVisRows = 10;
        constexpr int32_t kScrollX = 290; // scrollbar column x
        constexpr int32_t kScrollH = kRowH * kVisRows;
        constexpr int32_t kArrowH  = 18;
        constexpr int32_t kNameBarY = 262;
        constexpr int32_t kBtnY     = 291;
        constexpr int32_t kBtnOkX   = 16;
        constexpr int32_t kBtnCxX   = 208;

        const int mapCount = static_cast<int>( maps.size() );

        // Keep scroll window visible around the current selection.
        int scrollTop = std::max( 0, currentIndex - kVisRows / 2 );
        if ( scrollTop + kVisRows > mapCount )
            scrollTop = std::max( 0, mapCount - kVisRows );
        int selected = currentIndex;

        // Scroll arrow hit-rects (in screen coordinates).
        const fheroes2::Rect arrowUpRect{ reqX + kScrollX, reqY + kListY,                            20, kArrowH };
        const fheroes2::Rect arrowDnRect{ reqX + kScrollX, reqY + kListY + kScrollH - kArrowH,       20, kArrowH };
        const fheroes2::Rect listAreaRect{ reqX + kListX,  reqY + kListY, kListW + 20, kScrollH };

        fheroes2::Button btnOk    ( reqX + kBtnOkX, reqY + kBtnY, ICN::H1REQUEST_ICN, 1, 2 );
        fheroes2::Button btnCancel( reqX + kBtnCxX, reqY + kBtnY, ICN::H1REQUEST_ICN, 3, 4 );

        const auto redraw = [&]() {
            // Background panels.
            fheroes2::Copy( reqBmp, 0, 0, display, reqX, reqY, reqW, reqH );
            if ( !extBmp.empty() )
                fheroes2::Copy( extBmp, 0, 0, display, reqX, extY, extBmp.width(), extBmp.height() );

            // "File to Load:" title, centered in panel.
            {
                fheroes2::Text title( _( "File to Load:" ), fheroes2::FontType::normalWhite() );
                title.draw( reqX, reqY + 22, reqW, display );
            }

            // List rows.
            for ( int i = 0; i < kVisRows; ++i ) {
                const int mi = scrollTop + i;
                if ( mi >= mapCount )
                    break;
                const fheroes2::FontType font = ( mi == selected ) ? fheroes2::FontType::normalYellow() : fheroes2::FontType::normalWhite();
                const int32_t rowY = reqY + kListY + i * kRowH;
                fheroes2::Text row( maps[mi].name, font );
                row.fitToOneRow( kListW );
                row.draw( reqX + kListX, rowY + ( kRowH - row.height() ) / 2, display );
            }

            // Scrollbar thumb (drawn when the list is longer than the visible window).
            if ( mapCount > kVisRows ) {
                const int32_t trackTop = reqY + kListY + kArrowH;
                const int32_t trackH   = kScrollH - 2 * kArrowH;
                const int32_t thumbH   = std::max( 8, trackH * kVisRows / mapCount );
                const int32_t thumbY   = trackTop + ( trackH - thumbH ) * scrollTop / ( mapCount - kVisRows );
                fheroes2::Fill( display, reqX + kScrollX + 2, thumbY, 16, thumbH, fheroes2::GetColorId( 180, 150, 80 ) );
            }

            // Name bar with selected scenario name.
            {
                const fheroes2::Sprite & bar = fheroes2::AGG::GetICN( ICN::H1REQUEST_ICN, 0 );
                if ( !bar.empty() ) {
                    const int32_t bx = reqX + ( reqW - bar.width() ) / 2;
                    fheroes2::Blit( bar, display, bx, reqY + kNameBarY );
                    fheroes2::Text sel( maps[selected].name, fheroes2::FontType::normalWhite() );
                    sel.fitToOneRow( bar.width() - 4 );
                    sel.draw( bx + 2, reqY + kNameBarY + ( bar.height() - sel.height() ) / 2, display );
                }
            }

            btnOk.draw();
            btnCancel.draw();

            // REQEXTRA: SIZE / DIFFICULTY / description.
            if ( !extBmp.empty() ) {
                const Maps::FileInfo & fi = maps[selected];

                fheroes2::Text lbl( _( "SIZE:" ), fheroes2::FontType::normalYellow() );
                lbl.draw( reqX + 12, extY + 18, display );

                const char * szStr = ( fi.width <= 36 ) ? "Small" : ( fi.width <= 72 ) ? "Medium" : ( fi.width <= 108 ) ? "Large" : "Extra Large";
                fheroes2::Text szVal( szStr, fheroes2::FontType::normalWhite() );
                szVal.draw( reqX + 12, extY + 36, display );

                lbl.set( _( "DIFFICULTY:" ), fheroes2::FontType::normalYellow() );
                lbl.draw( reqX + 168, extY + 18, display );

                fheroes2::Text dfVal( Difficulty::String( fi.difficulty ), fheroes2::FontType::normalWhite() );
                dfVal.draw( reqX + 168, extY + 36, display );

                if ( !fi.description.empty() ) {
                    fheroes2::Text desc( fi.description, fheroes2::FontType::smallWhite() );
                    desc.draw( reqX + 8, extY + 62, extBmp.width() - 16, display );
                }
            }
        };

        redraw();
        display.render();

        LocalEvent & le = LocalEvent::Get();
        while ( le.HandleEvents() ) {
            btnOk.drawOnState( le.isMouseLeftButtonPressedInArea( btnOk.area() ) );
            btnCancel.drawOnState( le.isMouseLeftButtonPressedInArea( btnCancel.area() ) );

            bool changed = false;

            // Click on a list entry to select it.
            for ( int i = 0; i < kVisRows; ++i ) {
                const int mi = scrollTop + i;
                if ( mi >= mapCount )
                    break;
                const fheroes2::Rect rowRect{ reqX + kListX, reqY + kListY + i * kRowH, kListW, kRowH };
                if ( le.MouseClickLeft( rowRect ) ) {
                    selected = mi;
                    changed  = true;
                }
            }

            // Scrollbar arrows.
            if ( le.MouseClickLeft( arrowUpRect ) && scrollTop > 0 ) {
                --scrollTop;
                changed = true;
            }
            if ( le.MouseClickLeft( arrowDnRect ) && scrollTop + kVisRows < mapCount ) {
                ++scrollTop;
                changed = true;
            }

            // Mouse wheel anywhere over the list area.
            if ( le.isMouseWheelUpInArea( listAreaRect ) && scrollTop > 0 ) {
                --scrollTop;
                changed = true;
            }
            if ( le.isMouseWheelDownInArea( listAreaRect ) && scrollTop + kVisRows < mapCount ) {
                ++scrollTop;
                changed = true;
            }

            if ( changed ) {
                redraw();
                display.render();
            }

            if ( le.MouseClickLeft( btnOk.area() ) || Game::HotKeyPressEvent( Game::HotKeyEvent::DEFAULT_OKAY ) )
                return selected;

            if ( le.MouseClickLeft( btnCancel.area() ) || Game::HotKeyCloseWindow() )
                return currentIndex;
        }

        return currentIndex;
    }

    // HoMM1-specific scenario selection screen.
    // Uses NEWGAME.BMP as the right panel background, NEWGAME.ICN for UI elements,
    // and BTNNEWGM.ICN for OKAY/CANCEL buttons.
    //
    // NEWGAME.ICN frame layout (29 frames):
    //   0       : 251×20  scenario name bar
    //   1-4     : 65×65   chess pieces (Easy, Normal, Hard, Expert) — unselected
    //   5-8     : 65×65   chess pieces (Easy, Normal, Hard, Expert) — selected (brighter)
    //   9       : 65×65   extra variant
    //   10-13   : 59×44   balance scales (Fool, Easy, Average, Hard) — AI strength icons
    //   14-17   : 59×44   balance scales — alternate states
    //   18      : 249×20  scenario list row bar
    //   19      : 24×20   scroll arrow up
    //   20      : 24×20   scroll arrow down
    //   21-22   : 96×25   small buttons (unused here)
    //   23-24   : 96×25   small buttons (unused here)
    //   25      : 71×71   color gem / king-of-hill indicator
    //   26      : 65×65   extra large icon
    //   27-28   : 44×39   small icons
    //
    // BTNNEWGM.ICN frame layout (8 frames, 127×63 each):
    //   0/1     : OKAY released / pressed
    //   2/3     : CANCEL released / pressed
    // Player colors available for selection in HoMM1 (index 0-3 cycles Red→Yellow→Green→Blue).

    fheroes2::GameMode ChooseHoMM1Map( MapsFileInfoList & maps, const int /*humanPlayerCount*/ )
    {
        assert( !maps.empty() );

        fheroes2::Display & display = fheroes2::Display::instance();

        // Draw the HEROES.BMP background centered on screen.
        const fheroes2::Sprite & bg = fheroes2::AGG::GetICN( ICN::HEROES, 0 );
        const int32_t bgX = bg.empty() ? 0 : ( display.width() - bg.width() ) / 2;
        const int32_t bgY = bg.empty() ? 0 : ( display.height() - bg.height() ) / 2;

        display.fill( 0 );
        if ( !bg.empty() ) {
            fheroes2::Copy( bg, 0, 0, display, bgX, bgY, bg.width(), bg.height() );
        }

        // Right panel: NEWGAME.BMP (322×459) — contains all static art including pre-drawn
        // chess pieces, balance scales, color gem slot, and king-of-hill indicator.
        const fheroes2::Sprite & panel = fheroes2::AGG::GetICN( ICN::H1NEWGAME_BMP, 0 );
        const int32_t panelW = panel.empty() ? 322 : panel.width();
        const int32_t panelH = panel.empty() ? 459 : panel.height();
        // Panel sits flush against the right edge of the background.
        const int32_t bgRight = bg.empty() ? display.width() : bgX + bg.width();
        const int32_t panelX  = bgRight - panelW;
        const int32_t panelY  = ( display.height() - panelH ) / 2;

        // Default selection: AES31000.MAP (first scenario in HoMM1).
        int selectedMap = 0;
        for ( int i = 0; i < static_cast<int>( maps.size() ); ++i ) {
            const std::string base = StringLower( System::GetFileName( maps[i].filename ) );
            if ( base == "aes31000.map" ) {
                selectedMap = i;
                break;
            }
        }
        int difficulty  = 1; // 0=Easy, 1=Normal, 2=Hard, 3=Expert
        int aiStrength[3] = { 2, 2, 2 }; // per-opponent: 0=Disable, 1=Dumb, 2=Average, 3=Smart, 4=Genius
        int colorIndex  = 0; // cycles Red→Yellow→Green→Blue
        bool kingOfHill = false;

        // -----------------------------------------------------------------------
        // Positions are derived from NEWGAME.BIN (all offsets relative to panel).
        // -----------------------------------------------------------------------

        // Difficulty chess pieces: x=22/93/164/235, y=39, 65×65.
        constexpr int32_t chessOffX[4] = { 22, 93, 164, 235 };
        constexpr int32_t chessUnselY  = 39;
        constexpr int32_t chessW       = 65;
        constexpr int32_t chessH       = 65;

        fheroes2::Rect chessRects[4];
        for ( int i = 0; i < 4; ++i )
            chessRects[i] = { panelX + chessOffX[i], panelY + chessUnselY, chessW, chessH };

        // Balance scales (AI strength): x=55/127/199, y=149, 65×65.
        constexpr int32_t scaleOffX[3] = { 55, 127, 199 };
        constexpr int32_t scaleY       = 149;
        constexpr int32_t scaleW       = 65;
        constexpr int32_t scaleH       = 65;

        fheroes2::Rect scaleRects[3];
        for ( int i = 0; i < 3; ++i )
            scaleRects[i] = { panelX + scaleOffX[i], panelY + scaleY, scaleW, scaleH };

        // Color gem: x=51, y=270, 59×44.
        constexpr int32_t gemOffX = 51;
        constexpr int32_t gemOffY = 270;
        constexpr int32_t gemW    = 59;
        constexpr int32_t gemH    = 44;
        const fheroes2::Rect gemRect{ panelX + gemOffX, panelY + gemOffY, gemW, gemH };

        // King-of-Hill indicator: x=210, y=272, 44×39.
        constexpr int32_t kohOffX = 210;
        constexpr int32_t kohOffY = 272;
        constexpr int32_t kohW    = 44;
        constexpr int32_t kohH    = 39;
        const fheroes2::Rect kohRect{ panelX + kohOffX, panelY + kohOffY, kohW, kohH };

        // Scenario bar: x=24, y=354, 249×20.
        // Two stacked scroll arrows (each 24×20) at x=273: up at y=354, down at y=374.
        constexpr int32_t barOffX   = 24;
        constexpr int32_t barOffY   = 354;
        constexpr int32_t barW      = 249;
        constexpr int32_t barH      = 20;
        constexpr int32_t arrowOffX = 273;
        constexpr int32_t arrowW    = 24;
        constexpr int32_t arrowH    = 20;
        const fheroes2::Rect scenBarRect  { panelX + barOffX,   panelY + barOffY,           barW,   barH   };
        const fheroes2::Rect arrowUpRect  { panelX + arrowOffX, panelY + barOffY,            arrowW, arrowH };
        const fheroes2::Rect arrowDownRect{ panelX + arrowOffX, panelY + barOffY + arrowH,   arrowW, arrowH };

        // OKAY / CANCEL click areas (pre-drawn in BMP): x=24/201, y=412, 96×25.
        constexpr int32_t btnOffY    = 412;
        constexpr int32_t btnW       = 96;
        constexpr int32_t btnH       = 25;
        constexpr int32_t btnOkOffX  = 24;
        constexpr int32_t btnCxOffX  = 201;
        const fheroes2::Rect okRect    { panelX + btnOkOffX, panelY + btnOffY, btnW, btnH };
        const fheroes2::Rect cancelRect{ panelX + btnCxOffX, panelY + btnOffY, btnW, btnH };

        // AI options: 0=Disable, 1=Dumb, 2=Average, 3=Smart, 4=Genius
        // ICN frames (0-based): disable=5, dumb=6, average=7, smart=8, genius=9
        static const char * const aiLabels[]   = { "Disabled", "Dumb", "Average", "Smart", "Genius" };
        constexpr int             aiFrames[]   = { 5, 6, 7, 8, 9 };

        // Difficulty ICN frames (0-based): Easy=1, Normal=2, Hard=3, Expert=4
        constexpr int diffICNFrames[] = { 1, 2, 3, 4 };
        static const int diffValues[] = { Difficulty::EASY, Difficulty::NORMAL, Difficulty::HARD, Difficulty::EXPERT };

        // Color ICN frames (0-based): Blue=11, Green=13, Red=15, Yellow=17
        // h1Colors order: Red(0), Yellow(1), Green(2), Blue(3)
        constexpr int colorFrames[] = { 15, 17, 13, 11 };

        bool arrowUpPressed = false;

        const auto redrawAll = [&]() {
            // Repaint the background then the panel (restores all pre-drawn art).
            display.fill( 0 );
            if ( !bg.empty() )
                fheroes2::Copy( bg, 0, 0, display, bgX, bgY, bg.width(), bg.height() );
            if ( !panel.empty() )
                fheroes2::Blit( panel, display, panelX, panelY );

            // --- Section labels ---
            {
                // "Choose Game Difficulty:" centered at panel y+21.
                fheroes2::Text lbl( _( "Choose Game Difficulty:" ), fheroes2::FontType::normalWhite() );
                lbl.draw( panelX, panelY + 21, panelW, display );

                // "Customize Opponents:" centered at panel y+131.
                lbl.set( _( "Customize Opponents:" ), fheroes2::FontType::normalWhite() );
                lbl.draw( panelX, panelY + 131, panelW, display );

                // "Choose Color:" left half; "King of the Hill:" right half - both at y+253.
                lbl.set( _( "Choose Color:" ), fheroes2::FontType::normalWhite() );
                lbl.draw( panelX, panelY + 253, panelW / 2, display );

                lbl.set( _( "King of the Hill:" ), fheroes2::FontType::normalWhite() );
                lbl.draw( panelX + panelW / 2, panelY + 253, panelW / 2, display );

                // "Choose Scenario:" centered at panel y+337.
                lbl.set( _( "Choose Scenario:" ), fheroes2::FontType::normalWhite() );
                lbl.draw( panelX, panelY + 337, panelW, display );

                // Difficulty rating at panel y+383.
                std::string ratingStr( _( "Difficulty Rating: %{rating}%" ) );
                StringReplace( ratingStr, "%{rating}", diffValues[difficulty] );
                lbl.set( ratingStr, fheroes2::FontType::normalWhite() );
                lbl.draw( panelX, panelY + 383, panelW, display );
            }

            // --- Difficulty icons (ICN frames 1-4: Easy/Normal/Hard/Expert) ---
            // Draw all four icons; selected one gets a yellow highlight border.
            for ( int i = 0; i < 4; ++i ) {
                const fheroes2::Sprite & icon = fheroes2::AGG::GetICN( ICN::H1NEWGAME_ICN, diffICNFrames[i] );
                if ( !icon.empty() ) {
                    const int32_t ix = panelX + chessOffX[i] + ( chessW - icon.width() ) / 2;
                    const int32_t iy = panelY + chessUnselY + ( chessH - icon.height() ) / 2;
                    fheroes2::Blit( icon, display, ix, iy );
                    if ( i == difficulty ) {
                        fheroes2::DrawRect( display, { ix - 2, iy - 2, icon.width() + 4, icon.height() + 4 },
                                            fheroes2::GetColorId( 220, 200, 20 ) );
                    }
                }
            }

            // --- AI strength icons (ICN frames 5-9: Disable/Dumb/Average/Smart/Genius) ---
            // Each slot is independent: left=AI1, middle=AI2, right=AI3.
            for ( int i = 0; i < 3; ++i ) {
                const fheroes2::Sprite & icon = fheroes2::AGG::GetICN( ICN::H1NEWGAME_ICN, aiFrames[aiStrength[i]] );
                if ( !icon.empty() ) {
                    const int32_t ix = panelX + scaleOffX[i] + ( scaleW - icon.width() ) / 2;
                    const int32_t iy = panelY + scaleY + ( scaleH - icon.height() ) / 2;
                    fheroes2::Blit( icon, display, ix, iy );
                }
                // Label below each slot.
                fheroes2::Text lbl( aiLabels[aiStrength[i]], fheroes2::FontType::smallWhite() );
                lbl.draw( panelX + scaleOffX[i] + ( scaleW - lbl.width() ) / 2, panelY + scaleY + scaleH + 2, display );
            }

            // --- Color icon (ICN frames: Red=15, Yellow=17, Green=13, Blue=11) ---
            {
                const fheroes2::Sprite & icon = fheroes2::AGG::GetICN( ICN::H1NEWGAME_ICN, colorFrames[colorIndex] );
                if ( !icon.empty() ) {
                    fheroes2::Blit( icon, display, panelX + gemOffX + ( gemW - icon.width() ) / 2,
                                    panelY + gemOffY + ( gemH - icon.height() ) / 2 );
                }
            }

            // --- King-of-Hill icon (ICN frame 27=box, 28=checked) ---
            {
                const fheroes2::Sprite & icon = fheroes2::AGG::GetICN( ICN::H1NEWGAME_ICN, kingOfHill ? 28 : 27 );
                if ( !icon.empty() ) {
                    fheroes2::Blit( icon, display, panelX + kohOffX + ( kohW - icon.width() ) / 2,
                                    panelY + kohOffY + ( kohH - icon.height() ) / 2 );
                }
            }

            // --- Scenario bar: ICN frame 18 background + map name in bold, frame 19/20 arrow ---
            {
                // Bar background (frame 18, 0-based).
                const fheroes2::Sprite & barSprite = fheroes2::AGG::GetICN( ICN::H1NEWGAME_ICN, 18 );
                if ( !barSprite.empty() ) {
                    fheroes2::Blit( barSprite, display, panelX + barOffX, panelY + barOffY );
                }
                // Map name in normal (bold) white font centered vertically over the bar.
                fheroes2::Text mapName( maps[selectedMap].name, fheroes2::FontType::normalWhite() );
                mapName.fitToOneRow( barW - 4 );
                const int32_t textY = panelY + barOffY + ( barH - mapName.height() ) / 2;
                mapName.draw( panelX + barOffX + 2, textY, display );

                // Up arrow: frame 19 normal, frame 20 pressed (0-based).
                const int arrowFrame = arrowUpPressed ? 20 : 19;
                const fheroes2::Sprite & arrowSprite = fheroes2::AGG::GetICN( ICN::H1NEWGAME_ICN, arrowFrame );
                if ( !arrowSprite.empty() ) {
                    fheroes2::Blit( arrowSprite, display, panelX + arrowOffX, panelY + barOffY );
                }
            }
        };

        redrawAll();

        if ( Game::validateDisplayFadeIn() ) {
            fheroes2::fadeInDisplay();
        }
        else {
            display.render();
        }

        Settings & conf = Settings::Get();
        LocalEvent & le = LocalEvent::Get();

        while ( le.HandleEvents() ) {
            bool changed = false;

            // Difficulty: click a chess piece to select.
            for ( int i = 0; i < 4; ++i ) {
                if ( le.MouseClickLeft( chessRects[i] ) && difficulty != i ) {
                    difficulty = i;
                    changed = true;
                }
            }

            // AI strength: each scale slot cycles its own opponent independently.
            for ( int i = 0; i < 3; ++i ) {
                if ( le.MouseClickLeft( scaleRects[i] ) ) {
                    aiStrength[i] = ( aiStrength[i] + 1 ) % 5;
                    changed = true;
                }
            }

            // Color gem: cycle through colors.
            if ( le.MouseClickLeft( gemRect ) ) {
                colorIndex = ( colorIndex + 1 ) % 4;
                changed = true;
            }

            // King of Hill: toggle.
            if ( le.MouseClickLeft( kohRect ) ) {
                kingOfHill = !kingOfHill;
                changed = true;
            }

            // Up arrow pressed state visual update.
            {
                const bool nowPressed = le.isMouseLeftButtonPressedInArea( arrowUpRect );
                if ( nowPressed != arrowUpPressed ) {
                    arrowUpPressed = nowPressed;
                    changed = true;
                }
            }

            // Up arrow or name bar click: open map selection dialog.
            if ( le.MouseClickLeft( arrowUpRect ) || le.MouseClickLeft( scenBarRect ) ) {
                arrowUpPressed = false;
                const int newMap = showHoMM1MapSelectDialog( maps, selectedMap );
                if ( newMap != selectedMap ) {
                    selectedMap = newMap;
                }
                // Always redraw to restore the NEWGAME panel after the dialog.
                changed = true;
            }

            // Down arrow: next scenario.
            if ( le.MouseClickLeft( arrowDownRect ) ) {
                selectedMap = ( selectedMap + 1 ) % static_cast<int>( maps.size() );
                changed = true;
            }

            if ( changed ) {
                redrawAll();
                display.render();
            }

            if ( le.MouseClickLeft( okRect ) || Game::HotKeyPressEvent( Game::HotKeyEvent::DEFAULT_OKAY ) ) {
                conf.setCurrentMapInfo( maps[selectedMap] );
                conf.SetGameType( Game::TYPE_STANDARD );
                conf.SetGameDifficulty( diffValues[difficulty] );
                conf.GetPlayers().SetStartGame();
                return fheroes2::GameMode::START_GAME;
            }

            if ( le.MouseClickLeft( cancelRect ) || Game::HotKeyPressEvent( Game::HotKeyEvent::DEFAULT_CANCEL ) ) {
                return fheroes2::GameMode::MAIN_MENU;
            }
        }

        return fheroes2::GameMode::MAIN_MENU;
    }

    fheroes2::GameMode ChooseNewMap( MapsFileInfoList & lists, const int humanPlayerCount )
    {
        assert( !lists.empty() );

        // setup cursor
        const CursorRestorer cursorRestorer( true, Cursor::POINTER );

        fheroes2::Display & display = fheroes2::Display::instance();

        Settings & conf = Settings::Get();
        const bool isEvilInterface = conf.isEvilInterfaceEnabled();

        fheroes2::drawMainMenuScreen();

        fheroes2::StandardWindow background( 388, 397, true, display );

        const fheroes2::Rect roi( background.activeArea() );

        const fheroes2::Point pointDifficultyInfo( roi.x + 8, roi.y + 79 );
        const fheroes2::Point pointOpponentInfo( roi.x + 8, roi.y + 181 );
        const fheroes2::Point pointClassInfo( roi.x + 8, roi.y + 265 );

        const fheroes2::Sprite & scenarioBox = fheroes2::AGG::GetICN( isEvilInterface ? ICN::METALLIC_BORDERED_TEXTBOX_EVIL : ICN::METALLIC_BORDERED_TEXTBOX_GOOD, 0 );

        // If the ICN is missing (e.g. HoMM1 data), fall back to a plain rect spanning the dialog.
        const fheroes2::Rect scenarioBoxRoi = ( scenarioBox.width() > 0 )
            ? fheroes2::Rect( roi.x + ( roi.width - scenarioBox.width() ) / 2, roi.y + 24, scenarioBox.width(), scenarioBox.height() )
            : fheroes2::Rect( roi.x + 4, roi.y + 24, roi.width - 8, 25 );

        if ( scenarioBox.width() > 0 ) {
            fheroes2::Copy( scenarioBox, 0, 0, display, scenarioBoxRoi );
            fheroes2::addGradientShadow( scenarioBox, display, scenarioBoxRoi.getPosition(), { -5, 5 } );
        }

        const fheroes2::Sprite & difficultyCursor = fheroes2::AGG::GetICN( ICN::NGEXTRA, 62 );

        const int32_t difficultyCursorWidth = difficultyCursor.width();
        const int32_t difficultyCursorHeight = difficultyCursor.height();

        // Difficulty selection areas vector.
        std::vector<fheroes2::Rect> coordDifficulty;
        coordDifficulty.reserve( 5 );

        coordDifficulty.emplace_back( roi.x + 8, roi.y + 78, difficultyCursorWidth, difficultyCursorHeight );
        coordDifficulty.emplace_back( roi.x + 85, roi.y + 78, difficultyCursorWidth, difficultyCursorHeight );
        coordDifficulty.emplace_back( roi.x + 161, roi.y + 78, difficultyCursorWidth, difficultyCursorHeight );
        coordDifficulty.emplace_back( roi.x + 238, roi.y + 78, difficultyCursorWidth, difficultyCursorHeight );
        coordDifficulty.emplace_back( roi.x + 315, roi.y + 78, difficultyCursorWidth, difficultyCursorHeight );

        const int32_t buttonSelectWidth = fheroes2::AGG::GetICN( ICN::BUTTON_MAP_SELECT_GOOD, 0 ).width();

        fheroes2::Button buttonSelectMaps( scenarioBoxRoi.x + scenarioBoxRoi.width - 6 - buttonSelectWidth, scenarioBoxRoi.y + 5,
                                           isEvilInterface ? ICN::BUTTON_MAP_SELECT_EVIL : ICN::BUTTON_MAP_SELECT_GOOD, 0, 1 );
        buttonSelectMaps.draw();

        fheroes2::Button buttonOk;
        fheroes2::Button buttonCancel;

        const fheroes2::Point buttonOffset( 20, 6 );

        const int buttonOkIcn = isEvilInterface ? ICN::BUTTON_SMALL_OKAY_EVIL : ICN::BUTTON_SMALL_OKAY_GOOD;
        background.renderButton( buttonOk, buttonOkIcn, 0, 1, buttonOffset, fheroes2::StandardWindow::Padding::BOTTOM_LEFT );

        const int buttonCancelIcn = isEvilInterface ? ICN::BUTTON_SMALL_CANCEL_EVIL : ICN::BUTTON_SMALL_CANCEL_GOOD;
        background.renderButton( buttonCancel, buttonCancelIcn, 0, 1, buttonOffset, fheroes2::StandardWindow::Padding::BOTTOM_RIGHT );

        const Maps::FileInfo & mapInfo = [&lists, &conf = std::as_const( conf )]() {
            const Maps::FileInfo & currentMapinfo = conf.getCurrentMapInfo();

            if ( currentMapinfo.filename.empty() ) {
                return lists.front();
            }

            // Make sure that the current map actually exists in the map's list
            const auto iter = std::find_if( lists.begin(), lists.end(), [&currentMapinfo]( const Maps::FileInfo & info ) {
                return info.name == currentMapinfo.name && info.filename == currentMapinfo.filename;
            } );
            if ( iter == lists.end() ) {
                return lists.front();
            }

            return *iter;
        }();

        Players & players = conf.GetPlayers();

        showCurrentlySelectedMapInfoInTextSupportMode( mapInfo );
        conf.setCurrentMapInfo( mapInfo );
        updatePlayers( players, humanPlayerCount );

        // Load players parameters saved from the previous call of the scenario info dialog.
        Game::LoadPlayers( mapInfo.filename, players );

        Interface::PlayersInfo playersInfo;
        playersInfo.UpdateInfo( players, pointOpponentInfo, pointClassInfo );

        DrawScenarioStaticInfo( roi );
        RedrawDifficultyInfo( pointDifficultyInfo );

        const int icnIndex = isEvilInterface ? 1 : 0;

        // Draw difficulty icons.
        for ( int i = 0; i < 5; ++i ) {
            const fheroes2::Sprite & icon = fheroes2::AGG::GetICN( ICN::DIFFICULTY_ICON_EASY + i, icnIndex );
            fheroes2::Copy( icon, 0, 0, display, coordDifficulty[i] );
            fheroes2::addGradientShadow( icon, display, { coordDifficulty[i].x, coordDifficulty[i].y }, { -5, 5 } );
        }

        // We calculate the allowed text width according to the select button's width while ensuring symmetric placement of the map title.
        const int32_t boxBorder = 6;
        const int32_t overallBoxTextAreaWidth = ( scenarioBoxRoi.width - ( 2 * boxBorder ) );
        const int32_t maxTextAreaWidth = std::max( 1, overallBoxTextAreaWidth - buttonSelectWidth );

        const fheroes2::Rect maxTextRoi{ scenarioBoxRoi.x + boxBorder, scenarioBoxRoi.y + 5, maxTextAreaWidth, 19 };

        const int32_t halfBoxTextAreaWidth = overallBoxTextAreaWidth / 2;
        const int32_t rightSideAvailableTextWidth
            = ( halfBoxTextAreaWidth > buttonSelectWidth ) ? ( halfBoxTextAreaWidth - buttonSelectWidth ) : ( buttonSelectWidth - halfBoxTextAreaWidth );

        const fheroes2::Rect centeredTextRoi{ scenarioBoxRoi.x + boxBorder + buttonSelectWidth, scenarioBoxRoi.y + 5, 2 * rightSideAvailableTextWidth, 19 };

        // Set up restorers.
        fheroes2::ImageRestorer mapTitleArea( display, maxTextRoi.x, maxTextRoi.y, maxTextRoi.width, maxTextRoi.height );
        fheroes2::ImageRestorer opponentsArea( display, roi.x, pointOpponentInfo.y, roi.width, 65 );
        fheroes2::ImageRestorer classArea( display, roi.x, pointClassInfo.y, roi.width, 69 );
        fheroes2::ImageRestorer handicapArea( display, roi.x, pointClassInfo.y + 69, roi.width, 31 );
        fheroes2::ImageRestorer ratingArea( display, buttonOk.area().x + buttonOk.area().width, buttonOk.area().y,
                                            roi.width - buttonOk.area().width - buttonCancel.area().width - 20 * 2, buttonOk.area().height );

        // Map name
        RedrawMapTitle( conf, maxTextRoi, centeredTextRoi );

        playersInfo.RedrawInfo( false );

        fheroes2::Rect ratingRoi = RedrawRatingInfo( roi.getPosition(), roi.width );

        fheroes2::MovableSprite levelCursor( difficultyCursor );
        const int32_t levelCursorOffset = 3;

        switch ( Game::getDifficulty() ) {
        case Difficulty::EASY:
            levelCursor.setPosition( coordDifficulty[0].x - levelCursorOffset, coordDifficulty[0].y - levelCursorOffset );
            break;
        case Difficulty::NORMAL:
            levelCursor.setPosition( coordDifficulty[1].x - levelCursorOffset, coordDifficulty[1].y - levelCursorOffset );
            break;
        case Difficulty::HARD:
            levelCursor.setPosition( coordDifficulty[2].x - levelCursorOffset, coordDifficulty[2].y - levelCursorOffset );
            break;
        case Difficulty::EXPERT:
            levelCursor.setPosition( coordDifficulty[3].x - levelCursorOffset, coordDifficulty[3].y - levelCursorOffset );
            break;
        case Difficulty::IMPOSSIBLE:
            levelCursor.setPosition( coordDifficulty[4].x - levelCursorOffset, coordDifficulty[4].y - levelCursorOffset );
            break;
        default:
            // Did you add a new difficulty mode? Add the corresponding case above!
            assert( 0 );
            break;
        }

        levelCursor.redraw();

        fheroes2::validateFadeInAndRender();

        fheroes2::GameMode result = fheroes2::GameMode::QUIT_GAME;

        outputNewGameInTextSupportMode();

        LocalEvent & le = LocalEvent::Get();

        while ( true ) {
            if ( !le.HandleEvents( true, true ) ) {
                if ( Game::processExitEvent() == fheroes2::GameMode::QUIT_GAME ) {
                    fheroes2::fadeOutDisplay();

                    return fheroes2::GameMode::QUIT_GAME;
                }

                continue;
            }

            // press button
            buttonSelectMaps.drawOnState( le.isMouseLeftButtonPressedAndHeldInArea( buttonSelectMaps.area() ) );
            buttonOk.drawOnState( le.isMouseLeftButtonPressedAndHeldInArea( buttonOk.area() ) );
            buttonCancel.drawOnState( le.isMouseLeftButtonPressedAndHeldInArea( buttonCancel.area() ) );

            // click select
            if ( HotKeyPressEvent( Game::HotKeyEvent::MAIN_MENU_SELECT_MAP ) || le.MouseClickLeft( buttonSelectMaps.area() ) ) {
                const Maps::FileInfo * fi = Dialog::SelectScenario( lists, false );
                if ( lists.empty() ) {
                    // This can happen if all maps have been deleted.
                    result = fheroes2::GameMode::MAIN_MENU;
                    break;
                }

                // The previous dialog might still have a pressed button event. We have to clean the state.
                le.reset();

                if ( fi && fi->filename != conf.getCurrentMapInfo().filename ) {
                    showCurrentlySelectedMapInfoInTextSupportMode( *fi );

                    // The map is changed. Update the map data and do default initialization of players.
                    conf.setCurrentMapInfo( *fi );

                    mapTitleArea.restore();
                    RedrawMapTitle( conf, maxTextRoi, centeredTextRoi );

                    opponentsArea.restore();
                    classArea.restore();
                    handicapArea.restore();
                    ratingArea.restore();

                    updatePlayers( players, humanPlayerCount );
                    playersInfo.UpdateInfo( players, pointOpponentInfo, pointClassInfo );

                    playersInfo.resetSelection();
                    playersInfo.RedrawInfo( false );

                    ratingRoi = RedrawRatingInfo( roi.getPosition(), roi.width );
                    levelCursor.setPosition( coordDifficulty[Game::getDifficulty()].x - levelCursorOffset,
                                             coordDifficulty[Game::getDifficulty()].y - levelCursorOffset ); // From 0 to 4, see: Difficulty enum
                }
                display.render();

                outputNewGameInTextSupportMode();
            }
            else if ( Game::HotKeyPressEvent( Game::HotKeyEvent::DEFAULT_CANCEL ) || le.MouseClickLeft( buttonCancel.area() ) ) {
                result = fheroes2::GameMode::MAIN_MENU;
                break;
            }

            if ( Game::HotKeyPressEvent( Game::HotKeyEvent::DEFAULT_OKAY ) || le.MouseClickLeft( buttonOk.area() ) ) {
                DEBUG_LOG( DBG_GAME, DBG_INFO, "select maps: " << conf.getCurrentMapInfo().filename << ", difficulty: " << Difficulty::String( Game::getDifficulty() ) )
                result = fheroes2::GameMode::START_GAME;

                // Fade-out screen before starting a scenario.
                fheroes2::fadeOutDisplay();
                break;
            }

            if ( le.MouseClickLeft( roi ) ) {
                const int32_t index = GetRectIndex( coordDifficulty, le.getMouseCursorPos() );

                // select difficulty
                if ( 0 <= index ) {
                    levelCursor.setPosition( coordDifficulty[index].x - levelCursorOffset, coordDifficulty[index].y - levelCursorOffset );
                    levelCursor.redraw();
                    Game::saveDifficulty( index );
                    ratingArea.restore();
                    ratingRoi = RedrawRatingInfo( roi.getPosition(), roi.width );

                    display.render( roi );
                }
                // playersInfo
                else if ( playersInfo.QueueEventProcessing() ) {
                    opponentsArea.restore();
                    classArea.restore();
                    handicapArea.restore();
                    playersInfo.RedrawInfo( false );

                    display.render( roi );
                }
            }
            else if ( ( le.isMouseWheelUp() || le.isMouseWheelDown() ) && playersInfo.QueueEventProcessing() ) {
                playersInfo.resetSelection();
                opponentsArea.restore();
                classArea.restore();
                handicapArea.restore();
                playersInfo.RedrawInfo( false );

                display.render( roi );
            }

            if ( le.isMouseRightButtonPressedInArea( roi ) ) {
                if ( le.isMouseRightButtonPressedInArea( buttonSelectMaps.area() ) ) {
                    fheroes2::showStandardTextMessage( _( "Scenario" ), _( "Click here to select which scenario to play." ), Dialog::ZERO );
                }
                else if ( 0 <= GetRectIndex( coordDifficulty, le.getMouseCursorPos() ) ) {
                    fheroes2::showStandardTextMessage(
                        _( "Game Difficulty" ),
                        _( "This lets you change the starting difficulty at which you will play. Higher difficulty levels start you off with fewer resources, and at the higher settings, give extra resources to the computer." ),
                        Dialog::ZERO );
                }
                else if ( le.isMouseRightButtonPressedInArea( ratingRoi ) ) {
                    fheroes2::showStandardTextMessage(
                        _( "Difficulty Rating" ),
                        _( "The difficulty rating reflects a combination of various settings for your game. This number will be applied to your final score." ),
                        Dialog::ZERO );
                }
                else if ( le.isMouseRightButtonPressedInArea( buttonOk.area() ) ) {
                    fheroes2::showStandardTextMessage( _( "Okay" ), _( "Click to accept these settings and start a new game." ), Dialog::ZERO );
                }
                else if ( le.isMouseRightButtonPressedInArea( buttonCancel.area() ) ) {
                    fheroes2::showStandardTextMessage( _( "Cancel" ), _( "Click to return to the main menu." ), Dialog::ZERO );
                }
                else {
                    playersInfo.QueueEventProcessing();
                }
            }
        }

        // Save the changes players parameters before closing this dialog.
        Game::SavePlayers( conf.getCurrentMapInfo().filename, players );

        return result;
    }

    fheroes2::GameMode LoadNewMap()
    {
        Settings & conf = Settings::Get();

        conf.GetPlayers().SetStartGame();

        const Maps::FileInfo & mapInfo = conf.getCurrentMapInfo();

        if ( mapInfo.version == GameVersion::SUCCESSION_WARS || mapInfo.version == GameVersion::PRICE_OF_LOYALTY ) {
            if ( world.LoadMapMP2( mapInfo.filename, ( mapInfo.version == GameVersion::SUCCESSION_WARS ) ) ) {
                return fheroes2::GameMode::START_GAME;
            }

            fheroes2::drawMainMenuScreen();
            fheroes2::showStandardTextMessage( _( "Warning" ), _( "The map is corrupted or doesn't exist." ), Dialog::OK );
            return fheroes2::GameMode::MAIN_MENU;
        }

        if ( mapInfo.version == GameVersion::HOMM1 ) {
            std::vector<uint8_t> mapData;
            if ( mapInfo.filename.size() >= 5 && mapInfo.filename.compare( 0, 5, "[AGG]" ) == 0 ) {
                // AGG-embedded map: strip "[AGG]" prefix.
                mapData = AGG::getDataFromAggFile( mapInfo.filename.substr( 5 ), false );
            }
            else {
                // Filesystem map: read raw bytes from disk.
                StreamFile fs;
                if ( fs.open( mapInfo.filename, "rb" ) ) {
                    mapData = fs.getRaw( fs.size() );
                }
            }

            if ( !mapData.empty() && world.loadHoMM1Map( mapData ) ) {
                return fheroes2::GameMode::START_GAME;
            }

            fheroes2::drawMainMenuScreen();
            fheroes2::showStandardTextMessage( _( "Warning" ), _( "The map is corrupted or doesn't exist." ), Dialog::OK );
            return fheroes2::GameMode::MAIN_MENU;
        }

        assert( mapInfo.version == GameVersion::RESURRECTION );
        if ( world.loadResurrectionMap( mapInfo.filename ) ) {
            return fheroes2::GameMode::START_GAME;
        }

        fheroes2::drawMainMenuScreen();
        fheroes2::showStandardTextMessage( _( "Warning" ), _( "The map is corrupted or doesn't exist." ), Dialog::OK );
        return fheroes2::GameMode::MAIN_MENU;
    }
}

fheroes2::GameMode Game::SelectScenario( const uint8_t humanPlayerCount )
{
    assert( humanPlayerCount >= 1 && humanPlayerCount <= 6 );

    AudioManager::PlayMusicAsync( MUS::MAINMENU, Music::PlaybackMode::RESUME_AND_PLAY_INFINITE );

    MapsFileInfoList maps = Maps::getAllMapFileInfos( humanPlayerCount );
    if ( maps.empty() ) {
        fheroes2::showStandardTextMessage( _( "Warning" ), _( "No maps available!" ), Dialog::OK );
        return fheroes2::GameMode::MAIN_MENU;
    }

    // Use HoMM1-specific scenario selection if all maps are HoMM1 format.
    const bool allHoMM1 = std::all_of( maps.begin(), maps.end(), []( const Maps::FileInfo & fi ) {
        return fi.version == GameVersion::HOMM1;
    } );

    // We must release UI resources for this window before loading a new map. That's why all UI logic is in a separate function.
    const fheroes2::GameMode result = allHoMM1 ? ChooseHoMM1Map( maps, humanPlayerCount ) : ChooseNewMap( maps, humanPlayerCount );
    if ( result != fheroes2::GameMode::START_GAME ) {
        return result;
    }

    return LoadNewMap();
}

int32_t Game::GetStep4Player( const int32_t currentId, const int32_t width, const int32_t totalCount )
{
    return currentId * width * maxNumOfPlayers / totalCount + ( width * ( maxNumOfPlayers - totalCount ) / ( 2 * totalCount ) );
}
