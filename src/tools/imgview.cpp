/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2019 - 2026                                             *
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

// imgview: interactive browser for ICN/BMP files stored in HEROES.AGG.
//
// Usage:  imgview [path/to/HEROES.AGG]
//   Left/Right arrows — previous/next file
//   Up/Down arrows   — previous/next frame within an ICN
//   Q / Escape       — quit

#if defined( __GNUC__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wswitch-default"
#endif

#include <SDL.h>
#include <SDL_ttf.h>

#if defined( __GNUC__ )
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <initializer_list>
#include <string>
#include <vector>

#include "agg_file.h"
#include "image.h"
#include "image_palette.h"
#include "image_tool.h"
#include "serialize.h"

namespace
{
    bool hasExtension( const std::string & fileName, const std::initializer_list<const char *> extensions )
    {
        for ( const char * extension : extensions ) {
            const size_t extensionLength = std::strlen( extension );
            if ( fileName.size() >= extensionLength && fileName.compare( fileName.size() - extensionLength, extensionLength, extension ) == 0 ) {
                return true;
            }
        }

        return false;
    }

    // Window dimensions.
    constexpr int winW = 800;
    constexpr int winH = 600;
    constexpr int statusH = 40; // reserved at the bottom for the status bar

    // Checkerboard tile size for the transparency background.
    constexpr int checkSize = 8;

    // -----------------------------------------------------------------------
    // Font helpers
    // -----------------------------------------------------------------------

    // Returns the path to the first font file that exists on the system, or ""
    // if none can be found.
    std::string findSystemFont()
    {
        static const char * const candidates[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
            "/usr/share/fonts/opentype/ipafont-gothic/ipagp.ttf",
            "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
        };
        for ( const char * p : candidates ) {
            if ( std::filesystem::exists( p ) ) {
                return p;
            }
        }
        return {};
    }

    // -----------------------------------------------------------------------
    // Palette conversion: 6-bit VGA (0-63) → 8-bit RGBA (0-255, A=255).
    // KB.PAL contains 768 bytes: R0,G0,B0, R1,G1,B1, …
    // -----------------------------------------------------------------------
    struct RGBA
    {
        uint8_t r, g, b, a;
    };

    std::array<RGBA, 256> buildRGBAPalette( const uint8_t * pal6 )
    {
        std::array<RGBA, 256> out{};
        for ( int i = 0; i < 256; ++i ) {
            // 6-bit → 8-bit: multiply by 4 (VGA standard).
            out[i].r = static_cast<uint8_t>( pal6[i * 3 + 0] * 4 );
            out[i].g = static_cast<uint8_t>( pal6[i * 3 + 1] * 4 );
            out[i].b = static_cast<uint8_t>( pal6[i * 3 + 2] * 4 );
            out[i].a = 255;
        }
        return out;
    }

    // -----------------------------------------------------------------------
    // Convert an fheroes2::Sprite to a 32-bit RGBA SDL_Surface.
    // Pixels with transform == 1 are rendered transparent (alpha = 0).
    // -----------------------------------------------------------------------
    SDL_Surface * spriteToSurface( const fheroes2::Sprite & sprite, const std::array<RGBA, 256> & pal )
    {
        const int w = sprite.width();
        const int h = sprite.height();
        if ( w <= 0 || h <= 0 ) {
            return nullptr;
        }

        SDL_Surface * surf = SDL_CreateRGBSurface( 0, w, h, 32, 0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u );
        if ( surf == nullptr ) {
            return nullptr;
        }

        SDL_LockSurface( surf );

        const uint8_t * src = sprite.image();
        const uint8_t * trn = sprite.singleLayer() ? nullptr : sprite.transform();
        auto * dst = static_cast<uint32_t *>( surf->pixels );

        const int pixels = w * h;
        for ( int i = 0; i < pixels; ++i ) {
            if ( trn != nullptr && trn[i] == 1 ) {
                // Transparent.
                dst[i] = 0;
            }
            else {
                const RGBA & c = pal[src[i]];
                dst[i] = ( static_cast<uint32_t>( c.a ) << 24 ) | ( static_cast<uint32_t>( c.b ) << 16 ) | ( static_cast<uint32_t>( c.g ) << 8 ) | c.r;
            }
        }

        SDL_UnlockSurface( surf );
        return surf;
    }

    // -----------------------------------------------------------------------
    // Scan sequential HoMM1 sprite data to build per-sprite byte offsets.
    // Each sprite ends with a 0x80 terminator byte.
    // isMonochrome: true for font ICNs (1-byte opcodes), false for colour RLE.
    // -----------------------------------------------------------------------
    std::vector<uint32_t> buildHoMM1SpriteOffsets( const uint8_t * db, const uint32_t dbSize, const uint32_t count, const bool isMonochrome )
    {
        std::vector<uint32_t> offsets( count, 0 );
        uint32_t pos = 0;

        for ( uint32_t i = 0; i < count; ++i ) {
            offsets[i] = pos;
            if ( pos >= dbSize ) {
                break;
            }

            if ( isMonochrome ) {
                while ( pos < dbSize ) {
                    if ( db[pos++] == 0x80 ) {
                        break;
                    }
                }
            }
            else {
                while ( pos < dbSize ) {
                    const uint8_t b = db[pos++];
                    if ( b == 0x80 ) {
                        break;
                    }
                    else if ( b > 0 && b < 0x80 ) {
                        pos += b;
                        if ( pos > dbSize ) {
                            pos = dbSize;
                        }
                    }
                    else if ( b == 0xC0 ) {
                        if ( pos >= dbSize ) {
                            break;
                        }
                        const uint8_t t = db[pos++];
                        if ( ( t & 0x03 ) == 0 && pos < dbSize ) {
                            ++pos;
                        }
                    }
                    else if ( b > 0xC0 ) {
                        if ( b == 0xC1 && pos < dbSize ) {
                            ++pos;
                        }
                        if ( pos < dbSize ) {
                            ++pos;
                        }
                    }
                }
            }
        }

        return offsets;
    }

    // -----------------------------------------------------------------------
    // Decode all frames from a raw ICN file.  Handles both HoMM1 (12-byte
    // headers, scanned offsets) and HoMM2 (13-byte headers, stored offsets).
    // -----------------------------------------------------------------------
    std::vector<fheroes2::Sprite> decodeICN( const std::vector<uint8_t> & raw, const std::string & fileName )
    {
        std::vector<fheroes2::Sprite> frames;
        constexpr size_t preamble = 6; // count(u16) + blockSize(u32)

        if ( raw.size() < preamble ) {
            return frames;
        }

        ROStreamBuf stream( raw );
        const uint32_t count     = stream.getLE16();
        const uint32_t blockSize = stream.getLE32();

        if ( count == 0 || blockSize == 0 ) {
            return frames;
        }

        // HoMM1 detection: blockSize covers both header table and sprite data,
        // so blockSize + preamble == raw.size().
        const bool isHoMM1 = ( blockSize + preamble == raw.size() );

        if ( isHoMM1 ) {
            constexpr uint32_t hdrStride = 12; // ox(2)+oy(2)+w(2)+h(2)+af(1)+adv(1)+pad(2)
            const uint32_t dataBlockOffset = static_cast<uint32_t>( preamble ) + count * hdrStride;
            if ( dataBlockOffset > raw.size() ) {
                return frames;
            }

            const uint32_t dataOnlySize = static_cast<uint32_t>( raw.size() ) - dataBlockOffset;

            // Font ICNs use monochrome RLE; all other HoMM1 ICNs use colour RLE.
            const bool isMonoFont = ( fileName.find( "FONT" ) != std::string::npos );

            const uint8_t * db = raw.data() + dataBlockOffset;
            const std::vector<uint32_t> spriteOffsets = buildHoMM1SpriteOffsets( db, dataOnlySize, count, isMonoFont );

            frames.reserve( count );
            for ( uint32_t i = 0; i < count; ++i ) {
                stream.seek( preamble + i * hdrStride );

                fheroes2::ICNHeader hdr;
                hdr.offsetX        = static_cast<int16_t>( stream.getLE16() );
                hdr.offsetY        = static_cast<int16_t>( stream.getLE16() );
                hdr.width          = stream.getLE16();
                hdr.height         = stream.getLE16();
                stream.get(); // af byte
                stream.get(); // advance byte
                stream.getLE16(); // padding

                hdr.animationFrames = isMonoFont ? 0x20u : 0u;
                hdr.offsetData      = count * hdrStride + spriteOffsets[i];

                if ( hdr.width == 0 || hdr.height == 0 ) {
                    frames.emplace_back();
                    continue;
                }

                const uint32_t dataSize = ( i + 1 != count ) ? ( spriteOffsets[i + 1] - spriteOffsets[i] ) : ( dataOnlySize - spriteOffsets[i] );
                const size_t   absOff   = preamble + static_cast<size_t>( hdr.offsetData );

                if ( absOff >= raw.size() || absOff + dataSize > raw.size() ) {
                    frames.emplace_back();
                    continue;
                }

                const uint8_t * start = raw.data() + absOff;
                frames.push_back( fheroes2::decodeICNSprite( start, start + dataSize, hdr ) );
            }
        }
        else {
            // HoMM2 ICN: 13-byte headers, offsetData stored in each header.
            constexpr size_t hdrStride = 13;
            if ( raw.size() < preamble + count * hdrStride ) {
                return frames;
            }

            frames.reserve( count );
            for ( uint32_t i = 0; i < count; ++i ) {
                stream.seek( preamble + i * hdrStride );

                fheroes2::ICNHeader hdr;
                stream >> hdr;

                if ( hdr.width == 0 || hdr.height == 0 ) {
                    frames.emplace_back();
                    continue;
                }

                uint32_t dataSize = 0;
                if ( i + 1 != count ) {
                    fheroes2::ICNHeader next;
                    stream >> next;
                    dataSize = next.offsetData - hdr.offsetData;
                }
                else {
                    dataSize = blockSize - hdr.offsetData;
                }

                const size_t absOff = preamble + static_cast<size_t>( hdr.offsetData );
                if ( absOff >= raw.size() || absOff + dataSize > raw.size() ) {
                    frames.emplace_back();
                    continue;
                }

                const uint8_t * start = raw.data() + absOff;
                frames.push_back( fheroes2::decodeICNSprite( start, start + dataSize, hdr ) );
            }
        }

        return frames;
    }

    // -----------------------------------------------------------------------
    // Decode a HoMM1 BMP file (raw palette indices, 6-byte header).
    // Format: byte0=indicator, byte1=unused, bytes2-3=width(u16LE),
    //         bytes4-5=height(u16LE), then width*height raw palette indices.
    // -----------------------------------------------------------------------
    fheroes2::Sprite decodeHoMM1BMP( const std::vector<uint8_t> & raw )
    {
        if ( raw.size() < 6 ) {
            return {};
        }

        const uint32_t w = static_cast<uint32_t>( raw[2] ) | ( static_cast<uint32_t>( raw[3] ) << 8 );
        const uint32_t h = static_cast<uint32_t>( raw[4] ) | ( static_cast<uint32_t>( raw[5] ) << 8 );

        if ( w == 0 || h == 0 || raw.size() != 6 + w * h ) {
            return {};
        }

        fheroes2::Sprite sprite( static_cast<int32_t>( w ), static_cast<int32_t>( h ), 0, 0 );
        sprite.reset();

        const uint8_t * pixels = raw.data() + 6;
        uint8_t * image     = sprite.image();
        uint8_t * transform = sprite.transform();

        for ( uint32_t i = 0; i < w * h; ++i ) {
            image[i]     = pixels[i];
            transform[i] = 0; // fully opaque
        }

        return sprite;
    }

    // -----------------------------------------------------------------------
    // Draw a checkerboard background into the destination rect.
    // -----------------------------------------------------------------------
    void drawCheckerboard( SDL_Renderer * renderer, const SDL_Rect & rect )
    {
        for ( int row = 0; row * checkSize < rect.h; ++row ) {
            for ( int col = 0; col * checkSize < rect.w; ++col ) {
                const bool light = ( ( row + col ) & 1 ) == 0;
                if ( light ) {
                    SDL_SetRenderDrawColor( renderer, 180, 180, 180, 255 );
                }
                else {
                    SDL_SetRenderDrawColor( renderer, 100, 100, 100, 255 );
                }
                SDL_Rect tile{ rect.x + col * checkSize, rect.y + row * checkSize,
                               std::min( checkSize, rect.w - col * checkSize ),
                               std::min( checkSize, rect.h - row * checkSize ) };
                SDL_RenderFillRect( renderer, &tile );
            }
        }
    }

    // -----------------------------------------------------------------------
    // Render a text string at (x, y) using white colour.
    // -----------------------------------------------------------------------
    void drawText( SDL_Renderer * renderer, TTF_Font * font, const std::string & text, int x, int y )
    {
        if ( font == nullptr || text.empty() ) {
            return;
        }

        const SDL_Color white{ 255, 255, 255, 255 };
        SDL_Surface * surf = TTF_RenderUTF8_Blended( font, text.c_str(), white );
        if ( surf == nullptr ) {
            return;
        }

        SDL_Texture * tex = SDL_CreateTextureFromSurface( renderer, surf );
        SDL_FreeSurface( surf );
        if ( tex == nullptr ) {
            return;
        }

        int w = 0;
        int h = 0;
        SDL_QueryTexture( tex, nullptr, nullptr, &w, &h );
        const SDL_Rect dst{ x, y, w, h };
        SDL_RenderCopy( renderer, tex, nullptr, &dst );
        SDL_DestroyTexture( tex );
    }
}

int main( int argc, char ** argv )
{
    const std::string aggPath = ( argc >= 2 ) ? argv[1] : "HEROES.AGG";

    // Open the AGG.
    fheroes2::AGGFile agg;
    if ( !agg.open( aggPath ) ) {
        std::cerr << "Cannot open " << aggPath << std::endl;
        return EXIT_FAILURE;
    }

    // Load the palette from KB.PAL inside the AGG.
    {
        const std::vector<uint8_t> palData = agg.read( "KB.PAL" );
        if ( palData.size() != 768 ) {
            std::cerr << "KB.PAL not found or invalid in " << aggPath << " (got " << palData.size() << " bytes)" << std::endl;
            return EXIT_FAILURE;
        }
        fheroes2::setGamePalette( palData );
    }

    const std::array<RGBA, 256> palette = buildRGBAPalette( fheroes2::getGamePalette() );

    // Collect all ICN and BMP filenames, sorted.
    std::vector<std::string> fileNames;
    {
        std::vector<std::string> wlks = agg.getFileNamesWithExtension( "WLK" );
        std::vector<std::string> stds = agg.getFileNamesWithExtension( "STD" );
        std::vector<std::string> atks = agg.getFileNamesWithExtension( "ATK" );
        std::vector<std::string> icns = agg.getFileNamesWithExtension( "ICN" );
        std::vector<std::string> bkgs = agg.getFileNamesWithExtension( "BKG" );
        std::vector<std::string> bmps = agg.getFileNamesWithExtension( "BMP" );
        fileNames.insert( fileNames.end(), wlks.begin(), wlks.end() );
        fileNames.insert( fileNames.end(), stds.begin(), stds.end() );
        fileNames.insert( fileNames.end(), atks.begin(), atks.end() );
        fileNames.insert( fileNames.end(), icns.begin(), icns.end() );
        fileNames.insert( fileNames.end(), bkgs.begin(), bkgs.end() );
        fileNames.insert( fileNames.end(), bmps.begin(), bmps.end() );
        std::sort( fileNames.begin(), fileNames.end() );
    }

    if ( fileNames.empty() ) {
        std::cerr << "No ICN or BMP files found in " << aggPath << std::endl;
        return EXIT_FAILURE;
    }

    // SDL init.
    if ( SDL_Init( SDL_INIT_VIDEO ) != 0 ) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return EXIT_FAILURE;
    }

    if ( TTF_Init() != 0 ) {
        std::cerr << "TTF_Init failed: " << TTF_GetError() << std::endl;
        SDL_Quit();
        return EXIT_FAILURE;
    }

    const std::string fontPath = findSystemFont();
    TTF_Font * font = fontPath.empty() ? nullptr : TTF_OpenFont( fontPath.c_str(), 14 );
    if ( font == nullptr ) {
        std::cerr << "Warning: could not load a font — text labels will be absent." << std::endl;
    }

    SDL_Window * window = SDL_CreateWindow( "imgview — HEROES.AGG browser", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, 0 );
    if ( window == nullptr ) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        TTF_Quit();
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_Renderer * renderer = SDL_CreateRenderer( window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC );
    if ( renderer == nullptr ) {
        renderer = SDL_CreateRenderer( window, -1, SDL_RENDERER_SOFTWARE );
    }
    if ( renderer == nullptr ) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow( window );
        TTF_Quit();
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_SetRenderDrawBlendMode( renderer, SDL_BLENDMODE_BLEND );

    // State.
    int fileIdx = 0;
    int frameIdx = 0;
    std::vector<fheroes2::Sprite> frames;
    SDL_Texture * frameTex = nullptr;

    const auto loadFile = [&]() {
        // Release previous texture.
        if ( frameTex != nullptr ) {
            SDL_DestroyTexture( frameTex );
            frameTex = nullptr;
        }
        frames.clear();
        frameIdx = 0;

        const std::string & name = fileNames[static_cast<size_t>( fileIdx )];
        const std::vector<uint8_t> raw = agg.read( name );

        const bool isICN = hasExtension( name, { ".ICN", ".BKG", ".WLK", ".STD", ".ATK" } );

        if ( isICN ) {
            frames = decodeICN( raw, name );
        }
        else {
            // HoMM1 BMP: raw palette-index format (6-byte header + w*h bytes).
            frames.push_back( decodeHoMM1BMP( raw ) );
        }

        // Remove completely empty frames (zero dimensions).
        frames.erase( std::remove_if( frames.begin(), frames.end(), []( const fheroes2::Sprite & s ) { return s.width() == 0 || s.height() == 0; } ),
                      frames.end() );
    };

    const auto buildTexture = [&]() {
        if ( frameTex != nullptr ) {
            SDL_DestroyTexture( frameTex );
            frameTex = nullptr;
        }

        if ( frames.empty() ) {
            return;
        }

        const fheroes2::Sprite & sprite = frames[static_cast<size_t>( frameIdx )];
        SDL_Surface * surf = spriteToSurface( sprite, palette );
        if ( surf != nullptr ) {
            frameTex = SDL_CreateTextureFromSurface( renderer, surf );
            SDL_FreeSurface( surf );
        }
    };

    loadFile();
    buildTexture();

    bool running = true;
    while ( running ) {
        SDL_Event event;
        while ( SDL_PollEvent( &event ) ) {
            if ( event.type == SDL_QUIT ) {
                running = false;
            }
            else if ( event.type == SDL_KEYDOWN ) {
                switch ( event.key.keysym.sym ) {
                case SDLK_ESCAPE:
                case SDLK_q:
                    running = false;
                    break;

                case SDLK_LEFT:
                    fileIdx = ( fileIdx + static_cast<int>( fileNames.size() ) - 1 ) % static_cast<int>( fileNames.size() );
                    loadFile();
                    buildTexture();
                    break;

                case SDLK_RIGHT:
                    fileIdx = ( fileIdx + 1 ) % static_cast<int>( fileNames.size() );
                    loadFile();
                    buildTexture();
                    break;

                case SDLK_UP:
                    if ( !frames.empty() ) {
                        frameIdx = ( frameIdx + static_cast<int>( frames.size() ) - 1 ) % static_cast<int>( frames.size() );
                        buildTexture();
                    }
                    break;

                case SDLK_DOWN:
                    if ( !frames.empty() ) {
                        frameIdx = ( frameIdx + 1 ) % static_cast<int>( frames.size() );
                        buildTexture();
                    }
                    break;

                default:
                    break;
                }
            }
        }

        // --- Render ---
        const SDL_Rect viewRect{ 0, 0, winW, winH - statusH };

        // Dark background for the view area.
        SDL_SetRenderDrawColor( renderer, 40, 40, 40, 255 );
        SDL_RenderFillRect( renderer, &viewRect );

        if ( frameTex != nullptr ) {
            int texW = 0;
            int texH = 0;
            SDL_QueryTexture( frameTex, nullptr, nullptr, &texW, &texH );

            // Scale to fit while preserving aspect ratio.
            const float scaleX = static_cast<float>( viewRect.w ) / static_cast<float>( texW );
            const float scaleY = static_cast<float>( viewRect.h ) / static_cast<float>( texH );
            const float scale = ( scaleX < scaleY ) ? scaleX : scaleY;
            // Cap scale at 4× to avoid absurdly large tiny sprites.
            const float finalScale = ( scale > 4.0f ) ? 4.0f : scale;

            const int dstW = static_cast<int>( static_cast<float>( texW ) * finalScale );
            const int dstH = static_cast<int>( static_cast<float>( texH ) * finalScale );
            const SDL_Rect dstRect{ viewRect.x + ( viewRect.w - dstW ) / 2, viewRect.y + ( viewRect.h - dstH ) / 2, dstW, dstH };

            // Checkerboard under transparent areas.
            drawCheckerboard( renderer, dstRect );

            SDL_RenderCopy( renderer, frameTex, nullptr, &dstRect );
        }
        else {
            // No image: show a message.
            drawText( renderer, font, "(no image data)", winW / 2 - 60, ( winH - statusH ) / 2 );
        }

        // Status bar background.
        const SDL_Rect statusRect{ 0, winH - statusH, winW, statusH };
        SDL_SetRenderDrawColor( renderer, 20, 20, 20, 255 );
        SDL_RenderFillRect( renderer, &statusRect );

        // Separator line.
        SDL_SetRenderDrawColor( renderer, 80, 80, 80, 255 );
        SDL_RenderDrawLine( renderer, 0, winH - statusH, winW, winH - statusH );

        if ( font != nullptr ) {
            const std::string & name = fileNames[static_cast<size_t>( fileIdx )];

            std::string label = name;
            label += "  [" + std::to_string( fileIdx + 1 ) + "/" + std::to_string( fileNames.size() ) + "]";

            if ( !frames.empty() ) {
                label += "  frame " + std::to_string( frameIdx + 1 ) + "/" + std::to_string( frames.size() );
                const fheroes2::Sprite & sp = frames[static_cast<size_t>( frameIdx )];
                label += "  " + std::to_string( sp.width() ) + "×" + std::to_string( sp.height() );
                if ( sp.x() != 0 || sp.y() != 0 ) {
                    label += "  offset(" + std::to_string( sp.x() ) + "," + std::to_string( sp.y() ) + ")";
                }
            }
            else {
                label += "  (empty)";
            }

            drawText( renderer, font, label, 8, winH - statusH + ( statusH - 14 ) / 2 );

            // Right-side hint.
            drawText( renderer, font, "←→ file   ↑↓ frame   Q quit", winW - 260, winH - statusH + ( statusH - 14 ) / 2 );
        }

        SDL_RenderPresent( renderer );
    }

    if ( frameTex != nullptr ) {
        SDL_DestroyTexture( frameTex );
    }
    if ( font != nullptr ) {
        TTF_CloseFont( font );
    }
    SDL_DestroyRenderer( renderer );
    SDL_DestroyWindow( window );
    TTF_Quit();
    SDL_Quit();

    return EXIT_SUCCESS;
}
